# 6-Achsen Robotarm mit ESP32 und CAN-Bus

Dieses Projekt implementiert einen 6-achsen Robotarm mit NEMA 17 Motoren, die über einen CAN-Bus mit einem ESP32-WROOM-1 Steuerversion kommunizieren.

## Hardware

- **Steuereinheit**: ESP32-WROOM-1
- **Motoren**: 6 x NEMA 17
- **Motor-Treiber**: MKS SERVO42D
- **CAN-Bus Controller**: MCP2515
- **Kommunikation**: CAN-Bus (500 kbps)

## Projektstruktur

```
robot_arm/
├── robot_arm.ino          # Haupt-Steuerungsdatei
├── README.md              # Diese Dokumentation
└── src/                   # Quellcodedateien
    ├── robot_arm.ino      # Haupt-Steuerung
    ├── motor_control.h    # Motorsteuerungs-Klasse
    └── motor_control.cpp  # Motorsteuerungs-Implementierung
```

## Funktionen

- CAN-Bus Kommunikation für Motorsteuerung
- Unterstützung für 6 Motoren mit individuellen CAN-IDs
- PWM-basierte Steuerung der MKS SERVO42D Treiber
- Serielle Kommunikation zur Debugging-Zwecken

## CAN-Bus IDs

- Motor 1: 0x100
- Motor 2: 0x101
- Motor 3: 0x102
- Motor 4: 0x103
- Motor 5: 0x104
- Motor 6: 0x105

## Steckerbelegung

- CAN_CS_PIN: GPIO 10
- CAN_INT_PIN: GPIO 2
- Motor 1: GPIO 25
- Motor 2: GPIO 26
- Motor 3: GPIO 27
- Motor 4: GPIO 14
- Motor 5: GPIO 12
- Motor 6: GPIO 13

Hinweis: Die oben aufgeführten GPIO-Pins sind die optionalen PWM-/GPIO-Ausgänge,
die verwendet werden, wenn Sie Treiber direkt per PWM ansteuern. Bei Verwendung
der MKS SERVO42D über CAN ist das nicht nötig — die Motoren werden ausschließlich
über den CAN-Bus gesteuert. Die Firmware deaktiviert die direkten PWM-Ausgänge
standardmäßig, um Konflikte zu vermeiden. Schließen Sie stattdessen die CAN_H/CAN_L
Leitungen, Vcc und gemeinsames GND an die MKS SERVO42D an.

Kurz:
- Wenn Sie CAN verwenden: Verbinden Sie nur CAN_H/CAN_L + Vcc + GND; die GPIOs
    müssen nicht angesetzt werden.
- Wenn Sie GPIO/PWM verwenden wollen: schließen Sie die Motor-Pins an und aktivieren
    `setUsePWM(true)` im Code für die gewünschten Motoren.

## Implementierung

Die Implementierung verwendet:

1. **SPI-Bibliothek** für die Kommunikation mit dem MCP2515
2. **CAN-Bus Bibliothek** für die CAN-Kommunikation
3. **PWM-Ausgabe** für die Motorsteuerung
4. **Serial-Kommunikation** für Debugging-Zwecke

## Verwendung

1. Laden Sie die Firmware auf Ihren ESP32-WROOM-1
2. Verbinden Sie die Motoren mit den MKS SERVO42D Treibern
3. Stellen Sie die CAN-Bus-Verbindung her
4. Senden Sie CAN-Nachrichten an die entsprechenden IDs mit Position und Geschwindigkeitswerten

## CAN-Nachrichtenformat

Jede CAN-Nachricht enthält 4 Bytes:
- Bytes 0-1: Position (int16_t)
- Bytes 2-3: Geschwindigkeit (int16_t)

Werte sind im Bereich von -1000 bis 1000.

Hinweis zur MKS-CAN‑Kompatibilität:
- Die Firmware sendet Bewegungsbefehle als MKS SERVO `posAbsolute` Frames (Command `0xFE`) mit dem Byte‑Layout `[0xFE, speed_hi, speed_lo, accel, pos24_hi, pos16, pos8]` (7 Bytes). Das ist kompatibel mit gängigen MKS/CAN Implementierungen wie `MKSServoCAN`.
- Einige ältere Beispiele im Repo verwenden ein 4‑Byte Format; das funktioniert nur mit angepasster Firmware auf der Gegenseite. Verwende das `posAbsolute` Format für volle Kompatibilität.

## Lizenz

Dieses Projekt ist noch nicht lizenziert.

## CAN-Bus Debugging

- **Serial Debugging:** Verwenden Sie die serielle Konsole (`Serial`, 115200) um empfangene CAN-Frames, Fehlerstatistiken und Bestätigungen zu sehen.
- **Sofort-Befehle über UART:** Die Firmware unterstützt einfache Debug-Befehle über USB-Serial:
    - `DBG CAN ON` / `DBG CAN OFF` : Ausgabe aller eingehenden CAN-Frames ein-/ausschalten.
    - `DBG LOOPBACK ON` / `DBG LOOPBACK OFF` : MCP2515 in Loopback für lokale Tests versetzen bzw. zurücksetzen.
    - `DBG STATS` : Zeigt Fehler- und Zählerstatistiken des CAN-Controllers.
- **Loopback-Test:** Im Loopback-Modus (`DBG LOOPBACK ON`) kann die Firmware CAN-Frames senden und lokales Empfangs-Logging prüfen, ohne physischen Bus.
- **Interrupt-basiert:** Die Firmware nutzt den MCP2515-INT-Pin (GPIO `CAN_INT_PIN`) und gibt bei aktivierter Debug-Ausgabe empfangene Frames sofort aus.

## Backend / G-code über MQTT

- **MQTT Integration:** Die Firmware kann G-code oder JSON-Commands per MQTT empfangen (Topic: `robot_arm/command`) und ACKs auf `robot_arm/ack` senden. Stelle `WIFI_SSID`, `WIFI_PASS` und `MQTT_SERVER` in `src/robot_arm.ino` ein.
- **Unterstützte Formate:**
    - JSON: `{ "cmd":"move", "motor":3, "pos":500, "speed":200 }`
    - G-code (einfache Form): `G1 A500 B-200 F300` (A–F = Achsen 1–6, F = Speed)
- **ACK / Fehler:** Nach Verarbeitung sendet die Firmware z.B. `ok` oder `err_*` an `robot_arm/ack`.
- **Flow & Safety:** Für produktive Nutzung empfehle ich Sequenznummern, ACK/Retry-Logik und einen Not-Aus (`M112`) als Prioritätsbefehl.

## Motor-Konfiguration über micro-ROS

    ```json
    { "motor": 1, "min_pos": -800, "max_pos": 800, "max_speed": 600 }
    ```
    ```json
    { "min_pos": -1000, "max_pos": 1000, "max_speed": 1000 }
    ```

Hinweis: micro-ROS-Features werden nur kompiliert, wenn `micro_ros_platform.h` verfügbar ist; die Verbindung wird in `setup()` initialisiert.

Zusätzlich: Parameter-ähnliche Steuerung


Beispiele:
`robot_arm/param_set` payload (ein Motor):
```json
{ "motor": 1, "min_pos": -800, "max_pos": 800, "max_speed": 600 }
```
`robot_arm/param_set` payload (alle Motoren):
```json
{ "min_pos": -1000, "max_pos": 1000, "max_speed": 1000 }
```

Per-Motor-Voreinstellungen

Du kannst pro Motor andere Min/Max/Speed-Werte als Firmware-Voreinstellung in `src/micro_ros_config.h` definieren. Standardmäßig werden dort `MOTOR_1_MIN_POSITION` .. `MOTOR_6_MAX_SPEED` mit sinnvollen Defaults gesetzt. Beispiel-Overrides in `src/micro_ros_config.h`:

```c
// Beispiel: Motor 1 hat engeren Bereich
#define MOTOR_1_MIN_POSITION -800
#define MOTOR_1_MAX_POSITION 800
#define MOTOR_1_MAX_SPEED 800
```

Die Firmware liest diese Werte beim Start und sendet die entsprechenden CONFIG-Frames per CAN an jeden Motor. Danach kannst du Konfigurationen weiterhin live per `robot_arm/param_set` setzen.

## Kurze Upload- und Monitor-Hinweise

- Upload (Beispiel):
    ```bash
    platformio run -e esp32dev --target upload --upload-port /dev/ttyUSB0
    ```
- Serielle Konsole:
    ```bash
    platformio device monitor -p /dev/ttyUSB0 -b 115200
    ```
