#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiConnect.h>

extern "C" {
#include "c_types.h"
#include "user_interface.h"
#include "wpa2_enterprise.h"
}

// #define UPDATE_FREQ 5000  // ms
#define SERVER_URL "http://aipl.duckdns.org:3000/flow_rate"
#define FLOW_RATE_ARRAY_SIZE 5  // allows for easy calculation of litres from L/min.
#define FLOW_RATE_RATIO 11.0    // f = 11 * Q where Q: flow rate, f: freq
#define SENSOR_PIN D5

WiFiClient client;
HTTPClient http;
WiFiConnect wc;
WiFiConnectParam user_id("user_id", "User ID", "", 16);

void beginWiFi();
void light_sleep();

void setup() {
    Serial.begin(115200);
    // Serial.setDebugOutput(true);

    gpio_init();

    pinMode(SENSOR_PIN, INPUT);

    Serial.println();
    Serial.println();
    Serial.println();

    beginWiFi();
}

unsigned long lastUpdate = 0;
double flowRates[FLOW_RATE_ARRAY_SIZE];
int freqi = 0;

void loop() {
    int fedges = 0;

    unsigned long t1 = millis(), t2 = t1 + 1;

    // calculate no. of falling edges in 1s
    for (bool prev = 0; t2 - t1 < 1000; t2 = millis()) {
        bool r = digitalRead(SENSOR_PIN);
        fedges += prev && !r;
        // Serial.printf("prev: %i, r: %i\n", prev, r);
        // if (!prev && r) Serial.println("rising edge");
        // if (prev && !r) Serial.println("falling edge");
        prev = r;
        delay(1);  // theoretical max freq 1000 hz
    }
    flowRates[freqi] = fedges / FLOW_RATE_RATIO;
    Serial.printf("freq %i:  %i\n", freqi, fedges);
    freqi++;

    if (freqi == FLOW_RATE_ARRAY_SIZE) {
        freqi = 0;
        double freqSum = 0;
        for (int i = 0; i < FLOW_RATE_ARRAY_SIZE; i++) {
            freqSum += flowRates[i];
        }
        if (freqSum == 0) {  // sleep if no activity for FLOW_RATE_ARRAY_SIZE seconds
            light_sleep();
            Serial.println("light sleep done");
        } else {
            postFlowRate();
        }
    }
}

void light_sleep() {
    // wifi_station_disconnect();
    wifi_set_opmode(NULL_MODE);
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
    wifi_fpm_open();
    wifi_fpm_set_wakeup_cb([]() {
        Serial.println("wakeup");
        // Serial.flush();
    });
    GPIO_INT_TYPE intr = digitalRead(SENSOR_PIN) ? GPIO_PIN_INTR_LOLEVEL : GPIO_PIN_INTR_HILEVEL;
    gpio_pin_wakeup_enable(GPIO_ID_PIN(SENSOR_PIN), intr);
    Serial.println("Sleeping...");
    delay(10);  // fix watchdog reset
    wifi_fpm_do_sleep(0xFFFFFFF);
    delay(10);
}

void postFlowRate() {
    // reconnect WiFi if disconnected
    if (WiFi.status() != WL_CONNECTED && !wc.autoConnect())
        wc.startConfigurationPortal(AP_WAIT);
    http.begin(client, SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    String body = "{\"interval\":1000,\"flow_rate\":[";
    for (int i = 0; i < FLOW_RATE_ARRAY_SIZE - 1; i++) {
        body += flowRates[i];
        body += ",";
    }
    body += flowRates[FLOW_RATE_ARRAY_SIZE - 1];  // add last flow rate measurement
    body += "]}";
    int httpCode = http.POST(body);

    Serial.printf("POST HTTP Code %d\n", httpCode);
}

void beginWiFi() {
    wc.setDebug(true);

    /* Set our callbacks */
    wc.setAPCallback([](WiFiConnect *mWiFiConnect) {
        Serial.println("Entering Access Point");
    });

    wc.addParameter(&user_id);

    // wc.resetSettings();

    if (!wc.autoConnect()) {  // try to connect to wifi
        /* We could also use button etc. to trigger the portal on demand within main loop */
        wc.startConfigurationPortal(AP_RESET);  // if not connected,
    }
}