#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>

// --- FIREBASE LIBRARIES ---
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include "secrets.h"

// --- PIN CONFIGURATION ---
#define BUTTON_PIN 18  // GPIO18 connected to the button
#define DHTPIN1 16     // GPIO16 connected to Internal DHT
#define DHTPIN2 17     // GPIO17 connected to External DHT

// --- GLOBAL VARIABLES ---
int lastButtonState = HIGH;               // Previous state of the button
unsigned long previousMillis = 0;         // Timer for RTDB (Fast)
unsigned long previousHistoryMillis = 0;  // Timer for Firestore (Slow)

const long interval = 5000;          // RTDB update: every 5 seconds
const long historyInterval = 60000;  // Firestore history: every 60 seconds

// Display state variables
bool displayOn = true;  // Tracks if the display is currently On or Off
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;  // 50ms to filter out button noise (debounce)

// --- FIREBASE OBJECTS ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;  // True if connection is succesful

// --- OBJECTS ---
Adafruit_BMP280 bmp;  // I2C Interface
Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_ADDR 0x3C    // OLED I2C address

// Declaration for an SSD1306 display connected to I2C (SDA=21, SCL=22 on ESP32)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT);

#define DHTTYPE DHT22
DHT dht1(DHTPIN1, DHTTYPE);  // Internal Sensor
DHT dht2(DHTPIN2, DHTTYPE);  // External Sensor

// --- HELPER FUNCTION FOR BOOT STATUS ---
// This function updates the OLED with the current boot status
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

void setup() {
  Serial.begin(115200);

  // Initialize OLED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("Error: SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();

  printBootStatus("Init Sensors...");

  // Configure button pin with internal pull-up resistor
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  dht1.begin();
  dht2.begin();

  // Initialize BMP280
  if (!bmp.begin()) {
    Serial.println(F("Error: BMP280 sensor not found!"));
    printBootStatus("Error:", "BMP280 missing");
    delay(2000);
  }

  // BMP280 Default settings
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  printBootStatus("Connecting WiFi...", WIFI_SSID);

  // --- WIFI CONNECTION ---
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  // Show IP Address on display
  printBootStatus("WiFi Connected!", "IP: " + WiFi.localIP().toString());
  delay(2000);  // Keep it on screen for 2 seconds so you can read it

  // --- TIME SYNC (NTP) ---
  // Essential for SSL/Firebase connection
  Serial.print("Syncing time (NTP)...");
  printBootStatus("Syncing Time (NTP)...");
  configTime(0, 0, "time.google.com", "pool.ntp.gov");

  // Rome timezone
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  time_t now = time(nullptr);
  int retry = 0;
  // Wait for time to sync, but timeout after ~20 seconds to prevent hanging
  while (now < 1600000000 && retry < 40) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }

  if (now < 1600000000) {
    Serial.println("\nTime sync failed. SSL might fail.");
    printBootStatus("Time Sync Failed", "Proceeding anyway...");
    delay(2000);
  } else {
    Serial.println("\nTime synced!");
    // --- SHOW TIME ON BOOT DISPLAY ---
    struct tm *timeinfo = localtime(&now);
    char timeBuffer[10];
    // Format the time as HH:MM into the buffer
    sprintf(timeBuffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);

    printBootStatus("Time Synced!", String(timeBuffer));

    delay(2000);
  }

  // --- FIREBASE SETUP ---
  printBootStatus("Init Firebase...", "Auth Request...");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Callback for status token
  config.token_status_callback = tokenStatusCallback;

  fbdo.setResponseSize(4096);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  printBootStatus("Firebase Ready!", "Waiting for data...");
  delay(1500);

  // Mark as ready (validation happens in loop)
  signupOK = true;
}

void loop() {
  // --- BUTTON MANAGEMENT (WITH DEBOUNCE) ---
  int reading = digitalRead(BUTTON_PIN);

  // If the switch changed, due to noise or pressing, reset the debouncing timer
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // Only toggle if the state has been stable for longer than the delay
  if ((millis() - lastDebounceTime) > debounceDelay) {

    static int buttonState = HIGH;  // Tracks the stable state

    // If the stable state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // Action only when the button is actually PRESSED (LOW)
      if (buttonState == LOW) {
        displayOn = !displayOn;  // Toggle state (True -> False, or False -> True)

        if (displayOn) {
          display.ssd1306_command(SSD1306_DISPLAYON);
        } else {
          display.ssd1306_command(SSD1306_DISPLAYOFF);
        }
      }
    }
  }

  lastButtonState = reading;

  // --- SENSOR READING AND FIREBASE UPLOAD ---
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read sensors
    sensors_event_t pressure_event;
    bmp_pressure->getEvent(&pressure_event);

    float hi = dht1.readHumidity();
    float ti = dht1.readTemperature();
    float he = dht2.readHumidity();
    float te = dht2.readTemperature();
    float p = pressure_event.pressure;

    // Get current timestamp
    time_t now = time(nullptr);

    // Serial Debug output
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

    // Update display ONLY if it is currently ON (saves time)
    if (displayOn) {
      display.clearDisplay();

      // Timestamp 'now' in readable format
      struct tm *timeinfo = localtime(&now);

      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(20, 0);
      display.printf("%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);

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

    // Firebase data sending
    if (Firebase.ready() && signupOK) {
      // Use setFloatAsync to not block for too long loop, or setFloat
      // Path: /Sensors/Internal/Temp, ecc.

      // RTDB update
      if (Firebase.RTDB.setFloat(&fbdo, "/Sensors/Internal/Temperature", ti)) {
        Serial.println("Firebase Send OK!");
      } else {
        Serial.print("Firebase Error: ");
        Serial.println(fbdo.errorReason());
      }

      Firebase.RTDB.setFloat(&fbdo, "/Sensors/Internal/Humidity", hi);
      Firebase.RTDB.setFloat(&fbdo, "/Sensors/External/Temperature", te);
      Firebase.RTDB.setFloat(&fbdo, "/Sensors/External/Humidity", he);
      Firebase.RTDB.setFloat(&fbdo, "/Sensors/Pressure", p);

      // Firestore update
      if (currentMillis - previousHistoryMillis >= historyInterval) {
        previousHistoryMillis = currentMillis;

        Serial.println("Preparing Firestore upload...");

        // Create JSON for Firestore
        // Firestore requires explicit typing (doubleValue, integerValue)
        FirebaseJson content;

        content.set("fields/timestamp/integerValue", (int)now);

        if (!isnan(ti))
          content.set("fields/temperature_i/doubleValue", ti);
        if (!isnan(hi))
          content.set("fields/humidity_i/doubleValue", hi);
        if (!isnan(te))
          content.set("fields/temperature_e/doubleValue", te);
        if (!isnan(he))
          content.set("fields/humidity_e/doubleValue", he);
        if (!isnan(p))
          content.set("fields/pressure/doubleValue", p);

        // Define path: Collection "history" -> Document ID (Timestamp)
        String docId = String((int)now);

        // Combine collection and document ID into a single path string to avoid ambiguity error
        String documentPath = "history/" + docId;

        // We pass documentPath instead of separating collection and ID
        if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "(default)", documentPath.c_str(), content.raw())) {
          Serial.println("Firestore History Saved!");
        } else {
          Serial.print("Firestore Error: ");
          Serial.println(fbdo.errorReason());
        }
      }
    }
  }
}