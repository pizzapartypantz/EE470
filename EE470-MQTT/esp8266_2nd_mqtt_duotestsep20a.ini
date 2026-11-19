
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ========================================
// EE470 - ESP8266 #2
// Two-Way MQTT Communication
// ========================================

#define BUTTON_PIN D5
#define LED_PIN D6

// WiFi
const char* ssid = "giants415";
const char* password = "down1961";

// MQTT
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;

// Receive button events from ESP8266 #1
const char* subscribeTopic =
  "testtopic/button/jasondowning";

// Send commands to ESP8266 #1 LED
const char* publishTopic =
  "testtopic/temp/inTopic/jasondowning";

WiFiClient espClient;
PubSubClient client(espClient);

// Button timing
bool lastReading = HIGH;
bool stableState = HIGH;
bool resetPending = false;

unsigned long debounceTime = 0;
unsigned long releaseTime = 0;
unsigned long lastReconnectAttempt = 0;

const unsigned long debounceDelay = 50;
const unsigned long resetDelay = 5000;

// ========================================
// Receive messages from ESP8266 #1
// ========================================
void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Received from ESP #1: ");
  Serial.println(message);

  if (String(topic) == subscribeTopic) {

    if (message == "1") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("ESP #2 LED ON");
    }

    else if (message == "0") {
      digitalWrite(LED_PIN, LOW);
      Serial.println("ESP #2 LED OFF");
    }
  }
}

// ========================================
// Connect to WiFi
// ========================================
void connectWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// ========================================
// Connect to MQTT
// ========================================
void reconnectMQTT() {

  if (client.connected()) return;

  unsigned long now = millis();

  if (lastReconnectAttempt != 0 &&
      now - lastReconnectAttempt < 5000) {
    return;
  }

  lastReconnectAttempt = now;

  String clientId = "EE470-ESP2-";
  clientId += String(ESP.getChipId(), HEX);

  Serial.print("Connecting to MQTT...");

  if (client.connect(clientId.c_str())) {

    Serial.println("Connected!");

    client.subscribe(subscribeTopic);

    Serial.println("Subscribed to ESP #1");

  } else {

    Serial.print("Failed, state=");
    Serial.println(client.state());
  }
}

// ========================================
// Button: Send commands to ESP8266 #1
// ========================================
void checkButton() {

  bool reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (reading != lastReading) {
    debounceTime = now;
  }

  if (now - debounceTime >= debounceDelay) {

    if (reading != stableState) {

      stableState = reading;

      if (stableState == LOW) {

        if (client.connected()) {
          client.publish(publishTopic, "1");
          Serial.println("Sent 1 to ESP #1");
        }

        resetPending = false;

      } else {

        releaseTime = now;
        resetPending = true;

        Serial.println("Button released");
      }
    }
  }

  if (resetPending &&
      now - releaseTime >= resetDelay) {

    if (client.connected()) {
      client.publish(publishTopic, "0");
      Serial.println("Sent 0 to ESP #1");
    }

    resetPending = false;
  }

  lastReading = reading;
}

// ========================================
// Setup
// ========================================
void setup() {

  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  connectWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
}

// ========================================
// Main Loop
// ========================================
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiAttempt = 0;
    if (millis() - lastWiFiAttempt >= 5000) {
      lastWiFiAttempt = millis();
      WiFi.reconnect();
    }
  } else {
    if (!client.connected()) {
      reconnectMQTT();
    }

    if (client.connected()) {
      client.loop();
    }
  }

  checkButton();
}
