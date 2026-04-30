# Soccer Wall — System Description

*This document describes how the Soccer Wall system works and how all the hardware and software pieces fit together. Read this before looking at the code. If a term or concept is unfamiliar, it is a good candidate to look up with a web search or an AI assistant.*

---

## 1. What the System Does

The Soccer Wall is a large (~6×6 ft, ~90 lb) training frame with mannequin pads mounted on it. Two linear actuators — one on each side of the frame — push the frame upward by about 20 cm when a jump command is issued, then retract it back down. The motion simulates a defensive wall of players jumping, giving a soccer player something realistic to practice free kicks against.

The system is controlled wirelessly from any phone, tablet, or laptop via a web page served directly by the ESP32 microcontroller. No app installation is needed. The user connects to the ESP32's WiFi network and opens a browser.

---

## 2. System Architecture

```
Browser (any device)
        │
        │ WiFi (SSID: SOCCER_WALL)
        │
   ┌────▼────────────────────────────────────────┐
   │  ESP32 Microcontroller                      │
   │  • Hosts web server at 192.168.4.1          │
   │  • Runs Modbus RTU master                   │
   │  • Manages safety parameters                │
   └────────────────────┬────────────────────────┘
                        │
                        │ RS-485 (Modbus RTU, 57,600 baud)
                        │
           ┌────────────┴──────────────┐
           │                           │
    ┌──────▼──────┐             ┌──────▼──────┐
    │  Drive 1    │             │  Drive 2    │
    │  SV660P     │             │  SV660P     │
    │  Addr: 1    │             │  Addr: 2    │
    └──────┬──────┘             └──────┬──────┘
           │                           │
    ┌──────▼──────┐             ┌──────▼──────┐
    │  Motor 1    │             │  Motor 2    │
    │  MS1H4      │             │  MS1H4      │
    │  + Brake    │             │  + Brake    │
    └──────┬──────┘             └──────┬──────┘
           │                           │
    Linear Actuator 1          Linear Actuator 2
    (left side of frame)       (right side of frame)
```

The ESP32 is the brain of the system. It does two things simultaneously: it runs a web server so users can issue commands from a browser, and it communicates with both servo drives over a wired industrial bus to carry out those commands. The drives translate the commands into precise motor motion, and the motors drive the linear actuators.

---

## 3. Power System

The system runs from a portable 24V lithium battery (TechCella CAP-TC24100-NW). Because the servo drives require AC power, an inverter (Y&H RS-3200W) converts the battery's 24V DC output to 120V AC. A separate DC-DC buck converter steps the 24V down to the lower voltage the ESP32 needs for logic power.

The motor brakes (described in Section 6) require their own independent 24V supply. This supply is separate from both the main battery output and the drives' internal logic power. It turns on at system startup to release the brakes before any motion commands are sent.

```
TechCella 24V Battery
    ├── Y&H Inverter (24V DC → 120V AC) ──→ Servo Drive 1 (AC input)
    │                                    └→ Servo Drive 2 (AC input)
    ├── DC-DC Buck Converter ─────────────→ ESP32 (logic power)
    └── Brake Power Supply (24V) ─────────→ Motor 1 brake coil
                                         └→ Motor 2 brake coil
```

**Important:** The brake power supply is independent and must be powered on before any motion commands are sent. If the brakes are not released, the drives will fault immediately when they try to move the motors (fault code E630.0 — motor stall). See Section 6 for more detail on brake behavior.

---

## 4. Communication: RS-485 and Modbus RTU

### RS-485

RS-485 is a wiring standard used in industrial settings to let multiple devices communicate over the same pair of wires. It uses two wires, called A and B, that carry a differential signal — meaning the voltage difference between A and B encodes the data, not the voltage on either wire alone. This makes it very resistant to electrical noise.

All devices on an RS-485 bus share the same A and B wires. The ESP32's built-in serial port outputs standard logic-level signals (TX and RX), so a small converter chip (MAX485) translates between the ESP32's signal levels and the RS-485 differential pair.

Because RS-485 is half-duplex (only one device can transmit at a time), the MAX485 has direction control pins (DE and RE) that must be toggled when switching between transmitting and receiving. The ESP32 controls these with GPIO 4.

**Wiring:**

| ESP32 Pin | MAX485 Pin | Description |
|---|---|---|
| GPIO 17 (UART2 TX) | DI | Data from ESP32 to bus |
| GPIO 16 (UART2 RX) | RO | Data from bus to ESP32 |
| GPIO 4 | DE / RE | Direction control |

MAX485 A → SV660P CN3 pin A (both drives)
MAX485 B → SV660P CN3 pin B (both drives)

Both drives connect to the same A and B wires (daisy-chained). Always match A→A and B→B.

> **Critical:** The ESP32, MAX485 module, and both servo drives must all share a common ground connection. Without this, communication will not work. Missing ground was an early source of failures on this project.

> **Current state:** The RS-485 connections between the ESP32, the MAX485 adapter, and Drive 1 are currently assembled on a breadboard. This is adequate for testing but fragile. Replacing it with a soldered or terminal-block assembly is a planned improvement.

### Modbus RTU

Modbus RTU is the communication protocol used on top of RS-485. It defines how the ESP32 (the "master") sends read and write requests, and how the drives (the "slaves") respond. Each drive has a unique slave address (1 or 2) so the ESP32 can address them individually on the shared bus.

The ESP32 runs the **ModbusMaster** Arduino library, which handles all the low-level framing. The sketch uses it to:
- Read status registers (e.g., is the drive ready? what is the motor speed?)
- Write parameter registers (e.g., set speed limits, command a target speed)
- Enable and disable the servo via a virtual digital input signal

Both drives are on the same RS-485 bus. The ESP32 communicates with them one at a time, polling at approximately 20 Hz.

**Communication settings (must match on both drives):**

| Parameter | Value | Meaning |
|---|---|---|
| H0C.00 | 1 or 2 | Slave address (1 = Drive 1, 2 = Drive 2) |
| H0C.02 | 5 | Baud rate = 57,600 bps |
| H0C.03 | 0 | Data format: 8 data bits, no parity, 2 stop bits (8N2) |
| H0C.26 | 1 | Word order: low 16 bits before high 16 bits (for 32-bit values) |

### Modbus Register Address Format

The Inovance parameter list uses a notation like `H0C.00`. This maps to a Modbus register address as follows:

```
Parameter H0C.00 → CANopen index 200C-01h
Modbus address = (object_index_low_byte << 8) | (subindex − 1)
               = (0x0C << 8) | (1 − 1)
               = 0x0C00
```

This formula has been verified empirically across multiple parameter groups. When in doubt, double-check against the Inovance SV660P user manual — the subindex starts at 1, but the Modbus address offset starts at 0.

---

## 5. Servo Drives (Inovance SV660P)

Each servo drive takes commands over Modbus and converts them into precise motor control. The drive handles the low-level details of motor commutation, current control, speed ramping, and fault detection.

### Drive Operating Mode

The drives are configured for **speed control mode** (H02.00 = 0). In this mode, the ESP32 commands a target speed, and the drive runs the motor at that speed. The speed reference comes from a Modbus register (H06.02 = 4 routes the reference to H31.09, a 32-bit signed register in units of 0.001 rpm).

Future work will transition to **position control mode** (H02.00 = 1), which is more appropriate for the final application — commanding "extend 20 cm" rather than "spin at X rpm."

### Servo Enable (VDI)

The drive must be enabled before it will move the motor. The SV660P supports "Virtual Digital Inputs" (VDI) — enable signals that are sent over Modbus rather than requiring a physical wired pushbutton. The ESP32 sends a VDI enable command as part of its normal control sequence. This gives the software full control over when the drive is active.

A physical pushbutton is still connected to DI5 on one of the drives from an earlier phase of development, but it is no longer the primary enable mechanism.

### Safety Parameters

These parameters are written to both drives at startup by the sketch:

| Parameter | Value | Meaning |
|---|---|---|
| H06.07 | 100 rpm | Maximum speed limit |
| H06.08 | 100 rpm | Forward speed limit |
| H06.09 | 100 rpm | Reverse speed limit |
| H06.05 | 1000 ms | Acceleration ramp time |
| H06.06 | 1000 ms | Deceleration ramp time |

Do not raise the speed limits until the system is mounted to the full structure and validated with load.

### Key Status Registers (Read-Only)

These are read by the ESP32 during normal operation:

| Parameter | Modbus Address | Description |
|---|---|---|
| H30.00 | 0x3000 | Drive status word. `0x1001` = Ready, `0x2001` = Running |
| H30.04 | 0x3004 | Digital input bitmap |
| H0B.00 | 0x0B01 | Motor actual speed (rpm, signed) |
| H0B.01 | 0x0B02 | Speed reference as seen by the drive |
| H0B.26 | 0x0B1B | DC bus voltage (Modbus units differ from keypad display — trust the keypad) |

### Common Fault Codes

| Fault | Meaning | Most Likely Cause |
|---|---|---|
| E630.0 | Motor stall | Brake not released — check brake power supply first |
| E136.1 | Encoder error | Motor encoder cable disconnected or faulty |

---

## 6. Motors and Brakes (Inovance MS1H4-40B30CB)

Each motor is a 0.4 kW servo motor with a rated torque of 1.27 N·m. The motors drive SAHO linear actuators (model seh65l-p10-s300-bc-400w-c4) mounted on opposite sides of the frame.

### Integrated Brake

Each motor has a **spring-engaged, electrically-released brake** built into it. This means:

- With **no power** to the brake coil: the motor shaft is **mechanically locked**. The motor cannot rotate.
- With **24V applied** to the brake coil: the brake is released and the motor can spin freely.

This design is a safety feature — if power is lost for any reason, the brake automatically engages and holds the frame in position.

The brake coil draws approximately 0.32 A at 24V (coil resistance ~75Ω). The brake's holding torque (1.5 N·m) exceeds the motor's rated torque (1.27 N·m), so the motor cannot back-drive the brake when it is engaged.

Both brake coils are wired to the dedicated 24V brake power supply. This supply must be energized at system startup **before** any motion commands are sent. Attempting to command motion with the brakes engaged will cause the drive to fault with E630.0 within seconds.

---

## 7. ESP32 and Web Interface

The ESP32 is a low-cost microcontroller with built-in WiFi and Bluetooth. In this project it runs an Arduino sketch (`SoccerWall.ino`) that does two things at the same time:

**Web server:** The ESP32 creates its own WiFi access point with the SSID `SOCCER_WALL`. Any device that connects to this network and opens a browser to `192.168.4.1` will see the control interface. The interface allows the user to enable/disable the servos, send motion commands, home the actuators, and trigger an emergency stop. No internet connection is required.

**Modbus master:** The sketch continuously polls both drives for status (drive state, motor speed, fault codes) and sends commands (enable signals, speed targets) over RS-485. This runs in the background while the web server handles incoming browser requests.

### Header File (SoccerWall.h)

All pin assignments, register addresses, constants, and data structures are defined in `SoccerWall.h`. If you need to change a hardware pin or a Modbus register address, change it there — not scattered through the `.ino` file.

---

## 8. Current Limitations and Known Issues

**Second motor partial support:** Drive 2 is communicating successfully over RS-485. However, several features in the web interface — including the homing function — currently only operate Motor 1. These need to be extended to Motor 2 and tested for synchronized dual-actuator operation.

**Breadboard wiring:** The RS-485 connections between the ESP32, MAX485 adapter, and Drive 1 are on a breadboard. This is fine for bench testing but should be replaced with a more permanent assembly before the system is mounted to the structure.

**Actuators not yet mounted:** The linear actuators have been tested independently but have not yet been mounted to the large frame structure. Mounting is the main remaining mechanical step.

**Speed mode only:** The drives are currently in speed control mode. Position control mode (needed for the final "jump 20 cm" behavior) has not yet been implemented and will require drive reconfiguration and tuning.

---

## 9. Startup Sequence

Follow this order every time the system is powered on:

1. Power on the 24V battery, inverter, and brake power supply
2. Power on the ESP32
3. Confirm both drive keypads show `rdy`
4. Connect a phone or laptop to WiFi network `SOCCER_WALL`
5. Open a browser and navigate to `192.168.4.1`
6. Use the web interface to enable the servos and issue commands

If something does not work, diagnose by working backward through the chain:
- Are the brakes released? (Is the brake supply on?)
- Are the drives showing `rdy` on the keypad?
- Is the WiFi network visible and can you reach `192.168.4.1`?
- Is RS-485 communication working? (Use the diagnostic tools in the web interface)

---

## 10. Useful References

- [Inovance SV660P User Manual](https://www.inovance.com) — Full parameter list, Modbus register map, fault code descriptions
- [ModbusMaster Arduino Library](https://github.com/4-20ma/ModbusMaster) — Library used for Modbus RTU communication on the ESP32
- [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32) — ESP32 Arduino documentation
