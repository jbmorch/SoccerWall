# Soccer Wall — Automated Training Device

An automated soccer training wall that simulates a defensive player wall. A large (\~6×6 ft, \~90 lb) frame with mannequin pads jumps vertically (\~20 cm) on command, triggered wirelessly from any device with a web browser. The system uses industrial servo motors for precise, repeatable motion.

\---

## Project Status

**As of April 2026 — Core motion is working end-to-end.**

* ✅ ESP32 ↔ servo drive communication (Modbus RTU over RS-485) — stable at 20 Hz
* ✅ Drive parameters configurable and readable over Modbus
* ✅ Servo enable via software VDI signal
* ✅ Smooth motion confirmed — actuator responds correctly
* ✅ Brake release via dedicated power supply at startup
* ✅ Both drives communicating (Drive 1 and Drive 2 on shared RS-485 bus)
* ✅ Web interface — connect any device to SSID `SOCCER\_WALL`, open `192.168.4.1`
* ⏳ Second motor: communicating but some features incomplete (e.g. homing only moves Motor 1)
* ⏳ Position control tuning and full dual-actuator synchronization
* ⏳ Actuators physically connected to the large frame structure

\---

## What's in This Repository

|File|Description|
|-|-|
|`soccer_wall.ino`|Main Arduino sketch for the ESP32. Handles Modbus RTU communication, safety parameter setup, and servo commanding.|
|`web_ui.h`|Header file — web-based user interface.|
|`SystemDescription.md`|Full system architecture narrative. Read this to understand how all the pieces fit together.|
|`WiringDiagram.\*`|Wiring diagram showing all electrical connections between components.|
|`README.md`|This file.|

**Start here if you're new:** Read `SystemDescription.md` first, then look at the wiring diagram, then open the `.ino` file.

\---

## System Overview

```
Browser (any device)  ──WiFi──  ESP32  ──RS-485──  Drive 1 (SV660P)  ──  Motor 1 + Brake
                                    (web server)  └──RS-485──  Drive 2 (SV660P)  ──  Motor 2 + Brake
```

The ESP32 is the brain. It runs a **web server** that any phone, tablet, or laptop can connect to over WiFi — no app installation required. It talks to the two servo drives using an industrial protocol called **Modbus RTU** over a wired **RS-485** connection. The servo drives control the motors, which extend and retract the linear actuators that move the frame.

Both drives share the same two RS-485 wires (a "bus"). The ESP32 addresses them separately using Drive 1 (slave address 1) and Drive 2 (slave address 2).

Servo enable is controlled in software via a **VDI (Virtual Digital Input)** signal sent over Modbus — no physical wiring to a pushbutton is required for normal operation.

The motor brakes are released by a dedicated power supply that energizes at startup. Both brakes must be released before motion is possible.

\---

## Hardware Summary

|Component|Part|Notes|
|-|-|-|
|Microcontroller|ESP32 (generic dev board)|Runs the Arduino sketch|
|Servo drives|Inovance SV660P (×2)|Controls motor speed/position|
|Motors|Inovance MS1H4-40B30CB (×2)|0.4 kW, includes integrated brake|
|RS-485 adapter|MAX485-based module|Converts ESP32 serial to RS-485|
|Battery|TechCella CAP-TC24100-NW (24V)|Portable power source|
|Inverter|Y\&H RS-3200W|Converts 24V battery to AC for the drives|
|DC-DC converter|Buck converter|Steps down 24V for ESP32 logic|

\---

## Key Concepts to Know

If something in the code or docs is unfamiliar, these are the most important concepts to look up:

**Modbus RTU** — A simple communication protocol widely used in industrial equipment. The ESP32 acts as the "master" and the servo drives act as "slaves." Each drive has a unique slave address (1 and 2) on the shared bus. The Arduino library used is `ModbusMaster`.

**RS-485** — A wiring standard for robust serial communication over longer distances. Uses two wires (A and B) in a differential signal pair. All devices share the same two wires. The MAX485 chip converts between the ESP32's standard serial signals and RS-485.

**Servo drive** — A motor controller that can precisely control speed and position. The SV660P drives are configured by writing values to registers (called "parameters") over Modbus. The parameter list and register address format are documented in `SystemDescription.md`.

**VDI (Virtual Digital Input)** — The servo drives support a set of "virtual" digital inputs that can be set by the ESP32 over Modbus, without any physical wiring. Servo enable (S-ON) is sent this way. This is different from a physical pushbutton — the software is fully in control.

**Integrated brake** — The motors have a spring-engaged, electrically-released brake built in. When there is no power to the brake coil, the motor is mechanically locked. The dedicated brake power supply releases both brakes at system startup. **If the brakes are not powered, the motors cannot move** — this is the most common cause of unexpected no-motion behavior.

\---

## Wiring Quick Reference

### RS-485 (ESP32 ↔ Servo Drives)

|ESP32 Pin|MAX485 Pin|Description|
|-|-|-|
|GPIO 17 (UART2 TX)|DI|Data out to bus|
|GPIO 16 (UART2 RX)|RO|Data in from bus|
|GPIO 4|DE / RE|Direction control (transmit vs. receive)|

MAX485 A/B → SV660P CN3 A/B. Both drives connect to the same A and B wires (daisy-chained). Match A→A and B→B.

> \*\*Important:\*\* ESP32, MAX485, and both servo drives must share a common ground. Missing ground was an early source of communication failure on this project.

> \*\*Note on current wiring:\*\* The RS-485 connection between the ESP32, MAX485 adapter, and Drive 1 is currently assembled on a breadboard. This works for testing but is fragile — replacing it with a more permanent wired assembly is a planned next step.

### Brake Power Supply

Each motor has an integrated brake coil that requires 24V DC to release. Both brake coils are connected to an independent 24V power supply that turns on at system startup. **This supply must be on before any motion commands are sent.**

The CN1 connector on the servo drives (which has digital I/O pins) is not used in the current design.

\---

## Communication Settings (Verified Working)

Both drives must be configured with these parameters. Drive 1 has slave address 1; Drive 2 has slave address 2 (parameter H0C.00).

|Parameter|Value|Meaning|
|-|-|-|
|H0C.00|1 (or 2)|Slave address|
|H0C.02|5|Baud rate = 57,600 bps|
|H0C.03|0|No parity, 2 stop bits (8N2)|

\---

## Quickstart Checklist

Use this when powering up the system for a test session:

1. Power on the 24V battery, inverter, and brake power supply
2. Power on the ESP32
3. Confirm both drive keypads show `rdy`
4. On your phone or laptop, connect to WiFi network **`SOCCER\_WALL`**
5. Open a browser and navigate to **`192.168.4.1`**
6. The web interface should appear — use it to enable the servos and send commands
7. Confirm both motors respond to commands

If something doesn't work, work backward through the chain: check that the brake supply is on, then check that both drives show `rdy`, then check WiFi connectivity, then check RS-485 communication using the diagnostic tools in the web interface.

\---

## Safety Notes

* **Never run the system unattended** until the actuators are mounted to the full structure and all software safety limits have been validated with load.
* Speed limits are set conservatively in the drive parameters (100 rpm max). Do not raise these until the system is fully validated.
* Servo enable is software-controlled via VDI. This means a software bug could leave the drives enabled. The web interface includes an emergency stop — keep it accessible during all tests.
* Do not perform a factory reset on the drives (`H02.31 = 1`) without first writing down the current Modbus communication parameters — a reset will break the RS-485 link and require manual keypad reconfiguration.
* The brake power supply must be on before any motion commands are sent. If the brakes are engaged while the drive tries to move the motor, the drive will fault (E630.0 stall error) within seconds.

\---

## Next Steps (Ordered by Priority)

1. **Connect actuators to the structure** — mount the linear actuators to the large frame; this is the main remaining mechanical step before real-world testing
2. **Improve the RS-485 wiring** — replace the current breadboard assembly between the ESP32, MAX485 adapter, and Drive 1 with a robust soldered or terminal-block connection
3. **Complete dual-motor software features** — several features in the web interface (including homing) currently only operate Motor 1; these need to be extended to Motor 2 and tested for synchronized operation

\---

## Useful References

* [Inovance SV660P User Manual](https://www.inovance.com) — Full parameter list and Modbus register map
* [ModbusMaster Arduino Library](https://github.com/4-20ma/ModbusMaster) — Library used for Modbus RTU communication
* [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32) — ESP32 Arduino documentation

