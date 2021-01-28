
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFiConnect.h>

extern "C" {
#include "c_types.h"
#include "user_interface.h"
#include "wpa2_enterprise.h"
}

// #define UPDATE_FREQ 5000  // ms
#define SERVER_URL "http://aipl.duckdns.org:3000/flow_volume"
#define FLOW_TIMEOUT 5000     // how many ms of flow rate inactivity to send data and sleep.
#define FLOW_RATE_RATIO 10.0  // f = 10 * Q where Q: litres/min, f: Hz
#define SENSOR_PIN D5

WiFiClient client;
WiFiConnect wc;
WiFiConnectParam user_id("user_id", "User ID", "", 11, "required type=\"number\" min=\"1\" max=\"4294967295\"");
WiFiConnectParam chip_id_display("chip_id", "Chip ID", "", 25, "readonly");

void beginWiFi();
void light_sleep();

volatile unsigned long fedges = 0;
volatile unsigned long lastedge = millis();

void ICACHE_RAM_ATTR flowrate_ISR() {
    fedges++;
    lastedge = millis();
    // Serial.print("millis: ");
    // Serial.println(lastedge);
}

void setup() {
    Serial.begin(115200);
    // Serial.setDebugOutput(true);
    pinMode(SENSOR_PIN, INPUT);

    Serial.println("\n");

    // display chip_id
    String cidDisplay = "Your Chip ID: ";
    cidDisplay += system_get_chip_id();
    chip_id_display.setValue(cidDisplay.c_str());
    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), flowrate_ISR, FALLING);

    // reset();
    // saveConfig();
    loadConfig();
    beginWiFi();
}

// double flowVolume = 0;  // litres
// int inactiveSecs = 0;

void loop() {
    unsigned long recorded_lastedge = lastedge;  // race condition when using lastedge and millis() directly.
    if (millis() - recorded_lastedge > FLOW_TIMEOUT) {
        unsigned long recorded_fedges = fedges;
        double flowVolume = recorded_fedges / FLOW_RATE_RATIO / 60.0;
        Serial.print(F("flowVolume: "));
        Serial.println(flowVolume);

        saveConfig();  // save flowVolume
        int code = postFlowVolume(flowVolume);
        Serial.print(F("POST HTTP Code "));
        Serial.println(code);

        if (code == 200) fedges -= recorded_fedges;
        saveConfig();  // save config again if flowVolume is uploaded
        if (fedges == 0) light_sleep();
    }
    delay(1000);
}

// // calculate no. of falling edges in 1s
// int calculateFrequency(uint8_t pin) {
//     int fedges = 0;
//     unsigned long t1 = millis(), t2 = t1 + 1;
//     for (bool prev = 0; t2 - t1 < 1000; t2 = millis()) {
//         bool r = digitalRead(pin);
//         fedges += prev && !r;
//         prev = r;
//         delay(1);  // sampling rate: 1000 hz
//     }
//     return fedges;
// }

void light_sleep() {
    // wifi_station_disconnect();
    wifi_set_opmode(NULL_MODE);
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
    wifi_fpm_open();
    wifi_fpm_set_wakeup_cb([]() {
        Serial.print(F("wakeup: "));
        Serial.println(millis());
        // Serial.flush();
    });
    // wakeup on changed logic level
    GPIO_INT_TYPE intr = digitalRead(SENSOR_PIN) ? GPIO_PIN_INTR_LOLEVEL : GPIO_PIN_INTR_HILEVEL;
    gpio_pin_wakeup_enable(GPIO_ID_PIN(SENSOR_PIN), intr);
    Serial.println(F("Sleeping..."));
    delay(10);                     // fix watchdog reset
    wifi_fpm_do_sleep(0xFFFFFFF);  // sleep forever until interrupt
    delay(10);                     // required to sleep
}

void saveConfig() {
    // if (!configNeedsSaving) return;
    // configNeedsSaving = false;
    if (!LittleFS.begin()) {
        Serial.println("UNABLE to open LittleFS");
        return;
    }
    uint32 uid = String(user_id.getValue()).toInt();  // assume checked
    if (uid <= 0 || uid > UINT32_MAX)
        Serial.printf("uid out of bounds (%u)\n", uid);

    Serial.print("Writing file... ");
    Serial.printf("user_id=%u ", uid);
    Serial.printf("fedges=%lu\n", fedges);
    File file = LittleFS.open("/config", "w");
    file.printf("user_id=%u\n", uid);
    file.printf("fedges=%lu\n", fedges);
    file.close();
}

void loadConfig() {
    if (!LittleFS.begin()) {
        Serial.println("UNABLE to open LittleFS");
        return;
    }
    Serial.println("Reading file...");
    File file = LittleFS.open("/config", "r");
    String line;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.startsWith("user_id=")) {
            user_id.setValue(line.substring(8).c_str());
        } else if (line.startsWith("fedges=")) {
            fedges = line.substring(7).toDouble();
        }
        Serial.println(line);
    }
    file.close();
}

int postFlowVolume(double flowVolume) {
    HTTPClient http;
    // reconnect WiFi if disconnected
    autoConnectWiFi();
    http.begin(client, SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    String body = "{\"chip_id\":";
    body += system_get_chip_id();
    body += ",\"user_id\":\"";
    body += user_id.getValue();
    body += "\",\"flow_volume\":";
    body += String(flowVolume, 5);
    body += "}";

    return http.POST(body);
}

void autoConnectWiFi() {
    if (!wc.autoConnect())
        wc.startConfigurationPortal(AP_NONE);
}

void beginWiFi() {
    wc.setDebug(true);
    // wc.setConnectionTimeoutSecs(5);
    /* Set our callbacks */
    wc.setAPCallback([](WiFiConnect *mWiFiConnect) {
        Serial.println(F("Entering Access Point"));
    });
    wc.setSaveConfigCallback(saveConfig);

    wc.addParameter(&user_id);
    wc.addParameter(&chip_id_display);

    // wc.startConfigurationPortal(AP_RESET);  // for testing the wifi/config portal

    autoConnectWiFi();
}

void reset() {
    if (!LittleFS.begin()) {
        Serial.println("UNABLE to open LittleFS");
        return;
    }
    Serial.printf("format: %i\n", LittleFS.format());
}