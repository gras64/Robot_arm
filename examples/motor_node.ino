/*
  motor_node.ino

  ESP32 sketch: empfängt Steuerbefehle über UART (Serial1) und schickt CAN-Frames
  an MKS SERVO42D Module.

  Neu: einfache, konfigurierbare MKS "open-loop" CAN-Treiberschicht und ein
  kleiner G-Code-Parser. G-Code-Format (vereinfachte Variante):
    G0/G1 J1=<deg> J2=<deg> ... J6=<deg>
    Beispiel: "G1 J1=90 J2=45 J3=0"

  Die Implementierung sendet derzeit Positionen als 16-bit Ganzzahl in
  Zentigrad (deg * 100). Falls dein MKS-Modul anderes Format erwartet,
  passe `sendPositionToServo_MKS()` an.

*/

#include <Arduino.h>
#include <CAN.h>

// Konfiguration
const int CAN_RX_PIN = 4; // anpassen falls nötig
const int CAN_TX_PIN = 5; // anpassen falls nötig
const long CAN_BAUD = 500E3; // 500 kbit/s (häufig), evtl. 1E6

const unsigned long SAFETY_TIMEOUT_MS = 500;

unsigned long last_command_time = 0;

// CAN/Servo mapping
const uint32_t CAN_ID_BASE = 0x200; // Basis-ID, Servo-ID = CAN_ID_BASE + joint

// --- Low-level send (konfigurierbar für MKS) -------------------------------
// Aktuell: simple open-loop position command: [0x01, pos_hi, pos_lo]
// Wenn MKS anderes erwartet, ändere diese Funktion entsprechend.
void sendPositionToServo_MKS(uint8_t joint, int16_t pos_centideg) {
  uint32_t id = CAN_ID_BASE + joint;
  uint8_t buf[3];
  buf[0] = 0x01; // application-defined position command
  buf[1] = (pos_centideg >> 8) & 0xFF;
  buf[2] = pos_centideg & 0xFF;

  if (CAN.beginPacket(id)) {
    CAN.write(buf, 3);
    CAN.endPacket();
  }
}

void sendTorqueOff(uint8_t joint) {
  uint32_t id = CAN_ID_BASE + joint;
  uint8_t buf[1]; buf[0] = 0x02; // torque off
  if (CAN.beginPacket(id)) {
    CAN.write(buf, 1);
    CAN.endPacket();
  }
}

void sendStopAll() {
  for (uint8_t j = 1; j <= 6; ++j) sendTorqueOff(j);
}

// --- G-code parser --------------------------------------------------------
// Erwartet G0/G1 mit Parametern J1..J6
void parseAndHandleGcode(const String &line) {
  String s = line;
  s.trim();
  if (s.length() == 0) return;

  // Konvertiere in Grossbuchstaben für Befehlserkennung
  String up = s;
  up.toUpperCase();

  if (! (up.startsWith("G0") || up.startsWith("G1")) ) {
    // Optional: unterstütze direkte POS-Kommandos wie "pos,1,9000"
    if (up.startsWith("POS,")) {
      int parts[3] = {-1, -1, -1};
      // einfache POS,1,9000
      char buf[s.length()+1];
      s.toCharArray(buf, sizeof(buf));
      char *tok = strtok(buf, ",\r\n");
      if (tok && strcmp(tok, "pos") == 0) {
        char *js = strtok(NULL, ",\r\n");
        char *ps = strtok(NULL, ",\r\n");
        if (js && ps) {
          int joint = atoi(js);
          int pos = atoi(ps);
          if (joint >= 1 && joint <=6) {
            sendPositionToServo_MKS((uint8_t)joint, (int16_t)pos);
            last_command_time = millis();
            Serial.printf("POS cmd -> J%d = %d\n", joint, pos);
          }
        }
      }
    }
    return;
  }

  // Suche nach Parametern J1..J6
  for (uint8_t j = 1; j <= 6; ++j) {
    String key = "J" + String(j) + "=";
    int idx = up.indexOf(key);
    if (idx >= 0) {
      // Extrahiere Wert nach '='
      int valStart = idx + key.length();
      int valEnd = valStart;
      while (valEnd < (int)up.length() && (isDigit(up[valEnd]) || up[valEnd]=='.' || up[valEnd]=='-' )) valEnd++;
      String valStr = s.substring(valStart, valEnd);
      valStr.trim();
      if (valStr.length()) {
        float deg = valStr.toFloat();
        int16_t centideg = (int16_t) round(deg * 100.0);
        sendPositionToServo_MKS(j, centideg);
        last_command_time = millis();
        Serial.printf("Gcode -> J%d = %.2f deg (%d)\n", j, deg, centideg);
      }
    }
  }

  // Zusätzliche direkte Textbefehle: LEARN,<joint> und STATUS,<joint>
  String upTrim = up;
  upTrim.trim();
  if (upTrim.startsWith("LEARN,")) {
    int comma = upTrim.indexOf(',');
    String num = upTrim.substring(comma+1);
    int joint = num.toInt();
    if (joint >= 1 && joint <= 6) {
      uint8_t mode = 1; // enable learn
      uint32_t id = CAN_ID_BASE + joint;
      uint8_t buf[2]; buf[0] = 0x10; buf[1] = mode;
      if (CAN.beginPacket(id)) { CAN.write(buf, 2); CAN.endPacket(); }
      Serial.printf("Sent LEARN to J%d\n", joint);
      last_command_time = millis();
    }
  } else if (upTrim.startsWith("STATUS,")) {
    int comma = upTrim.indexOf(',');
    String num = upTrim.substring(comma+1);
    int joint = num.toInt();
    if (joint >= 1 && joint <= 6) {
      uint32_t id = CAN_ID_BASE + joint;
      uint8_t buf[1]; buf[0] = 0x03; // status request
      if (CAN.beginPacket(id)) { CAN.write(buf, 1); CAN.endPacket(); }
      Serial.printf("Requested STATUS from J%d\n", joint);
      last_command_time = millis();
    }
  }
}

// --- Setup / Loop ---------------------------------------------------------
void setup() {
  Serial.begin(115200); // USB console
  Serial1.begin(115200, SERIAL_8N1, -1, -1); // UART from wifi_master (pins optional)

  delay(100);
  Serial.println("motor_node (MKS open-loop + G-code) starting...");

  // CAN init
  if (!CAN.begin(CAN_BAUD)) {
    Serial.println("Starting CAN failed!");
    while (true) delay(1000);
  }
  Serial.println("CAN initialized");

  last_command_time = millis();
}

String rxLine;

void loop() {
  // UART input from wifi_master (Serial1)
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      parseAndHandleGcode(rxLine);
      rxLine = "";
    } else if (c >= 32) {
      rxLine += c;
      if (rxLine.length() > 256) rxLine = rxLine.substring(rxLine.length() - 256);
    }
  }

  // Optional: accept local serial input from USB console for testing
  while (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length()) {
      parseAndHandleGcode(s);
    }
  }

  // CAN-Empfang: einfache Telemetrie/Antworten von Servos loggen
  int packetSize = CAN.parsePacket();
  if (packetSize) {
    uint32_t id = CAN.packetId();
    int len = CAN.available();
    uint8_t data[64];
    int i=0;
    while (CAN.available() && i < (int)sizeof(data)) data[i++] = CAN.read();
    Serial.printf("CAN RX ID=0x%03X len=%d ", id, i);
    for (int k=0;k<i;k++) Serial.printf("%02X ", data[k]);
    Serial.println();
    if (i>0 && data[0]==0x04) {
      Serial.println("Status-Reply received");
    }
  }

  // Safety
  if (millis() - last_command_time > SAFETY_TIMEOUT_MS) {
    sendStopAll();
    last_command_time = millis();
  }
}
