#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>

// --- BROWNOUT FIX LIBRARIES ---
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- FIREBASE LIBRARIES ---
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include "secrets.h"

// --- PIN CONFIGURATION ---
#define BUTTON_PIN 18
#define DHTPIN1 16
#define DHTPIN2 17

// --- GLOBAL SHARED VARIABLES (Thread-safe) ---
volatile float shared_ti = NAN;
volatile float shared_hi = NAN;
volatile float shared_te = NAN;
volatile float shared_he = NAN;
volatile float shared_p = NAN;

// --- GLOBAL VARIABLES ---
int lastButtonState = HIGH;
bool displayOn = true;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 200;
unsigned long previousMillis = 0;
const long interval = 5000;

// --- FIREBASE OBJECTS ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- SENSORS & DISPLAY OBJECTS ---
Adafruit_BMP280 bmp;
Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT);

#define DHTTYPE DHT22
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

// --- TASK HANDLES ---
TaskHandle_t TaskNetwork;

// --- HELPER FUNCTION FOR BOOT STATUS ---
void printBootStatus(String message, String subMessage = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 5);
  display.println(F("SYSTEM BOOT"));

  display.setCursor(0, 25);
  display.println(message);

  if (subMessage != "") {
    display.setCursor(0, 45);
    display.println(subMessage);
  }

  display.display();
}

// ==========================================
// TASK 2: NETWORK & FIREBASE (Core 0)
// ==========================================
void networkTask(void *parameter) {
  unsigned long lastHistoryRun = 0;
  const long historyInterval = 60000;

  // Initial WiFi Connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);  // Non-blocking delay
  }

  // Time Sync
  configTime(0, 0, "time.google.com", "pool.ntp.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  // Firebase Config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  // Set timeouts
  config.timeout.wifiReconnect = 10000;
  config.timeout.socketConnection = 10000;

  fbdo.setResponseSize(4096);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  for (;;) {
    if (Firebase.ready()) {

      // --- RTDB UPDATE ---
      if (!isnan(shared_ti)) Firebase.RTDB.setFloatAsync(&fbdo, "/Sensors/Internal/Temperature", shared_ti);
      if (!isnan(shared_hi)) Firebase.RTDB.setFloatAsync(&fbdo, "/Sensors/Internal/Humidity", shared_hi);
      if (!isnan(shared_te)) Firebase.RTDB.setFloatAsync(&fbdo, "/Sensors/External/Temperature", shared_te);
      if (!isnan(shared_he)) Firebase.RTDB.setFloatAsync(&fbdo, "/Sensors/External/Humidity", shared_he);
      if (!isnan(shared_p)) Firebase.RTDB.setFloatAsync(&fbdo, "/Sensors/Pressure", shared_p);

      // --- FIRESTORE UPDATE ---
      if (millis() - lastHistoryRun >= historyInterval) {
        lastHistoryRun = millis();

        time_t now = time(nullptr);

        if (now > 1600000000) {
          Serial.println("Preparing Firestore upload...");

          FirebaseJson content;
          content.set("fields/timestamp/integerValue", (int)now);

          if (!isnan(shared_ti)) content.set("fields/temperature_i/doubleValue", shared_ti);
          if (!isnan(shared_hi)) content.set("fields/humidity_i/doubleValue", shared_hi);
          if (!isnan(shared_te)) content.set("fields/temperature_e/doubleValue", shared_te);
          if (!isnan(shared_he)) content.set("fields/humidity_e/doubleValue", shared_he);
          if (!isnan(shared_p)) content.set("fields/pressure/doubleValue", shared_p);

          String documentPath = "history/" + String((int)now);

          if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "(default)", documentPath.c_str(), content.raw())) {
            Serial.println("Firestore History Saved!");
          } else {
            Serial.print("Firestore Error: ");
            Serial.println(fbdo.errorReason());
          }
        }
      }
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// SETUP (Main Core)
// ==========================================
void setup() {
  // FIX: Disable Brownout Detector to prevent boot loops on WiFi start
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Init Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.display();

  printBootStatus("Init Sensors...");

  // Init Pins & Sensors
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  dht1.begin();
  dht2.begin();
  if (!bmp.begin()) {
    Serial.println(F("Error: BMP280 sensor not found!"));
    printBootStatus("Error:", "BMP280 missing");
    delay(2000);
  }
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2, Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16, Adafruit_BMP280::STANDBY_MS_500);

  printBootStatus("Starting Network...");

  xTaskCreatePinnedToCore(
    networkTask,
    "NetworkTask",
    20000,
    NULL,
    1,
    &TaskNetwork,
    0);

  delay(1000);  // Allow power to stabilize before loop
}

// ==========================================
// LOOP (Main Core 1)
// ==========================================
void loop() {
  // --- BUTTON MANAGEMENT ---
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    static int buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        displayOn = !displayOn;
        if (displayOn) display.ssd1306_command(SSD1306_DISPLAYON);
        else display.ssd1306_command(SSD1306_DISPLAYOFF);
      }
    }
  }
  lastButtonState = reading;

  // --- SENSOR READING & DISPLAY ---
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    sensors_event_t pressure_event;
    bmp_pressure->getEvent(&pressure_event);

    float hi = dht1.readHumidity();
    float ti = dht1.readTemperature() - 1.5;
    float he = dht2.readHumidity();
    float te = dht2.readTemperature() -1.5;
    float p = pressure_event.pressure;

    shared_ti = ti;
    shared_hi = hi;
    shared_te = te;
    shared_he = he;
    shared_p = p;

    Serial.print("T_Int: ");
    Serial.print(ti);
    Serial.print(" | H_Int: ");
    Serial.print(hi);
    Serial.print(" | Pres: ");
    Serial.print(p);
    Serial.print(" | T_Ext: ");
    Serial.print(te);
    Serial.print(" | H_Ext: ");
    Serial.println(he);

    if (displayOn) {
      display.clearDisplay();

      time_t now = time(nullptr);
      struct tm *timeinfo = localtime(&now);

      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(20, 0);

      if (timeinfo->tm_year > 70) {
        display.printf("%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
      } else {
        display.print("--:--");
      }

      display.setTextSize(1);
      display.setCursor(0, 17);

      display.print("I Humid:  ");
      display.print(hi);
      display.println("%");
      display.print("I Temp:   ");
      display.print(ti);
      display.println(" C");
      display.print("E Humid:  ");
      display.print(he);
      display.println("%");
      display.print("E Temp:   ");
      display.print(te);
      display.println(" C");
      display.print("Pressure: ");
      display.print(p);
      display.println(" hPa");

      display.display();
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Firebase Send OK!");
    }
  }
}