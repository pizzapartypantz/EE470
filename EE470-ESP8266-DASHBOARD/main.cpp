
/*
 * -------------------------------------------------------
 * Project: EE470 ESP8266 IoT System
 * File: main.cpp
 * Version: 4.0
 *
 * Features:
 * - DHT11 temperature and humidity
 * - KY-018 analog light sensor
 * - KY-004 pushbutton: Node 1
 * - KY-017 tilt switch: Node 2
 * - WiFi connectivity and RSSI
 * - Seven timezone selections
 * - NTP clock synchronization
 * - Timestamp captured at activation
 * - HTTP transmission to Hostinger
 * - JSON response validation
 * - 30-second retransmission
 * - Original measurement preservation
 *
 * Required libraries:
 * ESP8266WiFi, ESP8266HTTPClient,
 * WiFiClientSecure, ArduinoJson, DHT
 * -------------------------------------------------------
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>

// ========================================
// NETWORK CONFIGURATION
// ========================================

const char* WIFI_SSID = "giants415";
const char* WIFI_PASSWORD = "down1961";

const char* SERVER_URL =
    "https://jasondowning.com/insert.php";

// ========================================
// HARDWARE CONFIGURATION
// ========================================

#define DHT_PIN D1
#define DHT_TYPE DHT11

#define LIGHT_PIN A0
#define BUTTON_PIN D6
#define TILT_PIN D7

DHT dht(DHT_PIN, DHT_TYPE);

// ========================================
// TIMING
// ========================================

const unsigned long DEBOUNCE_MS = 40;
const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long RETRY_INTERVAL = 30000;

unsigned long lastSensorRead = 0;
unsigned long lastWiFiAttempt = 0;

// ========================================
// TIMEZONE CONFIGURATION
// ========================================

struct TimeZoneOption {
    const char* name;
    const char* posixTZ;
};

const TimeZoneOption TIMEZONES[] = {
    {"Eastern",  "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"Central",  "CST6CDT,M3.2.0/2,M11.1.0/2"},
    {"Mountain", "MST7MDT,M3.2.0/2,M11.1.0/2"},
    {"Pacific",  "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"Alaska",   "AKST9AKDT,M3.2.0/2,M11.1.0/2"},
    {"Hawaii",   "HST10"},
    {"Atlantic", "AST4"}
};

int selectedZone = 3;

// ========================================
// SENSOR VARIABLES
// ========================================

float temperature = NAN;
float humidity = NAN;
int lightLevel = 0;

bool sensorReady = false;
bool wifiConnected = false;

// ========================================
// SWITCH DEBOUNCING
// ========================================

int lastRawButton = HIGH;
int stableButton = HIGH;
unsigned long lastButtonChange = 0;

int lastRawTilt = HIGH;
int stableTilt = HIGH;
unsigned long lastTiltChange = 0;

// ========================================
// MEASUREMENT QUEUE
// ========================================

struct Measurement {
    String node;
    String timestampUTC;
    String timestampLocal;
    String timezoneName;

    float temperature;
    float humidity;
    int light;

    unsigned long lastAttempt;
    unsigned int attempts;
};

const int QUEUE_CAPACITY = 10;

Measurement queueData[QUEUE_CAPACITY];
int queueCount = 0;

// ========================================
// TIMEZONE FUNCTIONS
// ========================================

void applyTimezone() {

    setenv(
        "TZ",
        TIMEZONES[selectedZone].posixTZ,
        1
    );

    tzset();
}

void showTimezoneMenu() {

    Serial.println();
    Serial.println("====== TIMEZONE SELECTION ======");

    for (int i = 0; i < 7; i++) {
        Serial.print(i + 1);
        Serial.print(". ");
        Serial.println(TIMEZONES[i].name);
    }

    Serial.println();
    Serial.println("Default: Pacific Time");
    Serial.println("Enter 1-7 to select.");
    Serial.println("Enter T to display menu.");

    Serial.print("Current: ");
    Serial.println(TIMEZONES[selectedZone].name);
}

void checkSerialInput() {

    while (Serial.available()) {

        char input = Serial.read();

        if (input == '\r' || input == '\n') {
            continue;
        }

        if (input == 't' || input == 'T') {
            showTimezoneMenu();
        }
        else if (input >= '1' && input <= '7') {

            selectedZone = input - '1';
            applyTimezone();

            Serial.print("Timezone selected: ");
            Serial.println(TIMEZONES[selectedZone].name);
        }
        else {
            Serial.println("Enter 1-7 or T.");
        }
    }
}

// ========================================
// WIFI
// ========================================

void connectWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    lastWiFiAttempt = millis();

    Serial.println("Connecting to WiFi...");
}

void printWiFiInfo() {

    Serial.println();
    Serial.println("====== WIFI INFORMATION ======");

    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.println("==============================");
}

void checkWiFi() {

    if (WiFi.status() == WL_CONNECTED) {

        if (!wifiConnected) {

            wifiConnected = true;

            Serial.println("WIFI CONNECTED!");
            printWiFiInfo();

            // Synchronize clock to UTC.
            
configTime(
    TIMEZONES[selectedZone].posixTZ,
    "pool.ntp.org",
    "time.google.com"
);

Serial.println("NTP synchronization requested.");



            Serial.println("NTP synchronization requested.");
        }

        return;
    }

    if (wifiConnected) {
        wifiConnected = false;
        Serial.println("WIFI DISCONNECTED!");
        lastWiFiAttempt = millis();
    }

    if (millis() - lastWiFiAttempt >=
        WIFI_RETRY_MS) {

        Serial.println("Retrying WiFi...");

        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        lastWiFiAttempt = millis();
    }
}

// ========================================
// TIME FUNCTIONS
// ========================================

bool clockIsReady() {

    time_t now = time(nullptr);

    // Reject uninitialized epoch timestamps.
    return now > 1735689600;
}

String formatTime(
    time_t timestamp,
    bool useLocal
) {

    struct tm timeInfo;

    if (useLocal) {
        localtime_r(&timestamp, &timeInfo);
    }
    else {
        gmtime_r(&timestamp, &timeInfo);
    }

    char buffer[24];

    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d %H:%M:%S",
        &timeInfo
    );

    return String(buffer);
}

// ========================================
// SENSOR READING
// ========================================

void readSensors() {

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    lightLevel = analogRead(LIGHT_PIN);

    if (isnan(t) || isnan(h)) {

        sensorReady = false;
        Serial.println("DHT11 reading failed!");
        return;
    }

    temperature = t;
    humidity = h;

    sensorReady = true;
}

void printMeasurement(const Measurement& m) {

    Serial.println();
    Serial.println("======= MEASUREMENT =======");

    Serial.print("Node: ");
    Serial.println(m.node);

    Serial.print("Local Time (");
    Serial.print(m.timezoneName);
    Serial.print("): ");
    Serial.println(m.timestampLocal);

    Serial.print("Database Time (UTC): ");
    Serial.println(m.timestampUTC);

    Serial.print("Temperature: ");
    Serial.print(m.temperature, 2);
    Serial.println(" C");

    Serial.print("Humidity: ");
    Serial.print(m.humidity, 2);
    Serial.println(" %");

    Serial.print("Light Level: ");
    Serial.print(m.light);
    Serial.println(" / 1023");

    Serial.println("===========================");
}

// ========================================
// URL ENCODING
// ========================================

// Encode spaces in the SQL timestamp.
// Other characters used by this timestamp
// format are safe in the request query.
String encodeTimestamp(const String& value) {

    String result = value;
    result.replace(" ", "%20");

    return result;
}

// ========================================
// CAPTURE NODE MEASUREMENT
// ========================================

void activateNode(int nodeNumber) {

    Serial.println();

    Serial.print("NODE ");
    Serial.print(nodeNumber);
    Serial.println(" ACTIVATED");

    if (!sensorReady) {
        Serial.println("Valid DHT11 data unavailable.");
        return;
    }

    if (!clockIsReady()) {
        Serial.println("Clock not synchronized yet.");
        return;
    }

    if (queueCount >= QUEUE_CAPACITY) {
        Serial.println("ERROR: Measurement queue full!");
        return;
    }

    // Capture event time before networking.
    time_t eventTime = time(nullptr);

    Measurement m;

    m.node = "node_" + String(nodeNumber);

    m.timestampUTC = formatTime(eventTime, false);
    m.timestampLocal = formatTime(eventTime, true);

    m.timezoneName = TIMEZONES[selectedZone].name;

    m.temperature = temperature;
    m.humidity = humidity;

    // Fresh analog reading at activation.
    m.light = analogRead(LIGHT_PIN);

    m.attempts = 0;
    m.lastAttempt = millis() - RETRY_INTERVAL;

    // Preserve the captured values in RAM.
    queueData[queueCount] = m;
    queueCount++;

    Serial.println("Measurement queued.");
    printMeasurement(m);
}

// ========================================
// REMOVE ACKNOWLEDGED MEASUREMENT
// ========================================

void removeFirstMeasurement() {

    if (queueCount <= 0) {
        return;
    }

    for (int i = 1; i < queueCount; i++) {
        queueData[i - 1] = queueData[i];
    }

    queueCount--;
}

// ========================================
// HTTP TRANSMISSION
// ========================================

bool transmitMeasurement(const Measurement& m) {

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("HTTP: WiFi unavailable.");
        return false;
    }

    String url = String(SERVER_URL);

    url += "?nodeId=" + m.node;
    url += "&nodeTemp=" + String(m.temperature, 2);
    url += "&nodeHumidity=" + String(m.humidity, 2);
    url += "&nodeLight=" + String(m.light);
    url += "&timeReceived=" +
           encodeTimestamp(m.timestampUTC);

    BearSSL::WiFiClientSecure client;

    // Lab prototype only.
    // Proper TLS verification should replace this.
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setTimeout(5000);

    if (!http.begin(client, url)) {

        Serial.println("HTTP initialization failed.");
        return false;
    }

    int httpCode = http.GET();

    Serial.print("HTTP Status: ");
    Serial.println(httpCode);

    if (httpCode != HTTP_CODE_OK) {

        if (httpCode > 0) {
            Serial.println(http.getString());
        }

        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    Serial.print("Server Response: ");
    Serial.println(response);

    DynamicJsonDocument doc(512);

    DeserializationError error =
        deserializeJson(doc, response);

    if (error) {
        Serial.println("Invalid JSON response.");
        return false;
    }

    String status = doc["status"] | "";

    // Both statuses acknowledge delivery.
    if (status == "success" ||
        status == "duplicate") {

        return true;
    }

    return false;
}

// ========================================
// RETRANSMISSION HANDLER
// ========================================

void processQueue() {

    if (queueCount == 0) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    unsigned long now = millis();

    // Process the oldest queued measurement.
    // Retry no sooner than every 30 seconds.
    if (now - queueData[0].lastAttempt <
        RETRY_INTERVAL) {

        return;
    }

    // Freeze retry timing before HTTP request.
    queueData[0].lastAttempt = now;
    queueData[0].attempts++;

    Serial.println();
    Serial.println("====== HTTP TRANSMISSION ======");

    Serial.print("Attempt: ");
    Serial.println(queueData[0].attempts);

    Serial.print("Node: ");
    Serial.println(queueData[0].node);

    Serial.print("Original timestamp: ");
    Serial.println(queueData[0].timestampUTC);

    bool acknowledged =
        transmitMeasurement(queueData[0]);

    if (acknowledged) {

        Serial.println("DELIVERY ACKNOWLEDGED!");
        removeFirstMeasurement();
    }
    else {

        Serial.println("Delivery not acknowledged.");
        Serial.println("Retry scheduled in 30 seconds.");
    }

    Serial.println("===============================");
}

// ========================================
// PUSHBUTTON - NODE 1
// ========================================

void checkButton() {

    unsigned long now = millis();
    int raw = digitalRead(BUTTON_PIN);

    if (raw != lastRawButton) {
        lastRawButton = raw;
        lastButtonChange = now;
    }

    if (now - lastButtonChange >= DEBOUNCE_MS &&
        raw != stableButton) {

        stableButton = raw;

        if (stableButton == LOW) {
            activateNode(1);
        }
        else {
            Serial.println("BUTTON RELEASED");
        }
    }
}

// ========================================
// TILT SWITCH - NODE 2
// ========================================

void checkTilt() {

    unsigned long now = millis();
    int raw = digitalRead(TILT_PIN);

    if (raw != lastRawTilt) {
        lastRawTilt = raw;
        lastTiltChange = now;
    }

    if (now - lastTiltChange >= DEBOUNCE_MS &&
        raw != stableTilt) {

        stableTilt = raw;

        if (stableTilt == LOW) {
            activateNode(2);
        }
        else {
            Serial.println("TILT SWITCH RELEASED");
        }
    }
}

// ========================================
// SETUP
// ========================================

void setup() {

    Serial.begin(115200);

    dht.begin();

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(TILT_PIN, INPUT_PULLUP);

    lastRawButton = digitalRead(BUTTON_PIN);
    stableButton = lastRawButton;

    lastRawTilt = digitalRead(TILT_PIN);
    stableTilt = lastRawTilt;

    applyTimezone();

    Serial.println();
    Serial.println("==============================");
    Serial.println(" EE470 ESP8266 IoT System");
    Serial.println(" Version 4.0");
    Serial.println("==============================");

    Serial.println("DHT11 Initialized");
    Serial.println("Photoresistor Initialized");
    Serial.println("Pushbutton Initialized");
    Serial.println("Tilt Switch Initialized");

    showTimezoneMenu();
    connectWiFi();

    Serial.println("System Ready.");
}

// ========================================
// MAIN LOOP
// ========================================

void loop() {

    checkWiFi();
    checkSerialInput();

    unsigned long now = millis();

    if (now - lastSensorRead >= SENSOR_INTERVAL) {

        lastSensorRead = now;
        readSensors();
    }

    checkButton();
    checkTilt();

    processQueue();
}
