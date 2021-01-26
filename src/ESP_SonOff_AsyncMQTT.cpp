
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Arduino_JSON.h>
#include <Ticker.h>
#include <Time.h>

#include <env_options.h>

#include <net_routine.h>

// ----------- Пины для выключателя -----------------
// #define PIN_BTN1        0
// #define PIN_BTN2        9
// #define PIN_BTN3        10
// #define PIN_RELAY1      12  
// #define PIN_RELAY2      5  
// #define PIN_RELAY3      4
// #define PIN_WIFI_LED    13

#define PIN_BTN1        2
#define PIN_BTN2        4
#define PIN_BTN3        5
#define PIN_RELAY1      12
#define PIN_RELAY2      14 
#define PIN_RELAY3      15
#define PIN_WIFI_LED    16


char pcPingPayload[20] = "0";
int64_t iPingPayload = 0;

Ticker xReadIOTimer;
Ticker xReadButtonsTimer;
uint32_t uiLastPingReceived = 0;
bool bExternalControlEnabled = false;
Ticker xNoPingWatchTimer;


static uint8_t uiBtn1State = HIGH;
static uint8_t uiBtn1Read = HIGH;
static bool bBtn1Changed = false;
static uint8_t uiBtn2State = HIGH;
static uint8_t uiBtn2Read = HIGH;
static bool bBtn2Changed = false;
static uint8_t uiBtn3State = HIGH;
static uint8_t uiBtn3Read = HIGH;
static bool bBtn3Changed = false;


void vStateReport(bool bAlarm);

void vNoPingWatchHandler() {
    if (bExternalControlEnabled) return;
    uint16_t uiDiff = (millis() - uiLastPingReceived) / 1000;
    if (uiDiff > WAIT_FOR_PING_SECS) {
        bExternalControlEnabled = true;
        Serial.printf("[ vNoPingWatchHandler ] Ping Watch timer interrupt. No pings were received in %d secs\n", uiDiff);
    }
}

void vPingCB(char* pcTopic, char* pcPayload, size_t len) {
    bExternalControlEnabled = true;
    uiLastPingReceived = millis();
    Serial.printf("[ PingCB ] MQTT Ping received. Payload: %s\n", pcPayload);
    strcpy(pcPingPayload, pcPayload);
    iPingPayload = atoll(pcPingPayload);
    if (timeStatus() == timeNotSet && iPingPayload != 0) {
        time_t xTime = iPingPayload / 1000 + SECS_PER_HOUR * TIME_ZONE;
        setTime(xTime);
        Serial.printf("[ PingCB ] Time is set to %02d.%02d.%04d %02d:%02d:%02d\n", day(), month(), year(), hour(), minute(), second());
    }
    vStateReport(false);
}

void vMessageCB(char* pcTopic, char* pcPayload, size_t len) {
    Serial.printf("[ MessageCB ] Event arrived!\n");

    JSONVar pxPayload = JSON.parse(pcPayload);

    if (JSON.typeof(pxPayload) != "undefined") {
        Serial.print("[ MessageCB ] MQTT payload successfully parsed as JSON: ");
        Serial.println(JSON.stringify(pxPayload));
    } else {
        Serial.println("[ MessageCB ] FAIL Can't parse MQTT payload. Nothing to do :(");
        return;
    }

    if (pxPayload.hasOwnProperty("command")) {
        char pcCommmand[20];
        strcpy(pcCommmand, (const char*) pxPayload["command"]);
        // if (strcmp(pcCommmand, "STOP") == 0) {
        //     vWingCommand(WING_CMD_STOP);
        // } 
        // else if (strcmp(pcCommmand, "OPEN") == 0) {
        //     vWingCommand(WING_CMD_OPEN);            
        // } 
        // else if (strcmp(pcCommmand, "CLOSE") == 0) {
        //     vWingCommand(WING_CMD_CLOSE);            
        // } else {
        //     Serial.printf("[ MessageCB ] Unsupported command %s\n", pcCommmand);
        // }
    }

    delete pxPayload;
    // Serial.printf("4.free mem:\t%d\n", system_get_free_heap_size());
}



void vStateReport(bool bAlarm) {
    JSONVar pxReport;
    char cBuf[32];

    if (bAlarm) {
        // * Если репорт формируется из-за какого-то события, отправляем только краткое сообщение о событии и выходим.
    } 
    else {
        // Это обычный регулярный отчет о состоянии. Формируем полный отчет и выходим.
        pxReport["pong"] = (double)iPingPayload;
    }

    String sReport = JSON.stringify(pxReport);
    Serial.printf("[ vStateReport ] Report prepared! %s\n", sReport.c_str());
    vPublishReport(sReport);
    delete pxReport;
}

void vReadButtonsIO() {
    uiBtn1Read = digitalRead(PIN_BTN1);
    if (uiBtn1Read != uiBtn1State) {
        uiBtn1State = uiBtn1Read;
        bBtn1Changed = true;
    }
    uiBtn2Read = digitalRead(PIN_BTN2);
    if (uiBtn2Read != uiBtn2State) {
        uiBtn2State = uiBtn2Read;
        bBtn2Changed = true;
    }
    uiBtn3Read = digitalRead(PIN_BTN3);
    if (uiBtn3Read != uiBtn3State) {
        uiBtn3State = uiBtn3Read;
        bBtn3Changed = true;
    }
}

void vReadButtonsHandler() {
    vReadButtonsIO();
    if (bBtn1Changed) {
        bBtn1Changed = false;
        Serial.printf("[ vReadButtonsHandler ] Button 1 changed => %i\n", uiBtn1State);
    }
    if (bBtn2Changed) {
        bBtn2Changed = false;
        Serial.printf("[ vReadButtonsHandler ] Button 2 changed => %i\n", uiBtn2State);
    }
    if (bBtn3Changed) {
        bBtn3Changed = false;
        Serial.printf("[ vReadButtonsHandler ] Button 3 changed => %i\n", uiBtn3State);
    }
}


void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println();

    pinMode(PIN_RELAY1, OUTPUT); 
    digitalWrite(PIN_RELAY1, LOW);
    pinMode(PIN_RELAY2, OUTPUT); 
    digitalWrite(PIN_RELAY2, LOW);
    pinMode(PIN_RELAY3, OUTPUT); 
    digitalWrite(PIN_RELAY3, LOW);

    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_BTN2, INPUT_PULLUP);
    pinMode(PIN_BTN3, INPUT_PULLUP);


    xReadButtonsTimer.attach_ms(50, vReadButtonsHandler);


    xNoPingWatchTimer.attach(WAIT_FOR_PING_SECS, vNoPingWatchHandler);
    pvMessageCB = vMessageCB;
    pvPingCB = vPingCB;
    netSetup();

}

void loop() {
   handleNetRoutine();
}