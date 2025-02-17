#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h> // For storing settings persistently

// Pin definitions
#define ONE_WIRE_BUS 4  // DS18B20 data pin
#define RELAY_PIN 5      // Output relay control

// Hysteresis settings
float setTemperature = 22.0;
float hysteresis = 0.5;

// MQTT settings
const char* mqttServer = "your.mqtt.server";
const char* mqttUser = "your_mqtt_user";
const char* mqttPassword = "your_mqtt_password";
const char* mqttTopicSetTemp = "home/thermostat/setTemp";
const char* mqttTopicHysteresis = "home/thermostat/hysteresis";
const char* mqttTopicTemp = "home/thermostat/currentTemp";

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TFT_eSPI tft = TFT_eSPI();
lv_obj_t *labelTemp;
lv_obj_t *sliderTemp;

// Function to read temperature
float readTemperature() {
    sensors.requestTemperatures();
    return sensors.getTempCByIndex(0);
}

// LVGL slider event callback
static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    setTemperature = lv_slider_get_value(slider);
    lv_label_set_text_fmt(labelTemp, "Set Temp: %.1f C", setTemperature);
    preferences.putFloat("setTemp", setTemperature);
}

// Function to handle MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    if (String(topic) == mqttTopicSetTemp) {
        setTemperature = message.toFloat();
        preferences.putFloat("setTemp", setTemperature);
        lv_slider_set_value(sliderTemp, setTemperature, LV_ANIM_ON);
        lv_label_set_text_fmt(labelTemp, "Set Temp: %.1f C", setTemperature);
    } else if (String(topic) == mqttTopicHysteresis) {
        hysteresis = message.toFloat();
        preferences.putFloat("hysteresis", hysteresis);
    }
}

void reconnectMQTT() {
    while (!client.connected()) {
        if (client.connect("ESP32_Thermostat", mqttUser, mqttPassword)) {
            client.subscribe(mqttTopicSetTemp);
            client.subscribe(mqttTopicHysteresis);
        } else {
            delay(5000);
        }
    }
}

void setupLVGL() {
    lv_obj_t * scr = lv_scr_act();
    labelTemp = lv_label_create(scr);
    lv_label_set_text_fmt(labelTemp, "Set Temp: %.1f C", setTemperature);
    lv_obj_align(labelTemp, LV_ALIGN_TOP_MID, 0, 20);
    
    sliderTemp = lv_slider_create(scr);
    lv_slider_set_range(sliderTemp, 10, 30);
    lv_slider_set_value(sliderTemp, setTemperature, LV_ANIM_OFF);
    lv_obj_align(sliderTemp, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(sliderTemp, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void setup() {
    Serial.begin(115200);
    sensors.begin();
    pinMode(RELAY_PIN, OUTPUT);
    
    // Load saved settings
    preferences.begin("thermostat", false);
    setTemperature = preferences.getFloat("setTemp", 22.0);
    hysteresis = preferences.getFloat("hysteresis", 0.5);
    
    // WiFi setup
    WiFi.begin("your_SSID", "your_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }
    
    // MQTT setup
    client.setServer(mqttServer, 1883);
    client.setCallback(callback);
    reconnectMQTT();
    
    // LVGL setup
    lv_init();
    tft.begin();
    tft.setRotation(1);
    setupLVGL();
}

void loop() {
    float currentTemp = readTemperature();
    Serial.printf("Current Temp: %.2f\n", currentTemp);
    
    // Thermostat logic
    if (currentTemp < setTemperature - hysteresis) {
        digitalWrite(RELAY_PIN, HIGH);  // Turn on heating
    } else if (currentTemp > setTemperature + hysteresis) {
        digitalWrite(RELAY_PIN, LOW);   // Turn off heating
    }
    
    // Publish temperature to MQTT
    client.publish(mqttTopicTemp, String(currentTemp).c_str());
    
    // Maintain MQTT connection
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();
    
    lv_task_handler();
    delay(10);
}
