/*
  wifi_master.ino

  ESP32 sketch: stellt ein WLAN (AP) bereit und empfängt Befehle per UDP.
  Leitet empfangene Steuerbefehle per UART an `motor_node` weiter.

  Einfaches UDP-Protokoll:
    Sende UDP-Paket mit Text "pos,<joint>,<pos>" z.B. "pos,1,9000"

  Passe `AP_SSID`/`AP_PASS` und UART-Pins/Einstellungen an.
*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Arduino.h>

const char* AP_SSID = "RobotMaster";
const char* AP_PASS = "robot1234"; // mind. 8 Zeichen empfohlen

const unsigned int UDP_PORT = 8888;

// UART zu motor_node
HardwareSerial &motorSerial = Serial1; // Serial1 TX/RX pins anpassen falls nötig

WiFiUDP Udp;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("wifi_master starting...");

  // Start Access Point
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(ip);

  // start UDP
  Udp.begin(UDP_PORT);
  Serial.print("UDP listening on port "); Serial.println(UDP_PORT);

  // UART to motor_node
  motorSerial.begin(115200, SERIAL_8N1, -1, -1); // falls feste Pins benötigt werden, ändern
}

char packetBuffer[256];

void loop() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int len = Udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) packetBuffer[len] = 0;
    String msg = String(packetBuffer);
    msg.trim();
    Serial.print("RX UDP: "); Serial.println(msg);

    // Forward raw message to motor_node via UART
    motorSerial.println(msg);
  }

  // Optional: lokal per Serial Konsole Befehle eingeben
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length()) {
      motorSerial.println(s);
      Serial.print("Forwarded: "); Serial.println(s);
    }
  }
}
