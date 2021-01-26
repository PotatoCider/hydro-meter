
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
#define FLOW_TIMEOUT 60       // how many seconds of flow rate inactivity to send data and sleep.
#define FLOW_RATE_RATIO 10.0  // f = 10 * Q where Q: litres/min, f: Hz
#define SENSOR_PIN D5

WiFiClient client;
HTTPClient http;
WiFiConnect wc;
WiFiConnectParam user_id("user_id", "User ID", "", 6, "required type=\"number\" min=\"0\" max=\"65535\"");
WiFiConnectParam chip_id_display("chip_id", "Chip ID", "", 25, "readonly");

void beginWiFi();
void light_sleep();

// bool configNeedsSaving = false;
void setup() {
    Serial.begin(115200);
    // Serial.setDebugOutput(true);
    pinMode(SENSOR_PIN, INPUT);

    Serial.println("\n");

    // display chip_id
    String cidDisplay = "Your Chip ID: ";
    cidDisplay += system_get_chip_id();
    chip_id_display.setValue(cidDisplay.c_str());

    // reset();
    // saveConfig();
    loadConfig();

    beginWiFi();
}

double flowVolume = 0;  // litres
int inactiveSecs = 0;

void loop() {
    int sensorFreq = calculateFrequency(SENSOR_PIN);
    flowVolume += sensorFreq;  // flow volume is currently scaled by FLOW_RATE_RATIO * 600.0

    if (sensorFreq)
        inactiveSecs = 0;
    else
        inactiveSecs++;

    Serial.print(F("flowVolume: "));
    Serial.println(flowVolume / FLOW_RATE_RATIO / 60.0);

    if (inactiveSecs == FLOW_TIMEOUT) {
        inactiveSecs = 0;

        saveConfig();  // save flowVolume
        int code = postFlowVolume(flowVolume / FLOW_RATE_RATIO / 60.0);
        Serial.print(F("POST HTTP Code"));
        Serial.println(code);

        if (code == 200) flowVolume = 0;
        saveConfig();  // save config again if flowVolume is uploaded
        light_sleep();
    }
}

// calculate no. of falling edges in 1s
int calculateFrequency(uint8_t pin) {
    int fedges = 0;
    unsigned long t1 = millis(), t2 = t1 + 1;
    for (bool prev = 0; t2 - t1 < 1000; t2 = millis()) {
        bool r = digitalRead(pin);
        fedges += prev && !r;
        prev = r;
        delay(1);  // sampling rate: 1000 hz
    }
    return fedges;
}

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
    delay(10);
}

void saveConfig() {
    // if (!configNeedsSaving) return;
    // configNeedsSaving = false;
    if (!LittleFS.begin()) {
        Serial.println("UNABLE to open LittleFS");
        return;
    }
    uint16 uid = String(user_id.getValue()).toInt();  // assume checked
    if (uid <= 0 || uid > INT16_MAX)
        Serial.printf("uid out of bounds (%u)\n", uid);

    Serial.println("Writing file...");
    Serial.printf("writing: user_id=%u\n", uid);
    File file = LittleFS.open("/config", "w");
    file.printf("user_id=%u\n", uid);
    file.printf("flow_volume=%.6f\n", flowVolume);
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
        } else if (line.startsWith("flow_volume=")) {
            flowVolume = line.substring(12).toDouble();
        }
        Serial.println(line);
    }
    file.close();
}

int postFlowVolume(double flowVolume) {
    // reconnect WiFi if disconnected
    if (WiFi.status() != WL_CONNECTED && !wc.autoConnect())
        wc.startConfigurationPortal(AP_NONE);
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

void beginWiFi() {
    wc.setDebug(true);

    /* Set our callbacks */
    wc.setAPCallback([](WiFiConnect *mWiFiConnect) {
        Serial.println(F("Entering Access Point"));
    });
    wc.setSaveConfigCallback(saveConfig);

    wc.addParameter(&user_id);
    wc.addParameter(&chip_id_display);

    // wc.startConfigurationPortal(AP_RESET);  // for testing the wifi/config portal

    if (!wc.autoConnect()) {  // try to connect to wifi
        /* We could also use button etc. to trigger the portal on demand within main loop */
        wc.startConfigurationPortal(AP_NONE);  // if not connected, continue to measure
    }
}

void reset() {
    if (!LittleFS.begin()) {
        Serial.println("UNABLE to open LittleFS");
        return;
    }
    Serial.printf("format: %i\n", LittleFS.format());
}