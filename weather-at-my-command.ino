/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn
Copyright (c) 2021 by Tomáš Macek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <Arduino.h>
#include "secrets.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <JsonListener.h>
#include <DHTesp.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
 
// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
// OLED
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
// Services
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"

#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"


/***************************
 * Configuration
 **************************/
// To be set based on your particular conditions
const boolean IS_METRIC = true;  // set true for celsius
#define TZ              1       // (utc+) Time Zone in hours
#define DST_MN          60      // use 60min for summer time in some countries
const int UPDATE_INTERVAL_SECS = 20 * 60; // Update every 20 minutes weather forecast
const int UPDATE_MEASURE = 5;   // Time interval between conducting the measurement by local sensors
const int UPLOAD_MEASURE = 60;  // Time interval between uploading sensor values
#define TIME_PER_FRAME  3000    // frame switching period
#define FRAME_MEASURE   3       // measure frame - the one displayed if no wifi
const char* upload_host = "api.thingspeak.com";   // use empty string if you do not want to upload your measured data
//const char* upload_host = "";   
/* Go to https://openweathermap.org/find?q= and search for a location. Go through the
result set and select the entry closest to the actual location you want to display 
data for. It'll be a URL like https://openweathermap.org/city/2657896. The number
at the end is what you assign to the constant below. */
String OPEN_WEATHER_MAP_LOCATION_ID = "3067696";

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
String OPEN_WEATHER_MAP_LANGUAGE = "cz";
const uint8_t MAX_FORECASTS = 4;

// Adjust according to your language
//const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
//const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const String WDAY_NAMES[] = {"NE ", "PO ", "UT ", "ST ", "CT ", "PA ", "SO "};
const String MONTH_NAMES[] = {"LED", "UNO", "BRE", "DUB", "KVE", "CE1", "CE2", "SRP", "ZAR", "RIJ", "LIS", "PRO"};

/***************************
 * Mapping pins and fixed configuration
 **************************/
// DHT11
#define DHTPIN D6     
#define DHTTYPE DHTesp::DHT11   // DHT 11

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = D3;
const int SDC_PIN = D4;

/***************************
 * Global definitions
 **************************/ 
DHTesp dht11;               // Temperature-humidity sensor DHT11
BH1750 bh1750(0x23);  // Light meeter BH1750
Adafruit_BMP280 bmp280; // use I2C interface
Adafruit_Sensor *temperatureBMP280 = bmp280.getTemperatureSensor();
Adafruit_Sensor *pressureBMP280 = bmp280.getPressureSensor();

// Initialize the oled display for address 0x3c
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );

// variables to hold measured balues by sensors
float humidityDHT11 = 0.0;
float temperatureDHT11 = 0.0;
float luxBH1750 = 0.0;
sensors_event_t temperatureEventBMP280, pressureEventBMP280;
time_t now;

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

bool readyForWeatherUpdate = false;   // flag changed in the ticker function every 10 minutes
String lastUpdate = "--";
long timeSinceLastWUpdate = 0;        // Time measured since last weather information download from a service (Open weather map)
long timeSinceMeasured = 0;           // Time since last measuring  
long timeSinceUpdateThinkSpeak = 0;   // Time measured since last upload of weather information to ThinkSpeak

// Prototypes 
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawMeasured(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();

// This array keeps function pointers to all frames
FrameCallback frames[] = { drawDateTime, drawCurrentWeather, drawForecast, drawMeasured };
int numberOfFrames = 4;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

//*********************************************************
//***************** UPDATE
//*********************************************************
#define MAX_WIFI_CONNECT 50    // if greater unseccesful connects -> act as disconected
bool connectedWifi = false;   // indicator that connection was made

// Indicators that init was conducted ok for the sensor
bool initializedDHT11 = false;
bool initializedBH1750 = false;
bool initializedBMP280 = false;

void updateDHT11() { 
  // Updates values from DHT11 sensor (temp and humidity sensor)
  //  updated: humidityDHT11 and temperatureDHT11

  if (!initializedDHT11){ // if not initialized, try it now
     dht11.setup(DHTPIN, DHTTYPE); // init DHTP  
     initializedDHT11 = true;
  }
  temperatureDHT11 = dht11.getTemperature();
  humidityDHT11 = dht11.getHumidity();
  Serial.print("temperatureDHT11: "); Serial.print(temperatureDHT11); Serial.println((IS_METRIC ? "°C" : "°F"));
  Serial.print("humidityDHT11: "); Serial.print(humidityDHT11); Serial.println(" percent");
}

void updateBH1750() {
  // Updates value from BH1750 
  //   updated: luxBH1750

  if (!initializedBH1750){
    if (bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {   // Init BH1750 - light
      Serial.println(F("BH1750 initialized"));
      initializedBH1750 = true;
    }
    else {
      Serial.println(F("Error initialising BH1750"));
      initializedBH1750 = false;
      return;
    }   
  }
  
  if (bh1750.measurementReady()) {
    luxBH1750 = bh1750.readLightLevel();
    Serial.print("Light: "); Serial.print(luxBH1750); Serial.println(" lx");
  }
}

void updateBMP280() {
  // Updates values from BMP280 sensor
  //   updated: temperatureEventBMP280 and pressureEventBMP280
  if (!initializedBMP280){
    Serial.println("****** Initializing BMP280");

    Wire.begin(D3, D4);  // Initialize BMP280
    if (!bmp280.begin(0x76)) {
      Serial.println(F("Could not find a valid BMP280 sensor!"));
      initializedBMP280 = false;
      return;
    }else{
      Serial.println(F("BMP280 sensor initialized")); 
      initializedBMP280 = true;
      // Default settings from datasheet. 
      /*
      bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Operating Mode. 
                      Adafruit_BMP280::SAMPLING_X2,     // Temp. oversampling 
                      Adafruit_BMP280::SAMPLING_X16,    // Pressure oversampling 
                      Adafruit_BMP280::FILTER_X16,      // Filtering. 
                      Adafruit_BMP280::STANDBY_MS_500); // Standby time. 
                      
      temperatureBMP280->printSensorDetails();
      */
    }  
  }
  temperatureBMP280->getEvent(&temperatureEventBMP280);
  pressureBMP280->getEvent(&pressureEventBMP280);
  Serial.print("tempBMP280: "); Serial.print(temperatureEventBMP280.temperature); Serial.println((IS_METRIC ? "°C" : "°F"));
  Serial.print("pressureBMP280: "); Serial.print(pressureEventBMP280.pressure); Serial.println(" hPa ");
}

void updateThinkSpeak(){  
  // Upload all the measured values to thinkspeak
   
  if (strlen(upload_host) > 0 ) {  // empty upload_host indicates request to skip upload
    Serial.print("Connecting to "); Serial.println(upload_host);
  
    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    const int httpPort = 80;
    if (!client.connect(upload_host, httpPort)) {
      Serial.println("Connection failed!");
      return;
    }
  
    // URI for the request
    String url = "/update?api_key=";
    url += THINGSPEAK_API_KEY;
    url += "&field1=";
    url += String(temperatureDHT11);
    url += "&field2=";
    url += String(humidityDHT11);
    url += "&field3=";
    url += String(luxBH1750);
    url += "&field4=";
    url += String(temperatureEventBMP280.temperature);
    url += "&field5=";
    url += String(pressureEventBMP280.pressure);
    
    Serial.print("Requesting URL: "); Serial.println(url);
    
    // This will send the request to the server
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + upload_host + "\r\n" + 
                 "Connection: close\r\n\r\n");
    delay(10);
    while(!client.available()){
      delay(100);
      Serial.print(".");
    }
    // Read all the lines of the reply from server and print them to Serial
    while(client.available()){
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }   
    Serial.println(); Serial.println("closing connection");  
  }
}

void updateData(OLEDDisplay *display) {
  // Get all data from web services (current time, current weather, forecast weather.)

  drawProgress(display, 10, "Updating time...");
  drawProgress(display, 30, "Updating weather...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
  drawProgress(display, 50, "Updating forecasts...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
}

//*********************************************************
//***************** DRAW
//*********************************************************
// Draw frames and other things on the display

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawMeasured(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // Draw all the measured values to a single frame
  Serial.println("Temperature DHT11: " + String(temperatureDHT11) + (IS_METRIC ? "°C" : "°F"));
  Serial.println("Light: " + String(luxBH1750, 1) + " lx");
  Serial.println("Humidity: " + String(humidityDHT11, 1) +"%");
  Serial.println(F("Temperature BMP280 = ") + String(temperatureEventBMP280.temperature) + "°C"); 
  Serial.println(F("Pressure  BMP280 = ") + String(pressureEventBMP280.pressure) + " hPa"); 

  // Smaller font
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  // The first column
  display->drawString(x, 5 + y, String(temperatureDHT11) + (IS_METRIC ? "°C" : "°F"));
  display->drawString(x, 20 + y, String(humidityDHT11) +"%");
  display->drawString(x, 35 + y, String(luxBH1750, 1) +"lx");
  // The second column
  display->drawString(x + 64, 5 + y, String(temperatureEventBMP280.temperature) + (IS_METRIC ? "°C" : "°F"));
  display->drawString(x + 64, 20 + y, String(pressureEventBMP280.pressure) +"hPa");
  //display->drawString(x + 64, 35 + y, String(0) +"--");
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
}

//*********************************************************
//***************** SETUP
//*********************************************************     
void setup() {  
  Serial.begin(115200);
  delay(10);

  // Initialize dispaly
  Serial.println(); Serial.println(); Serial.println("Init display. ");
  display.init();
  display.clear();
  display.display();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  Serial.println("Init display completed.");

  // Connect to Wifi
  Serial.println();
  Serial.print("Connecting to: ");
  Serial.println(ssid); 
  WiFi.begin(ssid, password);  
  connectedWifi = false;
  int wifi_connect_counter = 0;
  while ((WiFi.status() != WL_CONNECTED) && (wifi_connect_counter < MAX_WIFI_CONNECT)) {
    delay(500);
    Serial.print(".");
    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, wifi_connect_counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, wifi_connect_counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, wifi_connect_counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();
    wifi_connect_counter++;
  }
  if (wifi_connect_counter < MAX_WIFI_CONNECT){
    Serial.println(""); Serial.println("WiFi CONNECTED"); Serial.println("IP address: "); Serial.println(WiFi.localIP());
    display.clear();
    display.drawString(64, 10, "WiFi connected");
    display.drawString(64, 20, WiFi.localIP().toString());
    display.display();   
    connectedWifi = true;
  }else{
    Serial.println(""); Serial.println("NOT CONNECTED"); 
    display.clear();
    display.drawString(64, 10, "WiFi NOT CONNECTED");
    display.display();
    connectedWifi = false; 
  }
  delay(500);

  // Setup frame based ui
  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  ui.setIndicatorPosition(BOTTOM); // can be TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT); // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrames(frames, numberOfFrames);
  ui.setOverlays(overlays, numberOfOverlays);
  if (connectedWifi){
    ui.enableAutoTransition();
    ui.setTimePerFrame(TIME_PER_FRAME);
    ui.switchToFrame(FRAME_MEASURE);      // frame with measurements
    configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  }else{ // in disconnected mode we display only measured values
    ui.disableAutoTransition();  // no other frames amart of measurements make sense 
    ui.switchToFrame(FRAME_MEASURE);     // frame with measurements
  }
  ui.init();
  updateData(&display); // Get time, current weather and weather forecast from open weather map
  updateDHT11();        // Make first measurements
  updateBH1750();
  //updateBMP280();

  timeSinceLastWUpdate= millis();
  timeSinceMeasured  = millis();
  timeSinceUpdateThinkSpeak  = millis();
}

//*********************************************************
//***************** LOOP
//*********************************************************

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void loop() {   
  if (millis() - timeSinceMeasured > (1000L * UPDATE_MEASURE)) { // Time measured since last weather information download from a service (Open weather map)
    Serial.println("****** Measuring");
    updateDHT11();
    updateBH1750();
    //updateBMP280();
    timeSinceMeasured  = millis();
  }

  if (millis() - timeSinceUpdateThinkSpeak > (1000L * UPLOAD_MEASURE)) { // Time since last weather information download from a service (Open weather map)
    Serial.println("****** updateThinkSpeak");
    updateThinkSpeak();
    timeSinceUpdateThinkSpeak  = millis();
  }
  
  if (millis() - timeSinceLastWUpdate > (1000L * UPDATE_INTERVAL_SECS)) {
    Serial.println("****** setReadyForWeatherUpdate");
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    Serial.println("****** updateData");
    updateData(&display); // Update data from internet - time, current and forecased weather 
  }

  int remainingTimeBudget = ui.update();
  if (remainingTimeBudget > 0) {
    // You can do some work here    
    //Serial.println("remainingTimeBudget="+String(remainingTimeBudget));
    delay(remainingTimeBudget);
  }
}
