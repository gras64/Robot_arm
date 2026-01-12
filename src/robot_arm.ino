#include <Arduino.h>
#include <mcp_can.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
#    include <micro_ros_platform.h>
#  else
#    pragma message ("micro_ros_platform.h not found; micro-ROS features disabled")
#  endif
#else
/* Compiler doesn't support __has_include; try to include and allow failure */
#  include <micro_ros_platform.h>
#endif
#include "motor_control.h"

// CAN Bus Konfiguration
#define CAN_CS_PIN 10
#define CAN_INT_PIN 2
MCP_CAN can(CAN_CS_PIN);

// Motorsteuerung für MKS SERVO42D
#define MOTOR_COUNT 6
#define MOTOR_PIN_1 25
#define MOTOR_PIN_2 26
#define MOTOR_PIN_3 27
#define MOTOR_PIN_4 14
#define MOTOR_PIN_5 12
#define MOTOR_PIN_6 13

// CAN-Bus IDs für die Motoren
#define MOTOR_1_ID 0x100
#define MOTOR_2_ID 0x101
#define MOTOR_3_ID 0x102
#define MOTOR_4_ID 0x103
#define MOTOR_5_ID 0x104
#define MOTOR_6_ID 0x105

// Globale Variablen
MotorControl* motors[MOTOR_COUNT];
int16_t motor_positions[MOTOR_COUNT] = {0};
int16_t motor_speeds[MOTOR_COUNT] = {0};

// CAN debug flags
volatile bool canIntFlag = false;
bool canDebug = false;
bool loopbackMode = false;

void IRAM_ATTR onCanInt() { canIntFlag = true; }

// Debug helpers
void printCANFrame(uint32_t id, uint8_t *buf, uint8_t len) {
  Serial.print("CAN id=0x"); Serial.print(id, HEX);
  Serial.print(" len="); Serial.print(len);
  Serial.print(" data=");
  for (uint8_t i=0;i<len;i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

void printCanStats() {
  Serial.print("CAN err: "); Serial.println(can.getError());
  Serial.print("RX err count: "); Serial.println(can.errorCountRX());
  Serial.print("TX err count: "); Serial.println(can.errorCountTX());
}

// --- MQTT / WiFi configuration (set your credentials) ---
const char* WIFI_SSID = "Hochhaus";
const char* WIFI_PASS = "wiebeihempelszuhauseunterdemsofa";
const char* MQTT_SERVER = "192.168.0.77"; // Home Assistant / Broker IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC_CMD = "robot_arm/command"; // accepts JSON or G-code

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Forward declarations
void mqttCallback(char* topic, byte* payload, unsigned int length);
void ensureMqttConnected();

void setup() {
  // Starten der seriellen Kommunikation
  Serial.begin(115200);
  
  // CAN Bus initialisieren (MCP2515 API: idMode, speed, clock)
  if (can.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) {
    Serial.println("CAN Bus initialized successfully");
    can.setMode(MCP_NORMAL);
  } else {
    Serial.println("Error initializing CAN Bus");
    while(1);
  }
  
  // CAN Bus Interrupt setzen
  pinMode(CAN_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CAN_INT_PIN), onCanInt, FALLING);
  
  // Motor-Instanzen erstellen
  motors[0] = new MotorControl(MOTOR_PIN_1);
  motors[1] = new MotorControl(MOTOR_PIN_2);
  motors[2] = new MotorControl(MOTOR_PIN_3);
  motors[3] = new MotorControl(MOTOR_PIN_4);
  motors[4] = new MotorControl(MOTOR_PIN_5);
  motors[5] = new MotorControl(MOTOR_PIN_6);
  
  // Micro-ROS initialisieren
  // Micro-ROS initialisieren (nur wenn Header vorhanden)
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
    set_microros_transports();
#  endif
#endif
}

void loop() {
  ensureMqttConnected();
  mqttClient.loop();
  // CAN Nachrichten empfangen
  receiveCANMessages();

  // if debug and interrupt flagged, print pending frames
  if (canDebug && canIntFlag) {
    canIntFlag = false;
    uint8_t len = 0; uint8_t buf[8]; uint32_t id;
    while (can.checkReceive() == CAN_MSGAVAIL) {
      can.readMsgBuf(&id, &len, buf);
      printCANFrame(id, buf, len);
    }
    printCanStats();
  }

  // UART -> CAN: prüfe serielle Eingaben und leite als CAN-Befehl weiter
  parseSerialCommand();

  // Motorsteuerung basierend auf CAN-Nachrichten
  controlMotors();

  // Micro-ROS Tick
  delay(10);
}

// --- MQTT helper functions ---
void ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) delay(100);
  }
  if (!mqttClient.connected()) {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    if (mqttClient.connect("robot_arm_client")) {
      mqttClient.subscribe(MQTT_TOPIC_CMD);
      Serial.println("MQTT connected");
    }
  }
}

// MQTT callback: accept JSON {"cmd":"move","motor":3,"pos":500,"speed":200}
// or plain G-code line as text
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.print("MQTT msg: "); Serial.println(msg);
  // try parse JSON
  if (msg.startsWith("{")) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (!err) {
      const char* cmd = doc["cmd"] | "";
      if (strcmp(cmd, "move") == 0) {
        int motor = doc["motor"] | -1;
        int pos = doc["pos"] | 0;
        int speed = doc["speed"] | 0;
        if (motor >= 1 && motor <= MOTOR_COUNT) {
          sendCANMotorCommand((uint8_t)(motor-1), (int16_t)pos, (int16_t)speed);
          mqttClient.publish("robot_arm/ack", "ok");
        } else {
          mqttClient.publish("robot_arm/ack", "err_invalid_motor");
        }
      }
    } else {
      mqttClient.publish("robot_arm/ack", "err_json");
    }
  } else {
    // treat as G-code line, reuse existing serial parser logic partially
    if (msg.startsWith("G1") || msg.startsWith("G0")) {
      // parse axis tokens A-F and F as speed
      int positions[MOTOR_COUNT]; for (int i=0;i<MOTOR_COUNT;i++) positions[i]=INT16_MIN;
      int speed = 0;
      // simple parser
      char buf[128];
      msg.toCharArray(buf, sizeof(buf));
      char *tok = strtok(buf, " \t");
      while (tok) {
        if (tok[0]=='A' || tok[0]=='B' || tok[0]=='C' || tok[0]=='D' || tok[0]=='E' || tok[0]=='F') {
          int idx = tok[0]-'A';
          int val = atoi(tok+1);
          if (idx >=0 && idx < MOTOR_COUNT) positions[idx] = val;
        } else if (tok[0]=='F') {
          speed = atoi(tok+1);
        }
        tok = strtok(NULL, " \t");
      }
      for (int i=0;i<MOTOR_COUNT;i++) {
        if (positions[i] != INT16_MIN) sendCANMotorCommand(i, (int16_t)positions[i], (int16_t)speed);
      }
      mqttClient.publish("robot_arm/ack", "ok");
    } else {
      mqttClient.publish("robot_arm/ack", "err_unknown_cmd");
    }
  }
}

void receiveCANMessages() {
  uint8_t len = 0;
  uint8_t buf[8];
  uint32_t canId;
  
  // Prüfen, ob neue CAN Nachrichten vorhanden sind
  if (can.checkReceive() == CAN_MSGAVAIL) {
    // readMsgBuf fills id, len and buf
    can.readMsgBuf(&canId, &len, buf);
    if (canDebug) printCANFrame(canId, buf, len);
    
    // Nachrichten basierend auf der CAN ID verarbeiten
    switch(canId) {
      case MOTOR_1_ID:
        processMotorCommand(0, buf, len);
        break;
      case MOTOR_2_ID:
        processMotorCommand(1, buf, len);
        break;
      case MOTOR_3_ID:
        processMotorCommand(2, buf, len);
        break;
      case MOTOR_4_ID:
        processMotorCommand(3, buf, len);
        break;
      case MOTOR_5_ID:
        processMotorCommand(4, buf, len);
        break;
      case MOTOR_6_ID:
        processMotorCommand(5, buf, len);
        break;
      default:
        Serial.println("Unknown CAN ID received");
        break;
    }
  }
}

void processMotorCommand(uint8_t motor_id, uint8_t* buf, uint8_t len) {
  if (len >= 4) {
    // Position und Geschwindigkeit aus der Nachricht extrahieren
    motor_positions[motor_id] = (int16_t)((buf[0] << 8) | buf[1]);
    motor_speeds[motor_id] = (int16_t)((buf[2] << 8) | buf[3]);
    
    Serial.print("Motor ");
    Serial.print(motor_id);
    Serial.print(" - Position: ");
    Serial.print(motor_positions[motor_id]);
    Serial.print(", Speed: ");
    Serial.println(motor_speeds[motor_id]);
  }
}

void controlMotors() {
  // Motorsteuerung basierend auf den empfangenen CAN-Nachrichten
  for (int i = 0; i < MOTOR_COUNT; i++) {
    if (motors[i] != nullptr) {
      motors[i]->setAngle(motor_positions[i]);
    }
  }
}

// Hilfsfunktion: sende Motorbefehl auf CAN (ID basiert auf MOTOR_*_ID)
void sendCANMotorCommand(uint8_t motor_index, int16_t position, int16_t speed) {
  if (motor_index >= MOTOR_COUNT) return;
  uint32_t id = MOTOR_1_ID + motor_index; // 0x100..0x105
  uint8_t buf[4];
  buf[0] = (uint8_t)((position >> 8) & 0xFF);
  buf[1] = (uint8_t)(position & 0xFF);
  buf[2] = (uint8_t)((speed >> 8) & 0xFF);
  buf[3] = (uint8_t)(speed & 0xFF);
  // sendMsgBuf(id, len, buf) - use 3-arg variant
  can.sendMsgBuf(id, 0, 4, buf);
}

// Einfacher ASCII-Parser: erwartetes Format: "SET <id> <pos> <speed>\n"
void parseSerialCommand() {
  static String line = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      // Prozessiere Zeile
      line.trim();
      if (line.length() > 0) {
        // Tokenize
        int idx = -1; int pos = 0; int speed = 0;
        // Debug commands: DBG CAN ON/OFF, DBG LOOPBACK ON/OFF, DBG STATS
        if (line.startsWith("DBG ")) {
          if (line.indexOf("CAN ON") >= 0) { canDebug = true; Serial.println("DBG CAN ON"); }
          else if (line.indexOf("CAN OFF") >= 0) { canDebug = false; Serial.println("DBG CAN OFF"); }
          else if (line.indexOf("LOOPBACK ON") >= 0) { loopbackMode = true; can.setMode(MCP_LOOPBACK); Serial.println("DBG LOOPBACK ON"); }
          else if (line.indexOf("LOOPBACK OFF") >= 0) { loopbackMode = false; can.setMode(MCP_NORMAL); Serial.println("DBG LOOPBACK OFF"); }
          else if (line.indexOf("STATS") >= 0) { printCanStats(); }
        }
        // unterstütze "SET <motor_index> <pos> <speed>"
        else if (line.startsWith("SET ")) {
          // parse
          // Teile anhand von Leerzeichen
          char buf[64];
          line.toCharArray(buf, sizeof(buf));
          char *tok = strtok(buf, " \t"); // "SET"
          tok = strtok(NULL, " \t");
          if (tok) idx = atoi(tok);
          tok = strtok(NULL, " \t");
          if (tok) pos = atoi(tok);
          tok = strtok(NULL, " \t");
          if (tok) speed = atoi(tok);
          if (idx >= 1 && idx <= MOTOR_COUNT) {
            sendCANMotorCommand((uint8_t)(idx-1), (int16_t)pos, (int16_t)speed);
            Serial.print("OK SET "); Serial.println(idx);
          } else {
            Serial.println("ERR INVALID_ID");
          }
        } else {
          Serial.println("ERR UNKNOWN_CMD");
        }
      }
      line = "";
    } else {
      line += c;
      if (line.length() > 120) line = ""; // safety
    }
  }
}