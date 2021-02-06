
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Arduino_JSON.h>
#include <Ticker.h>
#include <Time.h>

#include <env_options.h>

#include <net_routine.h>

// ----------- Пины esp8285 -----------------
#define PIN_BTN1        0
#define PIN_BTN2        9
#define PIN_BTN3        10
#define PIN_RELAY1      12  
#define PIN_RELAY2      5  
#define PIN_RELAY3      4
#define PIN_WIFI_LED    13

// ----------- Пины nodemcu -----------------
// #define PIN_BTN1        14
// #define PIN_BTN2        12
// #define PIN_BTN3        13
// #define PIN_RELAY1      4
// #define PIN_RELAY2      5 
// #define PIN_RELAY3      16
// #define PIN_WIFI_LED    LED_BUILTIN

#define LED_STATE_ON            LOW
#define LED_STATE_OFF           HIGH

#define BUTTONS_COUNT   3
#define BUTTON_STATE_PUSH       LOW
#define BUTTON_STATE_RELEASE    HIGH

#define RELAYS_COUNT   3
#define RELAY_STATE_ON  HIGH
#define RELAY_STATE_OFF LOW

#define AUTOOFF_DELAY_SECS  3600


#define SR_WAITING      BIT0
#define SR_PONG         BIT1
#define SR_RELAY1       BIT2
#define SR_RELAY2       BIT3
#define SR_RELAY3       BIT4
#define SR_RELAYS       SR_RELAY1 | SR_RELAY2 | SR_RELAY3       
#define SR_EXIT_CODE    BIT5
#define SR_DEVINFO      BIT6

uint8_t uiReportBits = 0;
uint8_t uiExitCode = 0;

typedef struct {
    uint8_t uiPin;
    uint8_t uiRead;
    uint8_t uiState;
    bool bChanged;
    uint32_t ulChangedAt;
    uint32_t ulPrevStateDuration;
} button_t;

button_t xButtons[BUTTONS_COUNT] = {
    { PIN_BTN1, BUTTON_STATE_RELEASE, BUTTON_STATE_RELEASE, false, 0, 0 },
    { PIN_BTN2, BUTTON_STATE_RELEASE, BUTTON_STATE_RELEASE, false, 0, 0 },
    { PIN_BTN3, BUTTON_STATE_RELEASE, BUTTON_STATE_RELEASE, false, 0, 0 },
};


typedef enum {
    RELAY_CMD_NONE = 0,
    RELAY_CMD_ON,
    RELAY_CMD_OFF,
    RELAY_CMD_TOGGLE
} relay_command_t;

typedef struct {
    uint8_t uiPin;
    uint8_t uiState;
    relay_command_t xScheduledCommand;
    bool bScheduledCommandChanged;
    uint8_t uiReportBit;
    uint32_t ulChangedAt;
    uint16_t uiAutoOffAfterSecs;
    Ticker xAutoOffTimer;
} relay_t;

relay_t xRelays[RELAYS_COUNT] = {
    { PIN_RELAY1, RELAY_STATE_OFF, RELAY_CMD_NONE, false, SR_RELAY1, 0, AUTOOFF_DELAY_SECS },
    { PIN_RELAY2, RELAY_STATE_OFF, RELAY_CMD_NONE, false, SR_RELAY2, 0, AUTOOFF_DELAY_SECS },
    { PIN_RELAY3, RELAY_STATE_OFF, RELAY_CMD_NONE, false, SR_RELAY3, 0, AUTOOFF_DELAY_SECS },
};

char pcPingPayload[20] = "0";
int64_t iPingPayload = 0;

Ticker xReadButtonsTimer;
Ticker xRelayCommandTimer;
Ticker xReportTimer;


uint32_t uiLastPingReceived = 0;
bool bExternalControlEnabled = false;
Ticker xNoPingWatchTimer;

Ticker xBlinkTimer;
uint8_t uiBlinks;
uint8_t uiLedState = LED_STATE_OFF;

void vBlinkHandler() {
    if (uiLedState == LED_STATE_OFF) {
        if (uiBlinks == 0) return;
        uiLedState = LED_STATE_ON;
        digitalWrite(PIN_WIFI_LED, uiLedState);
        xBlinkTimer.once_ms(200, vBlinkHandler);
    } else {
        uiBlinks--;
        uiLedState = LED_STATE_OFF;
        digitalWrite(PIN_WIFI_LED, uiLedState);
        if (uiBlinks > 0) xBlinkTimer.once_ms(300, vBlinkHandler);
    }
}

void vBlink(uint8_t uiCount) {
    if (uiCount == 0) return;
    uiBlinks = uiCount;
    uiLedState = LED_STATE_OFF;
    vBlinkHandler();
}


void vNoPingWatchHandler() {
    if (bExternalControlEnabled) return;
    uint16_t uiDiff = (millis() - uiLastPingReceived) / 1000;
    if (uiDiff > WAIT_FOR_PING_SECS) {
        bExternalControlEnabled = false;
        Serial.printf("[ vNoPingWatchHandler ] Ping Watch timer interrupt. No pings were received in %d secs\n", uiDiff);
    }
}

void vPingCB(char* pcTopic, char* pcPayload, size_t len) {
    bExternalControlEnabled = true;
    vBlink(1);
    uiLastPingReceived = millis();
    Serial.printf("[ vPingCB ] MQTT Ping received. Payload: %s\n", pcPayload);
    strcpy(pcPingPayload, pcPayload);
    iPingPayload = atoll(pcPingPayload);
    if (timeStatus() == timeNotSet && iPingPayload != 0) {
        time_t xTime = iPingPayload / 1000 + SECS_PER_HOUR * TIME_ZONE;
        setTime(xTime);
        Serial.printf("[ vPingCB ] Time is set to %02d.%02d.%04d %02d:%02d:%02d\n", day(), month(), year(), hour(), minute(), second());
    }
    uiReportBits |= SR_WAITING | SR_PONG | SR_RELAYS;
}

relay_command_t xParseRelayCommand(char * pcState) {
    if ((strcmp(pcState, "ON") == 0) || (strcmp(pcState, "on") == 0)) {
        return RELAY_CMD_ON;
    } 
    else if ((strcmp(pcState, "OFF") == 0) || (strcmp(pcState, "off") == 0)) {
        return RELAY_CMD_OFF;
    }
    else if ((strcmp(pcState, "TOGGLE") == 0) || (strcmp(pcState, "toggle") == 0)) {
        return RELAY_CMD_TOGGLE;
    } else {
        Serial.printf("[ xParseRelayCommand ] Unsupported state %s\n", pcState);
        return RELAY_CMD_NONE;
    }
}

void vMessageCB(char* pcTopic, char* pcPayload, size_t len) {
    Serial.printf("[ vMessageCB ] Event arrived!\n");
    vBlink(1);
    JSONVar pxPayload = JSON.parse(pcPayload);

    if (JSON.typeof(pxPayload) != "undefined") {
        Serial.print("[ vMessageCB ] MQTT payload successfully parsed as JSON: ");
        Serial.println(JSON.stringify(pxPayload));
    } else {
        Serial.println("[ vMessageCB ] FAIL Can't parse MQTT payload. Nothing to do :(");
        return;
    }
    
    char pcState[20];

    if (pxPayload.hasOwnProperty("l1_state")) {
        strcpy(pcState, (const char*) pxPayload["l1_state"]);
        xRelays[0].xScheduledCommand = xParseRelayCommand(pcState);
        xRelays[0].bScheduledCommandChanged = (xRelays[0].xScheduledCommand != RELAY_CMD_NONE);
    }

    if (pxPayload.hasOwnProperty("l2_state")) {
        strcpy(pcState, (const char*) pxPayload["l2_state"]);
        xRelays[1].xScheduledCommand = xParseRelayCommand(pcState);
        xRelays[1].bScheduledCommandChanged = (xRelays[1].xScheduledCommand != RELAY_CMD_NONE);
    }

    if (pxPayload.hasOwnProperty("l3_state")) {
        strcpy(pcState, (const char*) pxPayload["l3_state"]);
        xRelays[2].xScheduledCommand = xParseRelayCommand(pcState);
        xRelays[2].bScheduledCommandChanged = (xRelays[2].xScheduledCommand != RELAY_CMD_NONE);
    }

    if (pxPayload.hasOwnProperty("command")) {
        char pcCommand[20];
        strcpy(pcCommand, (const char*) pxPayload["command"]);
        if ((strcmp(pcCommand, "STATUS") == 0) || (strcmp(pcCommand, "status") == 0)) {
            uiReportBits |= SR_WAITING | SR_DEVINFO;
        } 
    }

    delete pxPayload;
    // Serial.printf("4.free mem:\t%d\n", system_get_free_heap_size());
}

void vNetEventCB(net_event_code_t xEventCode) {
    // Serial.printf("[ vNetEventCB ] Event code %i\n", uiEventCode);
    switch (xEventCode) {
    case NE_WIFI_CONNECTED:
        vBlink(3);
        break;
    case NE_MQTT_DISCONNECTED:
        // vBlink(5);
        bExternalControlEnabled = false;    
    default:
        break;
    }

}

void vStateReportHandler() {
    if (!(uiReportBits & SR_WAITING)) return;
    uint8_t uiBits = uiReportBits;
    uiReportBits = 0;
    JSONVar pxReport;
    // char cBuf[32];

    if (uiBits & SR_PONG) { pxReport["pong"] = (double)iPingPayload; }
    if (uiBits & SR_RELAY1) { pxReport["l1_state"] = (xRelays[0].uiState == RELAY_STATE_ON) ? "ON" : "OFF"; }
    if (uiBits & SR_RELAY2) { pxReport["l2_state"] = (xRelays[1].uiState == RELAY_STATE_ON) ? "ON" : "OFF"; }
    if (uiBits & SR_RELAY3) { pxReport["l3_state"] = (xRelays[2].uiState == RELAY_STATE_ON) ? "ON" : "OFF"; }
    if (uiBits & SR_EXIT_CODE) { pxReport["exit_code"] = uiExitCode; }
    if (uiBits & SR_DEVINFO) { 
        pxReport["device_id"] = NR_DEVICE_ID; 
        pxReport["device_alias"] = NR_DEVICE_ALIAS; 
        pxReport["ip_address"] = WiFi.localIP().toString(); 
    }
    
    String sReport = JSON.stringify(pxReport);
    Serial.printf("[ vStateReport ] Report prepared! %s\n", sReport.c_str());
    vPublishReport(sReport);
    delete pxReport;
}

void vReadButtonsIO() {
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        xButtons[i].uiRead = digitalRead(xButtons[i].uiPin);
        if (xButtons[i].uiRead != xButtons[i].uiState) {
            uint32_t ulNow = millis(); 
            xButtons[i].ulPrevStateDuration = ulNow - xButtons[i].ulChangedAt;
            xButtons[i].ulChangedAt = ulNow;
            xButtons[i].bChanged = true;
            xButtons[i].uiState = xButtons[i].uiRead;
        }
    }
}

void vReadButtonsHandler() {
    vReadButtonsIO();

    for (int i = 0; i < BUTTONS_COUNT; i++) {
        if (xButtons[i].bChanged) {
            xButtons[i].bChanged = false;
            Serial.printf("[ vReadButtonsHandler ] Button %i changed => %i\n", i, xButtons[i].uiState);
            if (xButtons[i].uiState == BUTTON_STATE_PUSH) {
                xRelays[i].xScheduledCommand = RELAY_CMD_TOGGLE;
                xRelays[i].bScheduledCommandChanged = true;
            }
        }
    }
}

void vAutoOffCB(uint8_t uiRelayIdx) {
    if (bExternalControlEnabled) return;
    Serial.printf("[ vAutoOffCB ] Relay %i turned OFF!\n", uiRelayIdx);
    xRelays[uiRelayIdx].xScheduledCommand = RELAY_CMD_OFF;
    xRelays[uiRelayIdx].bScheduledCommandChanged = true;

}

void vRelayCommandScheduledHandler() {
    for (int i = 0; i < RELAYS_COUNT; i++) {
        if (xRelays[i].bScheduledCommandChanged) {
            uint8_t uiCurrState = digitalRead(xRelays[i].uiPin);
            xRelays[i].bScheduledCommandChanged = false;
            switch (xRelays[i].xScheduledCommand) {
            case RELAY_CMD_NONE:
                continue;
            case RELAY_CMD_ON:
                xRelays[i].uiState = RELAY_STATE_ON;
                break;        
            case RELAY_CMD_OFF:
                xRelays[i].uiState = RELAY_STATE_OFF;
                break;
            case RELAY_CMD_TOGGLE:
                xRelays[i].uiState = !uiCurrState;
                break;
            }
            if (xRelays[i].xScheduledCommand != RELAY_CMD_NONE) {
                if (xRelays[i].uiState != uiCurrState) {
                    xRelays[i].ulChangedAt = millis(); // Фиксируем только реальное изменение состояния
                }
                digitalWrite(xRelays[i].uiPin, xRelays[i].uiState);
                uiReportBits |= SR_WAITING | xRelays[i].uiReportBit;
                Serial.printf("[ vRelayCommandScheduledHandler ] Relay %i set to %i\n", i, xRelays[i].uiState);
                if (xRelays[i].uiState == RELAY_STATE_ON && !bExternalControlEnabled && xRelays[i].uiAutoOffAfterSecs > 0) {
                    Serial.printf("[ vRelayCommandScheduledHandler ] AutoOff for Relay %i scheduled after %i secs\n", i, xRelays[i].uiAutoOffAfterSecs);
                    xRelays[i].xAutoOffTimer.once(xRelays[i].uiAutoOffAfterSecs, vAutoOffCB, (uint8_t)i);
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\n\n\n");
    Serial.printf("====================================================\n");
    Serial.printf("Welcome! Device " NR_DEVICE_ID " prepared to run... \n");
    Serial.printf("====================================================\n\n");

    pinMode(PIN_RELAY1, OUTPUT); digitalWrite(PIN_RELAY1, RELAY_STATE_OFF);
    pinMode(PIN_RELAY2, OUTPUT); digitalWrite(PIN_RELAY2, RELAY_STATE_OFF);
    pinMode(PIN_RELAY3, OUTPUT); digitalWrite(PIN_RELAY3, RELAY_STATE_OFF);

    pinMode(PIN_WIFI_LED, OUTPUT); digitalWrite(PIN_WIFI_LED, LED_STATE_OFF); 

    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_BTN2, INPUT_PULLUP);
    pinMode(PIN_BTN3, INPUT_PULLUP);

    xReadButtonsTimer.attach_ms(READ_BUTTONS_HANDLER_PERIOD_MS, vReadButtonsHandler);
    xRelayCommandTimer.attach_ms(READ_BUTTONS_HANDLER_PERIOD_MS, vRelayCommandScheduledHandler);
    xReportTimer.attach_ms(REPORT_HANDLER_SEND_MS, vStateReportHandler);

    xNoPingWatchTimer.attach(WAIT_FOR_PING_SECS, vNoPingWatchHandler);
    pvMessageCB = vMessageCB;
    pvPingCB = vPingCB;
    netSetup();
    pvNetEventCB = vNetEventCB;
    // vBlink(3);
}

void loop() {
   handleNetRoutine();
}