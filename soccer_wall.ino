// =============================================================================
// Soccer Wall — ESP32 Web Server + Motion Control
// =============================================================================
//
// Architecture:
//   - WiFi Access Point: SSID "SOCCER_WALL", IP 192.168.4.1
//   - ESPAsyncWebServer serves the web UI and handles WebSocket
//   - ModbusMaster drives two SV660P servo controllers over RS485
//   - Motion state machine runs in loop(), WebSocket callbacks set commands
//
// PROTOTYPE NOTES:
//   - Jump and manual use speed mode (H02.00 = 0) with software position
//     tracking. Position is estimated by integrating speed × elapsed time.
//     Calibrate LEAD_SCREW_PITCH_MM for your actuator.
//   - Homing uses the drive's native homing routine (H05.30 = 4, hit-and-stop
//     method). The drive switches to position mode, homes, then the sketch
//     restores speed mode. No software stall detection needed.
//   - Servo enable is via Modbus VDI (H0C.09, H17.00, H31.00).
//   - Positive rpm = retract (toward home). Negative rpm = extend.
//     Empirically determined from hardware testing.
//
// Required libraries (install via Arduino Library Manager):
//   - ESPAsyncWebServer  (by Me-No-Dev)
//   - AsyncTCP           (by Me-No-Dev)
//   - ModbusMaster       (by Doc Arduino)
//   - ArduinoJson        (by Benoit Blanchon, v6)
//
// =============================================================================

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include "web_ui.h"

// =============================================================================
// Hardware Configuration
// =============================================================================

#define PIN_RS485_TX     17
#define PIN_RS485_RX     16
#define PIN_RS485_DE_RE   4

// =============================================================================
// WiFi / Network
// =============================================================================

const char* AP_SSID     = "SOCCER_WALL";
const char* AP_PASSWORD = "";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

// =============================================================================
// Motion Parameters — TUNE THESE FOR YOUR HARDWARE
// =============================================================================

// Lead screw pitch: mm per motor revolution.
// SAHO actuator "p10" = 10mm/rev. Verify empirically.
const float LEAD_SCREW_PITCH_MM = 11.0f;

// Jump target: 90% of 400mm rated travel
const float JUMP_TARGET_MM      = 243.0f;

// Travel limit enforced in software
const float MAX_TRAVEL_MM       = 395.0f;

// Speeds in rpm.
// IMPORTANT: positive rpm = retract (toward home), negative rpm = extend.
// This was determined empirically from hardware testing.
const int JUMP_EXTEND_SPEED  = -400;  // rpm
const int JUMP_RETRACT_SPEED =  400;  // rpm
const int CAL_SPEED          =   80;  // rpm (magnitude, sign applied per direction)

// Speed to mm/s conversion
inline float rpmToMmPerSec(int rpm) {
  return (float)rpm * LEAD_SCREW_PITCH_MM / 60.0f;
}

// =============================================================================
// Homing Parameters
// =============================================================================

// H05.31 = 10: Forward, mechanical limit position as deceleration point and home.
// "Forward" in drive terms = positive rpm direction = retract = correct for our system.
const uint16_t HOMING_MODE_VAL   = 10;

// H05.32: Speed during homing approach (rpm)
const uint16_t HOMING_HI_SPEED   = 60;

// H05.33: Low-speed search (rpm) — set for completeness, not used for mech limit
const uint16_t HOMING_LO_SPEED   = 100;

// H05.34: Accel/decel time during homing (ms, 0-1000rpm)
const uint16_t HOMING_ACCEL_MS   = 500;

// H05.35: Drive-side time limit (ms, with H05.66=0 → 1ms unit)
const uint16_t HOMING_TIMELIM    = 65000;  // 65 seconds

// H05.56: Speed threshold for hit-and-stop detection (rpm)
const uint16_t HOMING_SPD_THR    = 5;

// H05.57: Consecutive samples below threshold to confirm stall
const uint16_t HOMING_HIT_CNT    = 5;

// H05.58: Torque limit during homing (0.1% units, so 500 = 50.0%)
// This prevents E630.0 by limiting current spike at end of travel.
const uint16_t HOMING_TORQUE_PCT = 2000;

// Software-side backup timeout (slightly longer than drive timeout)
const uint32_t HOMING_SW_TIMEOUT = 72000;  // 72 seconds

// =============================================================================
// Modbus Register Addresses
// =============================================================================
// Formula: Modbus addr = (CANopen_object_index_low_byte << 8) | (subindex - 1)

// H02 — Control mode
#define REG_CONTROL_MODE     0x0200  // H02.00: 0=Speed, 1=Position

// H03 — DI Functions
#define REG_DI5_FUNCTION     0x030A  // H03.10: DI5 function (0=unassigned)

// H05 — Position control / Homing
#define REG_HOMING_SELECT    0x051E  // H05.30: 0=off, 4=execute on S-ON
#define REG_HOMING_MODE      0x051F  // H05.31: homing mode
#define REG_HOMING_HI_SPD    0x0520  // H05.32: high-speed search (rpm)
#define REG_HOMING_LO_SPD    0x0521  // H05.33: low-speed search (rpm)
#define REG_HOMING_ACCEL     0x0522  // H05.34: accel/decel time (ms)
#define REG_HOMING_TIMELIM   0x0523  // H05.35: time limit (ms)
#define REG_HOMING_OFFSET    0x0524  // H05.36: home position offset (=0)
#define REG_HOMING_SPD_THR   0x0538  // H05.56: stall speed threshold (rpm)
#define REG_HOMING_HIT_CNT   0x0539  // H05.57: stall confirm count
#define REG_HOMING_TORQUE    0x053A  // H05.58: torque limit during homing (0.1%)

// H06 — Speed control
#define REG_SPEED_SOURCE     0x0602  // H06.02: 4=Modbus
#define REG_MAX_SPEED        0x0607  // H06.07: max speed limit
#define REG_FWD_SPEED_LIM    0x0608  // H06.08: forward speed limit
#define REG_REV_SPEED_LIM    0x0609  // H06.09: reverse speed limit
#define REG_ACCEL_RAMP       0x0605  // H06.05: accel ramp (ms)
#define REG_DECEL_RAMP       0x0606  // H06.06: decel ramp (ms)

// H0C — Communications
#define REG_VDI_ENABLE       0x0C09  // H0C.09: enable VDI
#define REG_VDI_DEFAULT      0x0C0A  // H0C.10: default VDI level on power-on
#define REG_EEPROM_UPDATE    0x0C0D  // H0C.13: save comms params to EEPROM

// H0d — Commands
#define REG_FAULT_RESET      0x0D01  // H0d.01: write 1 to reset fault

// H17 — Virtual DI
#define REG_VDI1_FUNCTION    0x1700  // H17.00: VDI1 function (1=S-ON)

// H0b — Status (read-only)
#define REG_ACTUAL_SPEED     0x0B00  // H0b.00: actual speed (rpm, signed)
#define REG_AVG_LOAD         0x0B0C  // H0b.12: average load (%)

// H30 — Live status (read-only)
#define REG_STATUS_WORD      0x3000  // H30.00: servo status
#define REG_DO_STATE_1       0x3001  // H30.01: DO function state 1
#define REG_DI_STATUS        0x3004  // H30.04: DI bitmap

// H31 — Communication setpoints
#define REG_VDI_VIRTUAL      0x3100  // H31.00: VDI virtual level (bit0=S-ON)
#define REG_CMD_SPEED_LO     0x3109  // H31.09 lo word: speed cmd (0.001rpm, 32-bit)
#define REG_CMD_SPEED_HI     0x310A  // H31.09 hi word

// HomeAttain = DO5 = bit 4 of H30.01
#define HOME_ATTAIN_BIT  (1 << 15)   // was (1 << 4) — HomeAttain is bit 15 of H30.01

// =============================================================================
// Drive Addresses
// =============================================================================

#define DRIVE_LEFT_ADDR   1
#define DRIVE_RIGHT_ADDR  2

// =============================================================================
// Motion State Machine
// =============================================================================

enum class MotionState {
  IDLE,
  HOMING,
  JUMPING_EXTEND,
  JUMPING_RETRACT,
  MANUAL_MOVING,
  ESTOP
};

// =============================================================================
// Shared State
// =============================================================================

struct SystemState {
  float    posLeftMm      = 0.0f;
  float    posRightMm     = 0.0f;
  bool     homed          = false;
  bool     estopActive    = false;
  bool     faultLeft      = false;
  bool     faultRight     = false;
  int      speedLeftRpm   = 0;
  int      speedRightRpm  = 0;
  uint16_t statusLeft     = 0;
  uint16_t statusRight    = 0;
  uint16_t loadLeft       = 0;
  uint16_t loadRight      = 0;
  MotionState motionState = MotionState::IDLE;
} sysState;

struct PendingCommand {
  bool   valid      = false;
  String cmd        = "";
  float  deltaLeft  = 0.0f;
  float  deltaRight = 0.0f;
} pendingCmd;

portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Globals
// =============================================================================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
ModbusMaster   nodeLeft, nodeRight;

bool leftDriveOnline   = false;
bool rightDriveOnline  = false;
bool rightAcknowledged = false;

uint32_t lastStatusBroadcast = 0;
uint32_t lastModbusPoll      = 0;
uint32_t motionStartMs       = 0;

float manualTargetLeft  = 0.0f;
float manualTargetRight = 0.0f;
float manualDeltaLeft   = 0.0f;  // sign used for done-check direction
float manualDeltaRight  = 0.0f;
//bool jumpSlowApproach = false;

// =============================================================================
// RS485 Direction Control
// =============================================================================

void preTransmission()  { digitalWrite(PIN_RS485_DE_RE, HIGH); }
void postTransmission() { Serial2.flush(); digitalWrite(PIN_RS485_DE_RE, LOW); }

// =============================================================================
// Modbus Helpers
// =============================================================================

void selectDrive(uint8_t addr) {
  if (addr == DRIVE_LEFT_ADDR) nodeLeft.begin(DRIVE_LEFT_ADDR,   Serial2);
  else                          nodeRight.begin(DRIVE_RIGHT_ADDR, Serial2);
  delay(2);
}

bool writeReg(ModbusMaster& node, uint16_t addr, uint16_t val) {
  uint8_t r = node.writeSingleRegister(addr, val);
  delay(60);
  return r == ModbusMaster::ku8MBSuccess;
}

bool readReg(ModbusMaster& node, uint16_t addr, uint16_t& out) {
  uint8_t r = node.readHoldingRegisters(addr, 1);
  delay(40);
  if (r != ModbusMaster::ku8MBSuccess) return false;
  out = node.getResponseBuffer(0);
  return true;
}

bool writeSpeedCmd(ModbusMaster& node, int rpm) {
  int32_t val = (int32_t)rpm * 1000;
  node.setTransmitBuffer(0, (uint16_t)(val & 0xFFFF));
  node.setTransmitBuffer(1, (uint16_t)((val >> 16) & 0xFFFF));
  uint8_t r = node.writeMultipleRegisters(REG_CMD_SPEED_LO, 2);
  delay(60);
  return r == ModbusMaster::ku8MBSuccess;
}

bool setServoEnable(ModbusMaster& node, bool enable) {
  return writeReg(node, REG_VDI_VIRTUAL, enable ? 1 : 0);
}

void clearDriveFault(ModbusMaster& node) {
  setServoEnable(node, false);
  delay(200);
  writeReg(node, REG_FAULT_RESET, 1);
  delay(300);
  writeReg(node, REG_FAULT_RESET, 0);
  delay(200);
}

bool isFault(uint16_t statusWord) {
  return ((statusWord >> 12) & 0x03) == 0x03;
}

bool probeDrive(ModbusMaster& node, uint8_t addr) {
  uint16_t status = 0;
  bool ok = readReg(node, REG_STATUS_WORD, status);
  Serial.printf("[Drive %u] Probe: %s\n", addr, ok ? "OK" : "no response");
  return ok;
}

void stopBothDrives() {
  selectDrive(DRIVE_LEFT_ADDR);
  writeSpeedCmd(nodeLeft, 0);
  setServoEnable(nodeLeft, false);
  if (rightDriveOnline) {
    selectDrive(DRIVE_RIGHT_ADDR);
    writeSpeedCmd(nodeRight, 0);
    setServoEnable(nodeRight, false);
  }
}

// =============================================================================
// Drive Initialisation — Speed Mode
// =============================================================================

bool initDrive(ModbusMaster& node, uint8_t addr) {
  Serial.printf("[Drive %u] Initialising...\n", addr);

  writeReg(node, REG_CONTROL_MODE,   0);   // speed mode
  writeReg(node, REG_SPEED_SOURCE,   4);   // Modbus speed source
  writeReg(node, REG_MAX_SPEED,    500);
  writeReg(node, REG_FWD_SPEED_LIM, 500);
  writeReg(node, REG_REV_SPEED_LIM, 500);
  writeReg(node, REG_ACCEL_RAMP,   500);
  writeReg(node, REG_DECEL_RAMP,   500);

  // Unassign DI5 — prevents E122.5 conflict with VDI1
  writeReg(node, REG_DI5_FUNCTION, 0);
  delay(50);

  // VDI software enable setup
  writeReg(node, REG_VDI_ENABLE,    1);
  writeReg(node, REG_VDI_DEFAULT,   0);   // all VDIs start low on power-on
  writeReg(node, REG_VDI1_FUNCTION, 1);   // VDI1 = S-ON
  writeReg(node, REG_EEPROM_UPDATE, 1);   // save to EEPROM
  delay(500);

  writeSpeedCmd(node, 0);
  setServoEnable(node, false);

  // Diagnostic readback
  uint16_t vdiEn = 0, vdi1 = 0, di5 = 0;
  readReg(node, REG_VDI_ENABLE,    vdiEn);
  readReg(node, REG_VDI1_FUNCTION, vdi1);
  readReg(node, REG_DI5_FUNCTION,  di5);
  Serial.printf("[Drive %u] H0C.09=%u  H17.00=%u  H03.10=%u\n", addr, vdiEn, vdi1, di5);

  uint16_t status = 0;
  if (readReg(node, REG_STATUS_WORD, status)) {
    Serial.printf("[Drive %u] Status: 0x%04X — OK\n", addr, status);
    return true;
  }
  Serial.printf("[Drive %u] ERROR: No status response\n", addr);
  return false;
}

// =============================================================================
// Homing — Native Drive Routine (H05.30 = 4, hit-and-stop)
// =============================================================================

// Configure one drive for native homing and trigger it via S-ON.
// selectDrive() must be called before this.
void configHomingDrive(ModbusMaster& node, uint8_t addr) {
  Serial.printf("[Homing] Configuring drive %u\n", addr);

  clearDriveFault(node);
  setServoEnable(node, false);
  delay(200);

  // Position mode required for homing
  writeReg(node, REG_CONTROL_MODE,  1);

  // Homing parameters
  writeReg(node, REG_HOMING_MODE,   HOMING_MODE_VAL);    // H05.31
  writeReg(node, REG_HOMING_HI_SPD, HOMING_HI_SPEED);   // H05.32
  writeReg(node, REG_HOMING_LO_SPD, HOMING_LO_SPEED);   // H05.33
  writeReg(node, REG_HOMING_ACCEL,  HOMING_ACCEL_MS);   // H05.34
  writeReg(node, REG_HOMING_TIMELIM,HOMING_TIMELIM);    // H05.35
  writeReg(node, REG_HOMING_OFFSET, 0);                 // H05.36: home = 0
  writeReg(node, REG_HOMING_SPD_THR,HOMING_SPD_THR);   // H05.56
  writeReg(node, REG_HOMING_HIT_CNT,HOMING_HIT_CNT);   // H05.57
  writeReg(node, REG_HOMING_TORQUE, HOMING_TORQUE_PCT); // H05.58: 50% torque limit

  // Arm: homing executes immediately when S-ON goes active
  writeReg(node, REG_HOMING_SELECT, 4);

  // S-ON — triggers homing
  setServoEnable(node, true);

  Serial.printf("[Homing] Drive %u homing started\n", addr);
}

// Restore one drive to speed mode after homing completes.
// selectDrive() must be called before this.
void finishHomingDrive(ModbusMaster& node, uint8_t addr) {
  Serial.printf("[Homing] Restoring drive %u to speed mode\n", addr);

  writeReg(node, REG_HOMING_SELECT, 0);  // H05.30 = 0: disable homing (required by docs)
  setServoEnable(node, false);
  delay(150);

  // Return to speed mode
  writeReg(node, REG_CONTROL_MODE,   0);
  writeReg(node, REG_SPEED_SOURCE,   4);
  writeReg(node, REG_MAX_SPEED,    300);
  writeReg(node, REG_FWD_SPEED_LIM, 300);
  writeReg(node, REG_REV_SPEED_LIM, 300);
  writeSpeedCmd(node, 0);

  Serial.printf("[Homing] Drive %u restored to speed mode\n", addr);
}

// =============================================================================
// Motion Transitions
// =============================================================================

void startJump() {
  //jumpSlowApproach = false;   // reset on each new jump
  if (!sysState.homed || sysState.estopActive) return;
  if (!rightDriveOnline && !rightAcknowledged) return;
  Serial.println("[Motion] Jump — extending");
  sysState.motionState = MotionState::JUMPING_EXTEND;
  motionStartMs = millis();

  selectDrive(DRIVE_LEFT_ADDR);
  setServoEnable(nodeLeft, true);
  writeSpeedCmd(nodeLeft, JUMP_EXTEND_SPEED);

  if (rightDriveOnline) {
    selectDrive(DRIVE_RIGHT_ADDR);
    setServoEnable(nodeRight, true);
    writeSpeedCmd(nodeRight, JUMP_EXTEND_SPEED);
  }
}

void startHoming() {
  Serial.println("[Motion] Starting native drive homing");
  sysState.motionState = MotionState::HOMING;
  sysState.homed = false;
  motionStartMs = millis();

  selectDrive(DRIVE_LEFT_ADDR);
  configHomingDrive(nodeLeft, DRIVE_LEFT_ADDR);

  if (rightDriveOnline) {
    selectDrive(DRIVE_RIGHT_ADDR);
    configHomingDrive(nodeRight, DRIVE_RIGHT_ADDR);
  }
}

void triggerEstop() {
  Serial.println("[Motion] E-STOP");
  sysState.motionState = MotionState::ESTOP;
  sysState.estopActive = true;
  sysState.homed = false;
  stopBothDrives();
}

void doManualMove(float deltaLeft, float deltaRight) {
  if (sysState.estopActive || sysState.motionState != MotionState::IDLE) return;

  manualTargetLeft  = constrain(sysState.posLeftMm  + deltaLeft,  0.0f, MAX_TRAVEL_MM);
  manualTargetRight = constrain(sysState.posRightMm + deltaRight, 0.0f, MAX_TRAVEL_MM);
  manualDeltaLeft   = manualTargetLeft  - sysState.posLeftMm;
  manualDeltaRight  = manualTargetRight - sysState.posRightMm;

  if (fabsf(manualDeltaLeft) < 0.5f && fabsf(manualDeltaRight) < 0.5f) return;

  // Positive delta = more extension = negative rpm (extend direction)
  // Negative delta = toward home   = positive rpm (retract direction)
  int spdL = (manualDeltaLeft  > 0) ? -CAL_SPEED :  CAL_SPEED;
  int spdR = (manualDeltaRight > 0) ? -CAL_SPEED :  CAL_SPEED;

  sysState.motionState = MotionState::MANUAL_MOVING;

  selectDrive(DRIVE_LEFT_ADDR);
  setServoEnable(nodeLeft, true);
  writeSpeedCmd(nodeLeft, spdL);

  if (rightDriveOnline) {
    selectDrive(DRIVE_RIGHT_ADDR);
    setServoEnable(nodeRight, true);
    writeSpeedCmd(nodeRight, spdR);
  }
}

// =============================================================================
// Motion Update — ~20Hz
// =============================================================================

void updateMotion() {
  uint32_t now   = millis();
  float    dtSec = (now - lastModbusPoll) / 1000.0f;
  lastModbusPoll = now;

  // ── HOMING: poll HomeAttain (DO5 = bit4 of H30.01) ──────────────────────
  if (sysState.motionState == MotionState::HOMING) {
    selectDrive(DRIVE_LEFT_ADDR);
    uint16_t doStateL = 0, statusL = 0;
    readReg(nodeLeft, REG_DO_STATE_1, doStateL);
    readReg(nodeLeft, REG_STATUS_WORD, statusL);
    sysState.statusLeft = statusL;
    bool leftHomed = (doStateL & HOME_ATTAIN_BIT) != 0;
    bool leftFault = isFault(statusL);

    bool rightHomed = true;
    bool rightFault = false;
    if (rightDriveOnline) {
      selectDrive(DRIVE_RIGHT_ADDR);
      uint16_t doStateR = 0, statusR = 0;
      readReg(nodeRight, REG_DO_STATE_1, doStateR);
      readReg(nodeRight, REG_STATUS_WORD, statusR);
      sysState.statusRight = statusR;
      rightHomed = (doStateR & HOME_ATTAIN_BIT) != 0;
      rightFault = isFault(statusR);
    }

    if (leftFault || (rightDriveOnline && rightFault)) {
      Serial.printf("[Homing] Fault during homing — L:0x%04X\n", statusL);
      selectDrive(DRIVE_LEFT_ADDR);
      finishHomingDrive(nodeLeft, DRIVE_LEFT_ADDR);
      if (rightDriveOnline) {
        selectDrive(DRIVE_RIGHT_ADDR);
        finishHomingDrive(nodeRight, DRIVE_RIGHT_ADDR);
      }
      triggerEstop();
      return;
    }

    if (leftHomed && rightHomed) {
      Serial.println("[Homing] HomeAttain — complete");
      selectDrive(DRIVE_LEFT_ADDR);
      finishHomingDrive(nodeLeft, DRIVE_LEFT_ADDR);
      if (rightDriveOnline) {
        selectDrive(DRIVE_RIGHT_ADDR);
        finishHomingDrive(nodeRight, DRIVE_RIGHT_ADDR);
      }
      sysState.posLeftMm  = 0.0f;
      sysState.posRightMm = 0.0f;
      sysState.homed      = true;
      sysState.motionState = MotionState::IDLE;
      return;
    }

    if (now - motionStartMs > HOMING_SW_TIMEOUT) {
      Serial.println("[Homing] Software timeout — declaring home");
      selectDrive(DRIVE_LEFT_ADDR);
      finishHomingDrive(nodeLeft, DRIVE_LEFT_ADDR);
      if (rightDriveOnline) {
        selectDrive(DRIVE_RIGHT_ADDR);
        finishHomingDrive(nodeRight, DRIVE_RIGHT_ADDR);
      }
      sysState.posLeftMm  = 0.0f;
      sysState.posRightMm = 0.0f;
      sysState.homed      = true;
      sysState.motionState = MotionState::IDLE;
    }
    return;
  }

  // ── All other states: poll speed and status ──────────────────────────────
  selectDrive(DRIVE_LEFT_ADDR);
  uint16_t statusL = 0, rawSpeedL = 0;
  readReg(nodeLeft, REG_STATUS_WORD,  statusL);
  readReg(nodeLeft, REG_ACTUAL_SPEED, rawSpeedL);
  readReg(nodeLeft, REG_AVG_LOAD,     sysState.loadLeft);
  int16_t spdL = (int16_t)rawSpeedL;

  uint16_t statusR = 0;
  int16_t  spdR    = 0;
  if (rightDriveOnline) {
    selectDrive(DRIVE_RIGHT_ADDR);
    uint16_t rawSpeedR = 0;
    readReg(nodeRight, REG_STATUS_WORD,  statusR);
    readReg(nodeRight, REG_ACTUAL_SPEED, rawSpeedR);
    readReg(nodeRight, REG_AVG_LOAD,     sysState.loadRight);
    spdR = (int16_t)rawSpeedR;
  }

  sysState.statusLeft    = statusL;
  sysState.statusRight   = statusR;
  sysState.speedLeftRpm  = spdL;
  sysState.speedRightRpm = spdR;
  sysState.faultLeft     = isFault(statusL);
  sysState.faultRight    = isFault(statusR);

  // Fault check during active motion
  if ((sysState.faultLeft || (rightDriveOnline && sysState.faultRight)) &&
      sysState.motionState != MotionState::IDLE &&
      sysState.motionState != MotionState::ESTOP) {
    Serial.printf("[Motion] Drive fault — L:0x%04X R:0x%04X\n", statusL, statusR);
    triggerEstop();
    return;
  }

  // Software position estimate.
  // Positive rpm = retract = position decreasing toward 0.
  // So position change = -rpmToMmPerSec(rpm) * dt
  if (sysState.motionState != MotionState::IDLE &&
      sysState.motionState != MotionState::ESTOP) {
    float dL = -rpmToMmPerSec(spdL) * dtSec;
    float dR = -rpmToMmPerSec(spdR) * dtSec;
    sysState.posLeftMm  = constrain(sysState.posLeftMm  + dL, 0.0f, MAX_TRAVEL_MM);
    sysState.posRightMm = constrain(sysState.posRightMm + dR, 0.0f, MAX_TRAVEL_MM);
  }

  // ── State transitions ────────────────────────────────────────────────────
  switch (sysState.motionState) {

    case MotionState::JUMPING_EXTEND: {
      // posLeftMm increases as we extend (negative rpm → negative dL above → wait...)
      // Actually: JUMP_EXTEND_SPEED is negative → spdL is negative
      // dL = -rpmToMmPerSec(negative) * dt = positive → posLeftMm increases. Correct.
      bool leftDone  = sysState.posLeftMm  >= JUMP_TARGET_MM;
      bool rightDone = !rightDriveOnline || sysState.posRightMm >= JUMP_TARGET_MM;

      if (leftDone && rightDone) {
        Serial.println("[Motion] Jump target reached — retracting");
        sysState.motionState = MotionState::JUMPING_RETRACT;
        motionStartMs = millis();
        selectDrive(DRIVE_LEFT_ADDR);
        writeSpeedCmd(nodeLeft, JUMP_RETRACT_SPEED);
        if (rightDriveOnline) {
          selectDrive(DRIVE_RIGHT_ADDR);
          writeSpeedCmd(nodeRight, JUMP_RETRACT_SPEED);
        }
      }
      if (sysState.posLeftMm >= MAX_TRAVEL_MM ||
          (rightDriveOnline && sysState.posRightMm >= MAX_TRAVEL_MM)) {
        Serial.println("[Motion] Travel limit — retracting");
        sysState.motionState = MotionState::JUMPING_RETRACT;
        selectDrive(DRIVE_LEFT_ADDR);
        writeSpeedCmd(nodeLeft, JUMP_RETRACT_SPEED);
        if (rightDriveOnline) {
          selectDrive(DRIVE_RIGHT_ADDR);
          writeSpeedCmd(nodeRight, JUMP_RETRACT_SPEED);
        }
      }
      break;
    }

    case MotionState::JUMPING_RETRACT: {
      // When the position estimate reaches ~80mm, stop and hand off
      // to native homing for the final approach. 80mm gives enough
      // buffer for position tracking error while still being well
      // into the retract so the fast phase is doing useful work.
      if (sysState.posLeftMm <= 80.0f) {
        Serial.println("[Motion] Jump retract — handing off to native homing");
        stopBothDrives();
        // Transition directly to native homing
        selectDrive(DRIVE_LEFT_ADDR);
        configHomingDrive(nodeLeft, DRIVE_LEFT_ADDR);
        if (rightDriveOnline) {
          selectDrive(DRIVE_RIGHT_ADDR);
          configHomingDrive(nodeRight, DRIVE_RIGHT_ADDR);
        }
        sysState.motionState = MotionState::HOMING;
        motionStartMs = millis();
        return;
      }
      break;
    }

    case MotionState::MANUAL_MOVING: {
      // Use stored delta sign for direction-aware done check
      bool leftDone  = (manualDeltaLeft  > 0)
                       ? sysState.posLeftMm  >= manualTargetLeft
                       : sysState.posLeftMm  <= manualTargetLeft;
      bool rightDone = !rightDriveOnline ||
                       ((manualDeltaRight > 0)
                        ? sysState.posRightMm >= manualTargetRight
                        : sysState.posRightMm <= manualTargetRight);
      if (leftDone && rightDone) {
        stopBothDrives();
        sysState.posLeftMm  = manualTargetLeft;
        if (rightDriveOnline) sysState.posRightMm = manualTargetRight;
        sysState.motionState = MotionState::IDLE;
      }
      break;
    }

    case MotionState::IDLE:
    case MotionState::ESTOP:
    default:
      break;
  }
}

// =============================================================================
// WebSocket
// =============================================================================

String buildStatusJson() {
  StaticJsonDocument<384> doc;
  doc["type"]              = "status";
  doc["posLeft"]           = (int)sysState.posLeftMm;
  doc["posRight"]          = (int)sysState.posRightMm;
  doc["homed"]             = sysState.homed;
  doc["estop"]             = sysState.estopActive;
  doc["faultLeft"]         = sysState.faultLeft;
  doc["faultRight"]        = sysState.faultRight;
  doc["speedLeft"]         = sysState.speedLeftRpm;
  doc["speedRight"]        = sysState.speedRightRpm;
  doc["loadLeft"]          = sysState.loadLeft;
  doc["loadRight"]         = sysState.loadRight;
  doc["rightOnline"]       = rightDriveOnline;
  doc["rightAcknowledged"] = rightAcknowledged;

  char lStr[8]; snprintf(lStr, 8, "0x%04X", sysState.statusLeft);
  char rStr[8]; snprintf(rStr, 8, "0x%04X", sysState.statusRight);
  doc["statusLeft"]  = lStr;
  doc["statusRight"] = rStr;

  const char* stStr = "idle";
  switch (sysState.motionState) {
    case MotionState::HOMING:          stStr = "homing";        break;
    case MotionState::JUMPING_EXTEND:  stStr = "jump_extend";   break;
    case MotionState::JUMPING_RETRACT: stStr = "jump_retract";  break;
    case MotionState::MANUAL_MOVING:   stStr = "manual_moving"; break;
    case MotionState::ESTOP:           stStr = "estop";         break;
    default: break;
  }
  doc["state"] = stStr;

  String out;
  serializeJson(doc, out);
  return out;
}

void onWebSocketMessage(void* arg, uint8_t* data, size_t len) {
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = '\0';
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, (char*)data)) return;
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    portENTER_CRITICAL(&cmdMux);
    pendingCmd.valid = true;
    pendingCmd.cmd   = String(cmd);
    if (strcmp(cmd, "manual") == 0) {
      float d = doc["delta"] | 0.0f;
      pendingCmd.deltaLeft  = d;
      pendingCmd.deltaRight = d;
    } else if (strcmp(cmd, "cal") == 0) {
      float d          = doc["delta"] | 0.0f;
      const char* side = doc["side"] | "left";
      pendingCmd.deltaLeft  = (strcmp(side, "left")  == 0) ? d : 0.0f;
      pendingCmd.deltaRight = (strcmp(side, "right") == 0) ? d : 0.0f;
    }
    portEXIT_CRITICAL(&cmdMux);
  }
}

void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client %u connected\n", client->id());
      client->text(buildStatusJson());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client %u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      onWebSocketMessage(arg, data, len);
      break;
    default:
      break;
  }
}

// =============================================================================
// Setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Soccer Wall Boot ===");

  pinMode(PIN_RS485_DE_RE, OUTPUT);
  digitalWrite(PIN_RS485_DE_RE, LOW);
  Serial2.begin(57600, SERIAL_8N2, PIN_RS485_RX, PIN_RS485_TX);

  nodeLeft.begin(DRIVE_LEFT_ADDR,   Serial2);
  nodeLeft.preTransmission(preTransmission);
  nodeLeft.postTransmission(postTransmission);

  nodeRight.begin(DRIVE_RIGHT_ADDR, Serial2);
  nodeRight.preTransmission(preTransmission);
  nodeRight.postTransmission(postTransmission);

  delay(500);

  // Probe first — skip full init if drive not responding
  selectDrive(DRIVE_LEFT_ADDR);
  leftDriveOnline = probeDrive(nodeLeft, DRIVE_LEFT_ADDR)
                    ? initDrive(nodeLeft, DRIVE_LEFT_ADDR)
                    : false;
  if (!leftDriveOnline) Serial.println("WARN: Left drive not responding");

  selectDrive(DRIVE_RIGHT_ADDR);
  rightDriveOnline = probeDrive(nodeRight, DRIVE_RIGHT_ADDR)
                     ? initDrive(nodeRight, DRIVE_RIGHT_ADDR)
                     : false;
  if (!rightDriveOnline) Serial.println("WARN: Right drive not responding — single-drive mode");

  rightAcknowledged = rightDriveOnline;

  // WiFi AP
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD[0] ? AP_PASSWORD : nullptr);
  Serial.printf("[WiFi] AP: %s  IP: %s\n", AP_SSID, AP_IP.toString().c_str());

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", SOCCER_WALL_HTML);
  });
  server.begin();

  Serial.println("[HTTP] Server started");
  Serial.println("=== Ready ===");
}

// =============================================================================
// Loop
// =============================================================================

void loop() {
  uint32_t now = millis();

  // Process WebSocket command
  String cmd = "";
  float dLeft = 0, dRight = 0;
  portENTER_CRITICAL(&cmdMux);
  if (pendingCmd.valid) {
    cmd    = pendingCmd.cmd;
    dLeft  = pendingCmd.deltaLeft;
    dRight = pendingCmd.deltaRight;
    pendingCmd.valid = false;
  }
  portEXIT_CRITICAL(&cmdMux);

  if (cmd.length() > 0) {
    Serial.printf("[CMD] %s\n", cmd.c_str());
    if      (cmd == "jump")              startJump();
    else if (cmd == "home")              startHoming();
    else if (cmd == "estop")             triggerEstop();
    else if (cmd == "manual" ||
             cmd == "cal")               doManualMove(dLeft, dRight);
    else if (cmd == "clear_estop") {
      selectDrive(DRIVE_LEFT_ADDR);
      clearDriveFault(nodeLeft);
      if (rightDriveOnline) {
        selectDrive(DRIVE_RIGHT_ADDR);
        clearDriveFault(nodeRight);
      }
      sysState.estopActive = false;
      sysState.faultLeft   = false;
      sysState.faultRight  = false;
      sysState.motionState = MotionState::IDLE;
    }
    else if (cmd == "ack_offline") {
      rightAcknowledged = true;
    }
  }

  if (now - lastModbusPoll > 50) {
    updateMotion();
  }

  if (now - lastStatusBroadcast > 200) {
    ws.cleanupClients();
    if (ws.count() > 0) ws.textAll(buildStatusJson());
    lastStatusBroadcast = now;
  }
}
