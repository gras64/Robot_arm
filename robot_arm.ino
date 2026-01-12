#include <Arduino.h>
#include <mcp_can.h>
#include <SPI.h>

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
int16_t motor_positions[MOTOR_COUNT] = {0};
int16_t motor_speeds[MOTOR_COUNT] = {0};

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
  pinMode(CAN_INT_PIN, INPUT);
  
  // Motor-Pins als Ausgänge konfigurieren
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(MOTOR_PIN_3, OUTPUT);
  pinMode(MOTOR_PIN_4, OUTPUT);
  pinMode(MOTOR_PIN_5, OUTPUT);
  pinMode(MOTOR_PIN_6, OUTPUT);
  
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  digitalWrite(MOTOR_PIN_3, LOW);
  digitalWrite(MOTOR_PIN_4, LOW);
  digitalWrite(MOTOR_PIN_5, LOW);
  digitalWrite(MOTOR_PIN_6, LOW);
}

void loop() {
  // CAN Nachrichten empfangen
  receiveCANMessages();

  // UART -> CAN: prüfe serielle Eingaben und leite als CAN-Befehl weiter
  parseSerialCommand();

  // Motorsteuerung basierend auf CAN-Nachrichten
  controlMotors();

  delay(10);
}

void receiveCANMessages() {
  uint8_t len = 0;
  uint8_t buf[8];
  uint32_t canId;
  
  // Prüfen, ob neue CAN Nachrichten vorhanden sind
  if (can.checkReceive() == CAN_MSGAVAIL) {
    // readMsgBuf fills id, len and buf
    can.readMsgBuf(&canId, &len, buf);
    
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
  analogWrite(MOTOR_PIN_1, map(motor_positions[0], -1000, 1000, 0, 255));
  analogWrite(MOTOR_PIN_2, map(motor_positions[1], -1000, 1000, 0, 255));
  analogWrite(MOTOR_PIN_3, map(motor_positions[2], -1000, 1000, 0, 255));
  analogWrite(MOTOR_PIN_4, map(motor_positions[3], -1000, 1000, 0, 255));
  analogWrite(MOTOR_PIN_5, map(motor_positions[4], -1000, 1000, 0, 255));
  analogWrite(MOTOR_PIN_6, map(motor_positions[5], -1000, 1000, 0, 255));
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
  can.sendMsgBuf(id, 0, 4, buf);
}

// Einfacher ASCII-Parser: erwartetes Format: "SET <id> <pos> <speed>\n"
void parseSerialCommand() {
  static String line = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      if (line.length() > 0) {
        int idx = -1; int pos = 0; int speed = 0;
        if (line.startsWith("SET ")) {
          char buf[64];
          line.toCharArray(buf, sizeof(buf));
          char *tok = strtok(buf, " \t");
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
      if (line.length() > 120) line = "";
    }
  }
}