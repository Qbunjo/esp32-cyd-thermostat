#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <TimeLib.h>

// Pin definitions
#define ONE_WIRE_BUS 4
#define RELAY_PIN 5

// Settings
float setTemperature = 22.0;
float hysteresis = 0.5;
bool heatingOn = false;
bool inSettingsPanel = false;
bool inSchedulePanel = false;

// Schedule
struct ScheduleEntry {
  int hour;
  int minute;
  float temperature;
};
ScheduleEntry schedule[7][24];

// MQTT
const char* mqttServer = "your.mqtt.server";
const char* mqttUser = "mqtt_user";
const char* mqttPassword = "mqtt_pass";

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TFT_eSPI tft = TFT_eSPI();

// LVGL
lv_obj_t *labelTemp;
lv_obj_t *labelHeating;
lv_obj_t *sliderTemp;
lv_obj_t *panelMain;
lv_obj_t *panelSchedule;
lv_obj_t *panelSettings;

// Function declarations
void create_main_panel();
void create_schedule_panel();
void create_settings_panel();

float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

void updateHeatingIndicator() {
  lv_label_set_text_fmt(labelHeating, "Heating: %s", heatingOn ? "ON" : "OFF");
}

void btn_open_schedule_cb(lv_event_t *e) {
  lv_obj_add_flag(panelMain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(panelSchedule, LV_OBJ_FLAG_HIDDEN);
}

void btn_open_settings_cb(lv_event_t *e) {
  lv_obj_add_flag(panelMain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(panelSettings, LV_OBJ_FLAG_HIDDEN);
}

void btn_back_cb(lv_event_t *e) {
  lv_obj_clear_flag(panelMain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(panelSchedule, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(panelSettings, LV_OBJ_FLAG_HIDDEN);
}

void slider_temp_cb(lv_event_t *e) {
  setTemperature = lv_slider_get_value(sliderTemp);
  preferences.putFloat("setTemp", setTemperature);
  lv_label_set_text_fmt(labelTemp, "Set Temp: %.1f C", setTemperature);
}

void create_main_panel() {
  panelMain = lv_obj_create(lv_scr_act());
  lv_obj_set_size(panelMain, 240, 135);

  labelTemp = lv_label_create(panelMain);
  lv_label_set_text_fmt(labelTemp, "Set Temp: %.1f C", setTemperature);
  lv_obj_align(labelTemp, LV_ALIGN_TOP_MID, 0, 10);

  sliderTemp = lv_slider_create(panelMain);
  lv_slider_set_range(sliderTemp, 10, 30);
  lv_slider_set_value(sliderTemp, setTemperature, LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderTemp, slider_temp_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_align(sliderTemp, LV_ALIGN_CENTER, 0, 0);

  labelHeating = lv_label_create(panelMain);
  lv_label_set_text(labelHeating, "Heating: OFF");
  lv_obj_align(labelHeating, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_t *btnSched = lv_btn_create(panelMain);
  lv_obj_align(btnSched, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btnSched, btn_open_schedule_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl1 = lv_label_create(btnSched);
  lv_label_set_text(lbl1, "Schedule");

  lv_obj_t *btnSet = lv_btn_create(panelMain);
  lv_obj_align(btnSet, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btnSet, btn_open_settings_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl2 = lv_label_create(btnSet);
  lv_label_set_text(lbl2, "Settings");
}

void create_schedule_panel() {
  panelSchedule = lv_obj_create(lv_scr_act());
  lv_obj_set_size(panelSchedule, 240, 135);
  lv_obj_add_flag(panelSchedule, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *lbl = lv_label_create(panelSchedule);
  lv_label_set_text(lbl, "Schedule Panel");
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *btnBack = lv_btn_create(panelSchedule);
  lv_obj_align(btnBack, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btnBack, btn_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, "Back");
}

void create_settings_panel() {
  panelSettings = lv_obj_create(lv_scr_act());
  lv_obj_set_size(panelSettings, 240, 135);
  lv_obj_add_flag(panelSettings, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *lbl = lv_label_create(panelSettings);
  lv_label_set_text(lbl, "Settings Panel");
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *btnBack = lv_btn_create(panelSettings);
  lv_obj_align(btnBack, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btnBack, btn_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, "Back");
}

void setup() {
  Serial.begin(115200);
  sensors.begin();
  pinMode(RELAY_PIN, OUTPUT);

  preferences.begin("thermostat", false);
  setTemperature = preferences.getFloat("setTemp", 22.0);
  hysteresis = preferences.getFloat("hysteresis", 0.5);

  WiFi.begin("your_ssid", "your_pass");
  while (WiFi.status() != WL_CONNECTED) delay(1000);

  client.setServer(mqttServer, 1883);

  lv_init();
  tft.begin();
  tft.setRotation(1);
  lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.flush_cb = NULL; // replace with actual flush callback
  lv_disp_drv_register(&disp_drv);

  create_main_panel();
  create_schedule_panel();
  create_settings_panel();
}

void loop() {
  float currentTemp = readTemperature();

  if (currentTemp < setTemperature - hysteresis) {
    digitalWrite(RELAY_PIN, HIGH);
    heatingOn = true;
  } else if (currentTemp > setTemperature + hysteresis) {
    digitalWrite(RELAY_PIN, LOW);
    heatingOn = false;
  }

  updateHeatingIndicator();
  lv_task_handler();
  delay(10);
}
