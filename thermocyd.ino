#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>  // For storing settings persistently

#include <XPT2046_Touchscreen.h>

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];


// Pin definitions
#define ONE_WIRE_BUS 4  // DS18B20 data pin
#define RELAY_PIN 5     // Output relay control

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
lv_obj_t* labelTemp;
lv_obj_t* sliderTemp;

// Function to read temperature
float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

// Get the Touchscreen data
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if(touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

    data->state = LV_INDEV_STATE_PRESSED;

    // Set the coordinates
    data->point.x = x;
    data->point.y = y;

    // Print Touchscreen info about X, Y and Pressure (Z) on the Serial Monitor
    /* Serial.print("X = ");
    Serial.print(x);
    Serial.print(" | Y = ");
    Serial.print(y);
    Serial.print(" | Pressure = ");
    Serial.print(z);
    Serial.println();*/
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
 
// LVGL slider event callback
static void slider_event_cb(lv_event_t* e) {
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
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


void setup() {
  Serial.begin(115200);
  sensors.begin();
  pinMode(RELAY_PIN, OUTPUT);
  Serial.print("ThermoCYD v1.1 Welcome.");

  // Load saved settings
  preferences.begin("thermostat", false);
  setTemperature = preferences.getFloat("setTemp", 22.0);
  hysteresis = preferences.getFloat("hysteresis", 0.5);

  // WiFi setup
  WiFi.begin("*********", "*********");
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(1000);
  }
  Serial.println("Connected!");

  // MQTT setup
  //   client.setServer(mqttServer, 1883);
  //  client.setCallback(callback);
  //  reconnectMQTT();

  // LVGL setup
  lv_init();

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 0: touchscreen.setRotation(0);
  touchscreen.setRotation(2);

  // Create a display object
  lv_display_t* disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);

 }

void loop() {
  float currentTemp = readTemperature();
  Serial.printf("Current Temp: %.2f\n", currentTemp);

  // Thermostat logic
  if (currentTemp < setTemperature - hysteresis) {
    digitalWrite(RELAY_PIN, HIGH);  // Turn on heating
  } else if (currentTemp > setTemperature + hysteresis) {
    digitalWrite(RELAY_PIN, LOW);  // Turn off heating
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
