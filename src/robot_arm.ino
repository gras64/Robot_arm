#include <Arduino.h>
#include <mcp_can.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
#    include <micro_ros_platform.h>
#    include <rcl/rcl.h>
#    include <rclc/rclc.h>
#    include <rclc/executor.h>
#    include <std_msgs/msg/string.h>
#  else
#    pragma message ("micro_ros_platform.h not found; micro-ROS features disabled")
#  endif
#else
/* Compiler doesn't support __has_include; try to include and allow failure */
#  include <micro_ros_platform.h>
#endif
#include "motor_control.h"


// Optional: Polling der Winkelsensoren und Meldung bei Bewegung.
// Beispiel: Wenn Ihre Sensoren an analogen Pins A0..A5 hängen, können
// Sie die Lese- und Mapping-Logik hier ergänzen und `updateFromSensor` aufrufen.
void pollSensors() {
  // Example (commented):
  // int analogPins[MOTOR_COUNT] = {A0, A1, A2, A3, A4, A5};
  // for (int i = 0; i < MOTOR_COUNT; i++) {
  //   int raw = analogRead(analogPins[i]);
  //   int16_t angle = map(raw, 0, 4095, -1000, 1000); // adjust ADC range if needed
  //   if (motors[i]) motors[i]->updateFromSensor(angle);
  // }
}

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
// per-motor configuration stored locally
int16_t motor_cfg_min[MOTOR_COUNT];
int16_t motor_cfg_max[MOTOR_COUNT];
int16_t motor_cfg_maxspeed[MOTOR_COUNT];

// CAN debug flags
volatile bool canIntFlag = false;
bool canDebug = false;
bool loopbackMode = false;

void IRAM_ATTR onCanInt() { canIntFlag = true; }

#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
// micro-ROS executor handle (initialized in setup)
static rclc_executor_t robot_executor;
// forward declare callback
void rosConfigCallback(const void *msgin);
void rosParamSetCallback(const void *msgin);
void publishParams();
// global publisher for params
static rcl_publisher_t robot_params_pub;
static std_msgs__msg__String robot_params_msg;
#  endif
#  endif
#endif
#  endif
#endif

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
  // Enable change reporting for all motors (reports via Serial)
  for (int i = 0; i < MOTOR_COUNT; i++) {
    if (motors[i]) motors[i]->setReport(true);
  }
  // These motors are controlled via CAN (MKS SERVO42D). Disable direct GPIO/PWM outputs
  // to avoid conflicts. If you drive motors directly via PWM instead of CAN, enable per-motor.
  for (int i = 0; i < MOTOR_COUNT; i++) {
    if (motors[i]) motors[i]->setUsePWM(false);
  }

  // Send default configuration to each motor over CAN (min/max position, max speed)
  // Values can be adjusted below or exposed to a config file.
  const int16_t cfg_min_pos = MOTOR_MIN_POSITION;
  const int16_t cfg_max_pos = MOTOR_MAX_POSITION;
  const int16_t cfg_max_speed = MOTOR_MAX_SPEED;
  for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
    // per-motor overrides: use MOTOR_X_MIN/MAX if defined, otherwise global defaults
    switch (i) {
      case 0:
        motor_cfg_min[i] = MOTOR_1_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_1_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_1_MAX_SPEED;
        break;
      case 1:
        motor_cfg_min[i] = MOTOR_2_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_2_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_2_MAX_SPEED;
        break;
      case 2:
        motor_cfg_min[i] = MOTOR_3_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_3_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_3_MAX_SPEED;
        break;
      case 3:
        motor_cfg_min[i] = MOTOR_4_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_4_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_4_MAX_SPEED;
        break;
      case 4:
        motor_cfg_min[i] = MOTOR_5_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_5_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_5_MAX_SPEED;
        break;
      case 5:
        motor_cfg_min[i] = MOTOR_6_MIN_POSITION;
        motor_cfg_max[i] = MOTOR_6_MAX_POSITION;
        motor_cfg_maxspeed[i] = MOTOR_6_MAX_SPEED;
        break;
      default:
        motor_cfg_min[i] = cfg_min_pos;
        motor_cfg_max[i] = cfg_max_pos;
        motor_cfg_maxspeed[i] = cfg_max_speed;
        break;
    }
    sendCANMotorConfig(i, motor_cfg_min[i], motor_cfg_max[i], motor_cfg_maxspeed[i]);
  }
  
  // Micro-ROS initialisieren
  // Micro-ROS initialisieren (nur wenn Header vorhanden)
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
    set_microros_transports();
    // micro-ROS node + subscription for runtime motor configuration
    {
      rcl_allocator_t allocator = rcl_get_default_allocator();
      static rclc_support_t support;
      rclc_support_init(&support, 0, NULL, &allocator);
      static rcl_node_t node;
      rclc_node_init_default(&node, ROS_NODE_NAME, ROS_NODE_NAMESPACE, &support);

      static rcl_subscription_t config_sub;
      static std_msgs__msg__String config_msg;
      std_msgs__msg__String__init(&config_msg);
      rclc_subscription_init_default(&config_sub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String), "robot_arm/config");

      // initialize params publisher and param_set subscription as well
      static rcl_subscription_t param_set_sub;
      static std_msgs__msg__String param_set_msg;
      std_msgs__msg__String__init(&param_set_msg);
      rclc_subscription_init_default(&param_set_sub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String), "robot_arm/param_set");

      // initialize global params publisher
      std_msgs__msg__String__init(&robot_params_msg);
      rclc_publisher_init_default(&robot_params_pub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String), "robot_arm/params");

      rclc_executor_init(&robot_executor, &support.context, 2, &allocator);
      rclc_executor_add_subscription(&robot_executor, &config_sub, &config_msg, rosConfigCallback, ON_NEW_DATA);
      rclc_executor_add_subscription(&robot_executor, &param_set_sub, &param_set_msg, rosParamSetCallback, ON_NEW_DATA);
    }
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

  // Poll sensors (if configured) to detect external movement
  pollSensors();

  // Micro-ROS Tick
  // spin micro-ROS executor if available
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
  rclc_executor_spin_some(&robot_executor, RCL_MS_TO_NS(10));
#  endif
#endif
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

#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
// micro-ROS config callback: accepts JSON string payloads. Examples:
// {"motor":1,"min_pos":-800,"max_pos":800,"max_speed":600}
// or an array of such objects.
void rosConfigCallback(const void *msgin) {
  const std_msgs__msg__String * msg = (const std_msgs__msg__String *)msgin;
  if (!msg || !msg->data.data) return;
  String s = String(msg->data.data);
  s.trim();
  if (s.length() == 0) return;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, s);
  if (err) {
    Serial.print("ROS config JSON error: "); Serial.println(err.c_str());
    return;
  }
  if (doc.is<JsonArray>()) {
    for (JsonObject obj : doc.as<JsonArray>()) {
      int motor = obj["motor"] | -1;
      int16_t minp = obj["min_pos"] | MOTOR_MIN_POSITION;
      int16_t maxp = obj["max_pos"] | MOTOR_MAX_POSITION;
      int16_t maxs = obj["max_speed"] | MOTOR_MAX_SPEED;
      if (motor >= 1 && motor <= MOTOR_COUNT) sendCANMotorConfig((uint8_t)(motor-1), minp, maxp, maxs);
    }
  } else if (doc.is<JsonObject>()) {
    int motor = doc["motor"] | -1;
    int16_t minp = doc["min_pos"] | MOTOR_MIN_POSITION;
    int16_t maxp = doc["max_pos"] | MOTOR_MAX_POSITION;
    int16_t maxs = doc["max_speed"] | MOTOR_MAX_SPEED;
    if (motor >= 1 && motor <= MOTOR_COUNT) {
      sendCANMotorConfig((uint8_t)(motor-1), minp, maxp, maxs);
    } else {
      // apply to all motors
      for (uint8_t i = 0; i < MOTOR_COUNT; i++) sendCANMotorConfig(i, minp, maxp, maxs);
    }
  }
}
#if defined(__has_include)
#  if __has_include(<micro_ros_platform.h>)
// param_set callback: accept either parameter-like JSON or same format as config
void rosParamSetCallback(const void *msgin) {
  const std_msgs__msg__String * msg = (const std_msgs__msg__String *)msgin;
  if (!msg || !msg->data.data) return;
  String s = String(msg->data.data);
  s.trim();
  if (s.length() == 0) return;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, s);
  if (err) {
    Serial.print("ROS param_set JSON error: "); Serial.println(err.c_str());
    return;
  }
  // reuse logic: if object/array, treat like configuration and update per-motor arrays
  if (doc.is<JsonArray>()) {
    for (JsonObject obj : doc.as<JsonArray>()) {
      int motor = obj["motor"] | -1;
      int16_t minp = obj["min_pos"] | MOTOR_MIN_POSITION;
      int16_t maxp = obj["max_pos"] | MOTOR_MAX_POSITION;
      int16_t maxs = obj["max_speed"] | MOTOR_MAX_SPEED;
      if (motor >= 1 && motor <= MOTOR_COUNT) {
        motor_cfg_min[motor-1] = minp;
        motor_cfg_max[motor-1] = maxp;
        motor_cfg_maxspeed[motor-1] = maxs;
        sendCANMotorConfig((uint8_t)(motor-1), minp, maxp, maxs);
      }
    }
  } else if (doc.is<JsonObject>()) {
    int motor = doc["motor"] | -1;
    int16_t minp = doc["min_pos"] | MOTOR_MIN_POSITION;
    int16_t maxp = doc["max_pos"] | MOTOR_MAX_POSITION;
    int16_t maxs = doc["max_speed"] | MOTOR_MAX_SPEED;
    if (motor >= 1 && motor <= MOTOR_COUNT) {
      motor_cfg_min[motor-1] = minp;
      motor_cfg_max[motor-1] = maxp;
      motor_cfg_maxspeed[motor-1] = maxs;
      sendCANMotorConfig((uint8_t)(motor-1), minp, maxp, maxs);
    } else {
      for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        motor_cfg_min[i] = minp;
        motor_cfg_max[i] = maxp;
        motor_cfg_maxspeed[i] = maxs;
        sendCANMotorConfig(i, minp, maxp, maxs);
      }
    }
  }
  // publish updated params back
  publishParams();
}

// publish current motor configuration as JSON on robot_arm/params
void publishParams() {
  // build JSON
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < MOTOR_COUNT; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["motor"] = i+1;
    obj["min_pos"] = motor_cfg_min[i];
    obj["max_pos"] = motor_cfg_max[i];
    obj["max_speed"] = motor_cfg_maxspeed[i];
  }
  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  // publish using std_msgs/String
  static std_msgs__msg__String params_msg;
  std_msgs__msg__String__init(&params_msg);
  // copy into stable buffer
  static char params_buf[512];
  memcpy(params_buf, buf, n);
  params_buf[n] = '\0';
  robot_params_msg.data.data = params_buf;
  robot_params_msg.data.size = n;
  robot_params_msg.data.capacity = sizeof(params_buf);
  // publish over micro-ROS
  if (rcl_publish(&robot_params_pub, &robot_params_msg, NULL) != RCL_RET_OK) {
    Serial.println("Failed to publish params");
  } else {
    Serial.print("Published params: "); Serial.println(params_buf);
  }
}
#  endif
#endif
#  endif
#endif

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
    // Wenn das MKS SERVO42D Positions-Feedback per CAN liefert,
    // behandeln wir diese Werte als Sensorsignal und prüfen auf Bewegung.
    if (motor_id < MOTOR_COUNT && motors[motor_id] != nullptr) {
      motors[motor_id]->updateFromSensor(motor_positions[motor_id]);
    }
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
  // Use MKS SERVO CAN posAbsolute command (0xFE)
  // Format: [0xFE, speed_hi, speed_lo, accel/flags, pos24_hi, pos16, pos8]
  // Position is 24-bit signed (lower 3 bytes), speed is 16-bit.
  uint32_t id = MOTOR_1_ID + motor_index; // 0x100..0x105
  int32_t ax = (int32_t)position; // allow negative positions
  uint16_t s = (uint16_t)speed;
  uint8_t buf[7];
  buf[0] = 0xFE;
  buf[1] = (uint8_t)((s >> 8) & 0xFF);
  buf[2] = (uint8_t)(s & 0xFF);
  buf[3] = 0x00; // accel/flags (leave 0)
  buf[4] = (uint8_t)((ax >> 16) & 0xFF);
  buf[5] = (uint8_t)((ax >> 8) & 0xFF);
  buf[6] = (uint8_t)(ax & 0xFF);
  can.sendMsgBuf(id, 0, 7, buf);
}

// Send a configuration frame to a motor via CAN. Format (8 bytes):
// byte0: 0x10 = CONFIG command
// byte1: reserved (0)
// bytes2-3: min position (int16_t)
// bytes4-5: max position (int16_t)
// bytes6-7: max speed (int16_t)
void sendCANMotorConfig(uint8_t motor_index, int16_t min_pos, int16_t max_pos, int16_t max_speed) {
  if (motor_index >= MOTOR_COUNT) return;
  uint32_t id = MOTOR_1_ID + motor_index; // target motor ID
  uint8_t buf[8];
  buf[0] = 0x10; // CONFIG command
  buf[1] = 0x00;
  buf[2] = (uint8_t)((min_pos >> 8) & 0xFF);
  buf[3] = (uint8_t)(min_pos & 0xFF);
  buf[4] = (uint8_t)((max_pos >> 8) & 0xFF);
  buf[5] = (uint8_t)(max_pos & 0xFF);
  buf[6] = (uint8_t)((max_speed >> 8) & 0xFF);
  buf[7] = (uint8_t)(max_speed & 0xFF);
  can.sendMsgBuf(id, 0, 8, buf);
  if (canDebug) {
    Serial.print("Sent CONFIG to motor "); Serial.print(motor_index);
    Serial.print(" id=0x"); Serial.print(id, HEX);
    Serial.print(" min="); Serial.print(min_pos);
    Serial.print(" max="); Serial.print(max_pos);
    Serial.print(" maxspd="); Serial.println(max_speed);
  }
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