/*Pikud Haoref Alarm lamp
Created by Ofek Golan for Amit education 
USE AT YOUR OWN RISK!
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include <WebServer.h>

// ==========================================
// DYNAMIC CONFIGURATION VARIABLES
// ==========================================
Preferences preferences;
String citiesCsv = ""; // Default if nothing is saved in flash yet
const int MAX_CITIES = 10;             
String targetCitiesArray[MAX_CITIES];  
int cityCount = 0;                     

// ==========================================
// HARDWARE & API CONFIGURATION
// ==========================================
const char* OREF_URL = "https://www.oref.org.il/WarningMessages/alert/alerts.json";

const int ALERT_PIN = 2; // internal_LED
#define PIN 4            // LED STRIP Pin
#define NUMPIXELS 11     // Popular NeoPixel ring size
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastCheckTime = 0;
const unsigned long POLLING_INTERVAL = 2000;  // Check every 2 seconds

// State tracker to hold the LED color until "Event Ended" arrives
bool alertActive = false; 

// Web Server on port 80
WebServer server(80);

// ==========================================
// HELPER: SPLIT COMMA-SEPARATED STRING
// ==========================================
int splitCities(String input, String* outputArray, int maxItems) {
  int itemCount = 0;
  int startIndex = 0;
  int commaIndex = input.indexOf(',');

  while (commaIndex != -1) {
    if (itemCount >= maxItems) break;
    String city = input.substring(startIndex, commaIndex);
    city.trim(); 
    if (city.length() > 0) outputArray[itemCount++] = city;
    startIndex = commaIndex + 1;
    commaIndex = input.indexOf(',', startIndex);
  }
  
  if (itemCount < maxItems && startIndex < input.length()) {
    String city = input.substring(startIndex);
    city.trim();
    if (city.length() > 0) outputArray[itemCount++] = city;
  }
  return itemCount; 
}

// ==========================================
// WEB SERVER PAGE HANDLER
// ==========================================
void handleRoot() {
  // Check if the user submitted new cities via the web page
  if (server.hasArg("new_cities")) {
    String newCities = server.arg("new_cities");
    if (newCities != citiesCsv) {
      citiesCsv = newCities;
      // Save to flash
      preferences.putString("cities", citiesCsv);
      // Rebuild the array dynamically
      cityCount = splitCities(citiesCsv, targetCitiesArray, MAX_CITIES);
      Serial.println("Cities updated via Web Server: " + citiesCsv);
    }
  }

  // Build the HTML page
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Oref Alert Settings</title>";
  html += "<style>body{font-family: Arial, sans-serif; margin: 20px; text-align: right; direction: rtl;} ";
  html += "input[type='text'] {width: 80%; padding: 10px; font-size: 16px;} ";
  html += "input[type='submit'] {padding: 10px 20px; font-size: 16px; background-color: #28a745; color: white; border: none; border-radius: 5px; cursor: pointer;}</style>";
  html += "</head><body>";
  html += "<h2>הגדרות צבע אדום (Oref Alert)</h2>";
  html += "<h3>ערים במעקב:</h3><ul>";
  
  for (int i = 0; i < cityCount; i++) {
    html += "<li>" + targetCitiesArray[i] + "</li>";
  }
  html += "</ul>";
  
  html += "<h3>עדכון ערים (מופרד בפסיקים):</h3>";
  html += "<form method='POST' action='/'>";
  html += "<input type='text' name='new_cities' value='" + citiesCsv + "'><br><br>";
  html += "<input type='submit' value='שמור ועדכן'>";
  html += "</form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize LEDs and Pins
  pinMode(ALERT_PIN, OUTPUT);
  digitalWrite(ALERT_PIN, LOW);
  pixels.begin();
  
  // BROWNOUT FIX: Limit brightness to ~20% to prevent power spikes
  pixels.setBrightness(50); 
  pixels.clear();
  pixels.show(); // Leave LEDs OFF during Wi-Fi connection phase

  // 1. Load saved cities from flash memory
  preferences.begin("app-data", false);
  citiesCsv = preferences.getString("cities", citiesCsv);
  Serial.println("\nLoaded Cities from Flash: " + citiesCsv);

  // 2. Setup WiFiManager
  WiFiManager wm;
  
  // Optional: Turn LEDs BLUE only if it fails to connect and enters Setup mode
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 150));
    }
    pixels.show();
  });

  WiFiManagerParameter custom_cities("cities", "Target Cities (comma separated)", citiesCsv.c_str(), 150);
  wm.addParameter(&custom_cities);

  Serial.println("Connecting to Wi-Fi...");

  // 3. Connect or create Access Point "ESP32_Alert_Config"
  bool res = wm.autoConnect("Alamp_AP");

  if (!res) {
    Serial.println("Failed to connect to Wi-Fi.");
    // ESP.restart();
  } else {
    Serial.println("Connected to Wi-Fi successfully!");
    
    // 4. Save new cities if the user updated them in the WiFiManager portal
    String newCities = String(custom_cities.getValue());
    if (newCities != citiesCsv) {
      citiesCsv = newCities;
      preferences.putString("cities", citiesCsv);
      Serial.println("New cities saved to flash: " + citiesCsv);
    }

    // 5. Start mDNS
    if (!MDNS.begin("alamp")) { 
      Serial.println("Error setting up mDNS responder!");
    } else {
      Serial.println("mDNS responder started! Go to: http://alamp.local");
    }

    // 6. Start Web Server
    server.on("/", handleRoot);
    server.begin();
    Serial.println("Web server started.");
  }

  // 7. Populate the active city array
  cityCount = splitCities(citiesCsv, targetCitiesArray, MAX_CITIES);

  Serial.println("--- Monitoring Cities ---");
  for (int i = 0; i < cityCount; i++) {
    Serial.println(targetCitiesArray[i]);
  }

  // 8. Set LEDs to GREEN indicating normal operation is starting
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 150, 0));
    pixels.show();
    delay(10);
  }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // Handle Web Server client requests
  server.handleClient();

  // Non-blocking delay for HTTP polling
  if (millis() - lastCheckTime >= POLLING_INTERVAL) {
    lastCheckTime = millis();
    checkAlerts();
  }
}

// ==========================================
// ALERT CHECKING LOGIC
// ==========================================
void checkAlerts() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(OREF_URL);

    // Expanded User-Agent to bypass Akamai
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    http.addHeader("Referer", "https://www.oref.org.il/");
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();

      // Strip UTF-8 BOM if present
      if (payload.startsWith("\xEF\xBB\xBF")) {
        payload.remove(0, 3);
      }
      payload.trim();

      if (payload.length() <= 2) {
        if (alertActive) {
          alertActive = false; 
          for (int i = 0; i < NUMPIXELS; i++) {
            pixels.setPixelColor(i, pixels.Color(0, 255, 0));
            pixels.show();
            delay(10);
          }
          digitalWrite(ALERT_PIN, LOW);
          Serial.println("Checked API: All clear (File Empty). Returning to Green.");
        } else {
          Serial.println("Checked API: All clear.");
          digitalWrite(ALERT_PIN, LOW);
          for (int i = 0; i < NUMPIXELS; i++) {
            pixels.setPixelColor(i, pixels.Color(0, 255, 0));
            pixels.show();
            delay(10);
          }
        }
      } else {

        if (payload.indexOf("<html") >= 0 || payload.indexOf("<HTML") >= 0) {
          Serial.println("Error: Received HTML instead of JSON. Blocked by Akamai.");
          http.end();
          return; 
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          bool isRelevantAlert = false;
          bool isEventEndedForUs = false;
          String alertCategory = "";
          
          String category = doc["cat"].as<String>();
          String title = doc["title"].as<String>();
          JsonArray cities = doc["data"].as<JsonArray>();

          for (JsonVariant city : cities) {
            String currentCity = city.as<String>();
            
            for (int i = 0; i < cityCount; i++) {
              if (targetCitiesArray[i] == "" || currentCity == targetCitiesArray[i]) {
                if (title == "האירוע הסתיים") {
                  isEventEndedForUs = true;
                } else {
                  isRelevantAlert = true;
                  alertCategory = category;
                }
                break;
              }
            }
            if (isRelevantAlert || isEventEndedForUs) break; 
          }

          if (isRelevantAlert) {
            alertActive = true; 
            
            if (alertCategory == "1" || alertCategory == "2" || alertCategory == "6") {
              if (alertCategory == "1"){
                for (int i = 0; i < NUMPIXELS; i++) {
                  pixels.setPixelColor(i, pixels.Color(255, 0, 0));
                  pixels.show();
                  delay(10);
                }
                digitalWrite(ALERT_PIN, HIGH);
                Serial.println("RED ALERT! Missile!");
              } else {
                for (int i = 0; i < NUMPIXELS; i++) {
                  pixels.setPixelColor(i, pixels.Color(255, 0, 200));
                  pixels.show();
                  delay(10);
                }
                digitalWrite(ALERT_PIN, HIGH);
                Serial.println("RED ALERT! UAV!");
              }

            } else if (alertCategory == "10") {
              for (int i = 0; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, pixels.Color(255, 100, 0));
                pixels.show();
                delay(10);
              }
              digitalWrite(ALERT_PIN, HIGH);
              Serial.println("YELLOW ALERT! Pre-Alert active.");
            } 
          } 
          else if (isEventEndedForUs) {
            alertActive = false;
            
            for (int i = 0; i < NUMPIXELS; i++) {
              pixels.setPixelColor(i, pixels.Color(0, 255, 0));
              pixels.show();
              delay(10);
            }
            digitalWrite(ALERT_PIN, LOW);
            Serial.println("Notice: Received 'Event Ended' for target city. Returning to Green.");
          } 
          else {
            if (!alertActive) {
              for (int i = 0; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, pixels.Color(0, 255, 0));
                pixels.show();
                delay(10);
              }
              digitalWrite(ALERT_PIN, LOW);
            } else {
              Serial.println("Other cities alerting. Holding Red/Yellow until Event Ended arrives for us.");
            }
          }
        } else {
          Serial.print("JSON Parse Failed: ");
          pixels.clear();
          delay(50);
          for (int i = 0; i < 5; i++) {
            pixels.setPixelColor(i, pixels.Color(255, 0, 0));
            pixels.show();
            delay(10);
          }
          Serial.println(error.c_str());
        }
      }
    } else {
      Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
      pixels.clear();
      delay(50);
      for (int i = 0; i < 3; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 0));
        pixels.show();
        delay(10);
      }
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected!");
    pixels.clear();
    delay(50);
    for (int i = 0; i < 1; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));
      pixels.show();
      delay(10);
    }
  }
}