#define I_BOARD "SonOff_T3"
#define I_FRAMEWORK "arduino"
#define I_VERSION "1.0.2"

#define TIME_ZONE 3
#define NR_SYNC_TIME_NTP    true
#define NR_SYNC_TIME_MQTT   true
#define NR_NTP_SERVER       "0.pool.ntp.org"

#define NR_DEVICE_ID "sonoff"
#define NR_MQTT_REPORT_TOPIC "myhome/sonoff"
#define NR_MQTT_SET_TOPIC "myhome/sonoff/set"
#define NR_MQTT_PING_TOPIC "myhome/sonoff/ping"
#define NR_DEVICE_ALIAS "SonOff_T3"


#define NR_SSID ""
#define NR_PASSWORD ""
#define NR_OTA_PASSWORD ""
#define NR_MQTT_USER ""
#define NR_MQTT_PASSWORD ""
#define NR_MQTT_SERVER_URL ""
#define NR_MQTT_SERVER_PORT 1883

#define WAIT_FOR_PING_SECS      600
#define IM_ALONE_TIMEOUT_MS  600 * 1000
#define READ_BUTTONS_HANDLER_PERIOD_MS 60
#define REPORT_HANDLER_IDLE_MS    50
#define REPORT_HANDLER_SEND_MS    500
#define SCHEDULE_TASK_DELAY_MS    5000
