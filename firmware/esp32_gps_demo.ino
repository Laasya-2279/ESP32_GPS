/*
 * ================================================================
 *  GreenNote GPS Demo - ESP32 Junction Alert
 *  
 *  Polls backend for live GPS coordinates and triggers buzzer
 *  when user reaches threshold distance from junction.
 * ================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ================================================================
//  CONFIG - Edit these values before flashing
// ================================================================
const char* WIFI_SSID   = "YOUR_WIFI_NAME";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char* BACKEND_URL = "https://your-backend.railway.app";  // Your deployed backend URL

// Junction coordinates (where ESP32 is "installed")
const float JUNCTION_LAT = 9.9954740f;   // Kaloor Junction
const float JUNCTION_LNG = 76.3014861f;

// Threshold distance (in meters)
const float THRESHOLD_METERS = 100.0f;   // Trigger at 100m
const float HYSTERESIS = 20.0f;          // Turn off at 120m
// ================================================================

// Hardware pins
const int PIN_BUZZER = 25;
const int PIN_LED    = 26;

// Timing
const unsigned long POLL_INTERVAL = 1000;   // Poll every 1 second
const int           MAX_FAILURES  = 10;

// Runtime state
bool          alertOn       = false;
int           failureCount  = 0;
unsigned long lastPoll      = 0;


// ================================================================
//  Haversine Distance Calculator (returns metres)
// ================================================================
float haversineMetres(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371000.0f;  // Earth radius in metres
    float dLat = (lat2 - lat1) * DEG_TO_RAD;
    float dLon = (lon2 - lon1) * DEG_TO_RAD;
    float a = sinf(dLat / 2.0f) * sinf(dLat / 2.0f)
            + cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD)
            * sinf(dLon / 2.0f) * sinf(dLon / 2.0f);
    return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}


// ================================================================
//  Alert Control (only writes GPIO when state changes)
// ================================================================
void setAlert(bool on) {
    if (on == alertOn) return;  // No change, skip
    alertOn = on;
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
    digitalWrite(PIN_LED,    on ? HIGH : LOW);
    if (on)  Serial.println("  >> ALERT ON  — user in range!");
    else     Serial.println("  >> ALERT OFF — user cleared zone.");
}


// ================================================================
//  WiFi Connect
// ================================================================
void connectWiFi() {
    Serial.printf("\nConnecting to WiFi: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi FAILED — restarting ESP32...");
        delay(1000);
        ESP.restart();
    }
}


// ================================================================
//  Boot Animation (3 short blinks = system ready)
// ================================================================
void bootBlink() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED,    HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
        delay(120);
        digitalWrite(PIN_LED,    LOW);
        digitalWrite(PIN_BUZZER, LOW);
        delay(120);
    }
}


// ================================================================
//  SETUP
// ================================================================
void setup() {
    Serial.begin(115200);
    delay(300);
    
    Serial.println("\n================================================");
    Serial.println("  GreenNote GPS Demo - ESP32");
    Serial.printf ("  Junction: %.6f, %.6f\n", JUNCTION_LAT, JUNCTION_LNG);
    Serial.printf ("  Threshold: %.0f meters\n", THRESHOLD_METERS);
    Serial.println("================================================");
    
    // Initialize pins
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_LED,    OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED,    LOW);
    
    connectWiFi();
    bootBlink();
    
    Serial.println("\nPolling started...\n");
}


// ================================================================
//  MAIN LOOP (runs every 1 second)
// ================================================================
void loop() {
    unsigned long now = millis();
    
    // Only poll every POLL_INTERVAL milliseconds
    if (now - lastPoll < POLL_INTERVAL) return;
    lastPoll = now;
    
    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost — reconnecting...");
        WiFi.reconnect();
        delay(2000);
        return;
    }
    
    // Build API URL
    char url[256];
    snprintf(url, sizeof(url), "%s/api/gps/latest", BACKEND_URL);
    
    // HTTP GET request
    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);  // 3 second timeout
    int httpCode = http.GET();
    
    if (httpCode != 200) {
        Serial.printf("HTTP %d  (fail %d/%d)\n", httpCode, ++failureCount, MAX_FAILURES);
        http.end();
        
        // Fail-safe: silence alert after too many failures
        if (failureCount >= MAX_FAILURES) {
            setAlert(false);
            Serial.println("Too many failures — alert silenced.");
        }
        return;
    }
    
    // Parse JSON response
    String body = http.getString();
    http.end();
    failureCount = 0;  // Reset failure counter on success
    
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);
    
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
    }
    
    // Extract user GPS position
    float userLat = doc["lat"] | 0.0f;
    float userLng = doc["lng"] | 0.0f;
    int   accuracy = doc["accuracy"] | 0;
    int   ageSeconds = doc["ageSeconds"] | 0;
    
    // Calculate distance from user to junction
    float distance = haversineMetres(userLat, userLng, JUNCTION_LAT, JUNCTION_LNG);
    
    // Log every poll
    Serial.printf("[%6lums]  dist = %6.1f m  |  acc = ±%d m  |  age = %d s\n",
                  now, distance, accuracy, ageSeconds);
    
    // ---------------------------------------------------------------
    //  HYSTERESIS LOGIC
    //    Turn ON  when distance <= THRESHOLD_METERS
    //    Turn OFF when distance >  THRESHOLD_METERS + HYSTERESIS
    //    Hold state in the band between them (prevents flickering)
    // ---------------------------------------------------------------
    if (distance <= THRESHOLD_METERS) {
        setAlert(true);
    } else if (distance > THRESHOLD_METERS + HYSTERESIS) {
        setAlert(false);
    }
    // In the hysteresis band: do nothing, hold current state
}
