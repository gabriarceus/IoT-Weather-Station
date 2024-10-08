#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define BUTTON_PIN 18 // GPIO18 pin connected to button
int lastState = HIGH; // the previous state from the input pin
int currentState;     // the current reading from the input pin
unsigned long previousMillis = 0; //will store last time data was read
const long interval = 5000; //delay to read from sensors in ms
int i = 0; //to keep track if the display is On or Off

Adafruit_BMP280 bmp; // use I2C interface
Adafruit_Sensor *bmp_temp = bmp.getTemperatureSensor(); //non dovrebbe servire
Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_ADDR   0x3C // OLED display address for I2C

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT);

#define DHTPIN1 16           //Pin a cui è connesso il sensore di temperatura interna
#define DHTPIN2 17           //Pin a cui è connesso il sensore di temperatura esterna
#define DHTTYPE DHT22       //Tipo di sensore che stiamo utilizzando (DHT22)
DHT dht1(DHTPIN1, DHTTYPE); //Inizializza oggetto chiamato "dht", parametri: pin a cui è connesso il sensore, tipo di dht 11/22
DHT dht2(DHTPIN2, DHTTYPE); //dht1 è il sensore interno. dht2 è il sensore esterno

void setup() {
  Serial.begin(9600);
  dht1.begin();
  dht2.begin();

  unsigned status;
  //status = bmp.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID);
  status = bmp.begin();

  /* Default settings from datasheet. */
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

}

void loop() {

  // read the state of the switch/button:
  currentState = digitalRead(BUTTON_PIN);

  if(lastState == LOW && currentState == HIGH)
    if(i == 0){
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    i++;}
    else {
    display.ssd1306_command(SSD1306_DISPLAYON);
    i--;}
  // save the last state
  lastState = currentState;

  // questa è la parte con delay
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // save the last time you read from sensors
    previousMillis = currentMillis;
  
  //da qui parte il codice

  sensors_event_t pressure_event;
  bmp_pressure->getEvent(&pressure_event);

  float hi = dht1.readHumidity();
  float ti = dht1.readTemperature() - 2.0;
  float he = dht2.readHumidity() - 2.5;
  float te = dht2.readTemperature() - 1.5;
  float p = pressure_event.pressure;

  //Stampa nel serial monitor
  Serial.print(ti);
  Serial.print(" , ");
  Serial.print(hi);
  Serial.print(" , ");
  Serial.print(p);
  Serial.print(" , ");
  Serial.print(te);
  Serial.print(" , ");
  Serial.print(he);
  Serial.println();

  //Stampa su display OLED
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Valori");

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 17);
  display.print("I humidity: ");
  display.print(hi);
  display.println("%");
  display.print("I temp: ");
  display.print(ti);
  display.println(" C");
  display.print("E humidity: ");
  display.print(he);
  display.println("%");
  display.print("E temp: ");
  display.print(te);
  display.println(" C");
  display.print("Atm pres: ");
  display.print(p);
  display.println(" hPa");

  display.display();
  }

}