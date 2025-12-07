#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// #include <MQUnifiedsensor.h>

// --- PIN CONFIGURATION ---
#define BUTTON_PIN 18  // GPIO18 connected to the button
#define DHTPIN1 16     // GPIO16 connected to Internal DHT
#define DHTPIN2 17     // GPIO17 connected to External DHT
// #define MQ135PIN 32

// --- GLOBAL VARIABLES ---
int lastButtonState = HIGH;        // Previous state of the button
unsigned long previousMillis = 0;  // Stores last time data was read
const long interval = 5000;        // Delay to read from sensors in ms (5 seconds)

// Display state variables
bool displayOn = true;  // Tracks if the display is currently On or Off
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;  // 50ms to filter out button noise (debounce)

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

void setup() {
  Serial.begin(9600);

  // Configure button pin with internal pull-up resistor
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dht1.begin();
  dht2.begin();

  // Initialize BMP280
  if (!bmp.begin()) {
    Serial.println(F("Error: BMP280 sensor not found!"));
  }

  // BMP280 Default settings
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  // Initialize OLED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("Error: SSD1306 allocation failed"));
  }

  display.clearDisplay();
  display.display();
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

  // --- SENSOR READING ---
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read sensors
    sensors_event_t pressure_event;
    bmp_pressure->getEvent(&pressure_event);

    float hi = dht1.readHumidity();
    float ti = dht1.readTemperature() - 2.0;  // Manual offset correction
    float he = dht2.readHumidity() - 2.5;     // Manual offset correction
    float te = dht2.readTemperature() - 1.5;  // Manual offset correction
    float p = pressure_event.pressure;

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

      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(0, 0);
      display.println("Values");

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
  }
}