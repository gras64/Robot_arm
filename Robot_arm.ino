/*
  Projekt: Robot_arm

  Dieses Repository enthält zwei Beispiel-Firmwares für einen 6-Gelenk
  Roboterarm mit MKS SERVO42D CAN-Servos:

  - motor_node.ino   -> Läuft auf dem ESP32, verbindet sich mit dem CAN-Bus
                        und sendet Befehle an die MKS SERVO42D Module.
                        Empfängt Steuerbefehle über `Serial`/UART.

  - wifi_master.ino  -> Läuft auf einem zweiten ESP32, übernimmt WLAN/UDP
                        (oder AP) und leitet empfangene Befehle per UART
                        an den `motor_node` weiter.

  Baue und lade die beiden Sketches getrennt auf die jeweiligen ESP32s.

  Die eigentliche Implementierung befindet sich in den Dateien
  `motor_node.ino` und `wifi_master.ino`.

*/

void setup() {
  // Platzhalter: das eigentliche Verhalten ist in den Beispiel-Sketches.
}

void loop() {
  // leer
}
