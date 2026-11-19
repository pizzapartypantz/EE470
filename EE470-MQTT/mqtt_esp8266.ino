
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ==========================================
// EE470 MQTT Assignment - Version 3.0
// ==========================================

// Hardware pins
#define POT_PIN A0
#define BUTTON_PIN D5
#define LED_PIN D6

// WiFi credentials
const char* ssid = "giants415";
const char* password = "down1961";

// MQTT broker
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;

// MQTT topics
const char* potTopic =
  "testtopic/temp/outTopic/jasondowning";

const char* ledCommandTopic =
  "testtopic/temp/inTopic/jasondowning";

const char* buttonTopic =
  "testtopic/button/jasondowning";

const char* ledStateTopic =
  "testtopic/led/state/jasondowning";

// Network clients
WiFiClient espClient;
PubSubClient client(espClient);

// Timing
unsigned long lastPotPublish = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastDebounceTime = 0;
unsigned long releaseTime = 0;

const unsigned long potInterval = 15000;
const unsigned long debounceDelay = 50;
const unsigned long buttonResetDelay = 5000;

// States
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
bool buttonResetPending = false;
bool ledState = false;

// ==========================================
// MQTT Callback: Receive LED commands
// ==========================================
void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("MQTT Received: ");
  Serial.println(message);

  if (String(topic) == ledCommandTopic) {

    if (message == "1") {
      ledState = true;
      digitalWrite(LED_PIN, HIGH);
    }
    else if (message == "0") {
      ledState = false;
      digitalWrite(LED_PIN, LOW);
    }
    else {
      Serial.println("Invalid LED command");
      return;
    }

    // Publish the actual LED state
    client.publish(
      ledStateTopic,
      ledState ? "1" : "0",
      true
    );

    Serial.print("LED State: ");
    Serial.println(ledState ? "ON" : "OFF");
  }
}

// ==========================================
// Connect to WiFi
// ==========================================
void connectWiFi() {

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// Reconnect to MQTT broker
// ==========================================
void reconnectMQTT() {

  if (client.connected()) return;

  unsigned long now = millis();

  if (lastMqttAttempt != 0 &&
      now - lastMqttAttempt < 5000) {
    return;
  }

  lastMqttAttempt = now;

  String clientId = "EE470-";
  clientId += String(ESP.getChipId(), HEX);

  Serial.print("Connecting to MQTT...");

  if (client.connect(clientId.c_str())) {

    Serial.println("Connected!");

    // Subscribe to remote LED commands
    client.subscribe(ledCommandTopic);

    Serial.println("Subscribed to LED commands");

    // Publish current LED state on reconnect
    client.publish(
      ledStateTopic,
      ledState ? "1" : "0",
      true
    );

  } else {
    Serial.print("MQTT failed, state=");
    Serial.println(client.state());
  }
}

// ==========================================
// Pushbutton handling
// ==========================================
void checkButton() {

  bool reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  // Debouncing
  if (reading != lastButtonReading) {
    lastDebounceTime = now;
  }

  if (now - lastDebounceTime >= debounceDelay) {

    if (reading != stableButtonState) {
      stableButtonState = reading;

      // Button pressed
      if (stableButtonState == LOW) {

        if (client.connected()) {
          client.publish(buttonTopic, "1");
        }

        Serial.println("Button PRESSED -> 1");
        buttonResetPending = false;

      }
      // Button released
      else {

        releaseTime = now;
        buttonResetPending = true;

        Serial.println("Button RELEASED");
      }
    }
  }

  // Send 0 five seconds after release
  if (buttonResetPending &&
      now - releaseTime >= buttonResetDelay) {

    if (client.connected()) {
      client.publish(buttonTopic, "0");
    }

    Serial.println("Button reset -> 0");
    buttonResetPending = false;
  }

  lastButtonReading = reading;
}

// ==========================================
// Publish potentiometer readings
// ==========================================
void publishPotentiometer() {

  unsigned long now = millis();

  if (now - lastPotPublish >= potInterval) {

    lastPotPublish = now;

    int potValue = analogRead(POT_PIN);

    char payload[12];
    snprintf(payload, sizeof(payload), "%d", potValue);

    if (client.connected()) {

      bool sent = client.publish(potTopic, payload);

      Serial.print("Potentiometer: ");
      Serial.print(payload);

      Serial.print(" | MQTT: ");
      Serial.println(sent ? "SUCCESS" : "FAILED");
    }
  }
}

// ==========================================
// Setup
// ==========================================
void setup() {

  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  // LED starts OFF
  digitalWrite(LED_PIN, LOW);
  ledState = false;

  connectWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
}

// ==========================================
// Main loop
// ==========================================
void loop() {

  if (WiFi.status() == WL_CONNECTED) {

    if (!client.connected()) {
      reconnectMQTT();
    }

    if (client.connected()) {
      client.loop();
    }
  }

  checkButton();
  publishPotentiometer();
}
