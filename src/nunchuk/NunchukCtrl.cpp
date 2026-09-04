#include "NunchukCtrl.h"
#include <Wire.h>
#include "board_config.h"
#include "ui/ui.h" // For g_nunchukSensitivity

static const uint8_t NUNCHUK_ADDR = 0x52;
static const int DEADZONE = 15; // User requested deadzone
static const float MAX_FEED = 2000.0f; // Max jog feed mm/min

void NunchukCtrl::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  initNunchuk();
}

void NunchukCtrl::initNunchuk() {
  Wire.beginTransmission(NUNCHUK_ADDR);
  Wire.write(0xF0);
  Wire.write(0x55);
  Wire.endTransmission();
  delay(1);
  
  Wire.beginTransmission(NUNCHUK_ADDR);
  Wire.write(0xFB);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(1);
}

bool NunchukCtrl::readData() {
  Wire.beginTransmission(NUNCHUK_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  
  delay(1);
  
  Wire.requestFrom(NUNCHUK_ADDR, (uint8_t)6);
  int i = 0;
  while (Wire.available() && i < 6) {
    _readBuf[i++] = Wire.read();
  }
  
  return i == 6;
}

void NunchukCtrl::update(FluidNC* fluid) {
  uint32_t now = millis();
  if (now - _lastRead < 50) return; // 20 Hz read rate
  _lastRead = now;

  // Only jog if the machine is ready to move manually
  MachineState state = fluid->status().state;
  if (state != MachineState::Idle && state != MachineState::Jog) {
    if (_isJogging) {
      fluid->jogCancel();
      _isJogging = false;
      _unacked = 0;
    }
    return;
  }

  // Consume acks to track unacknowledged commands in the serial buffer
  while (fluid->takeAck(nullptr)) {
    if (_unacked > 0) _unacked--;
  }

  if (!readData()) {
    // Attempt re-init if read fails
    initNunchuk();
    return;
  }

  int joyX = _readBuf[0];
  int joyY = _readBuf[1];

  // If we read all 0xFF, the I2C read probably failed or nunchuk disconnected
  if (joyX == 255 && joyY == 255) {
    if (_isJogging) {
      fluid->jogCancel();
      _isJogging = false;
      _unacked = 0;
    }
    initNunchuk();
    return;
  }
  
  // Calculate deviation from center (127)
  int devX = joyX - 127;
  int devY = joyY - 127;

  // Apply deadzone
  if (abs(devX) < DEADZONE) devX = 0;
  if (abs(devY) < DEADZONE) devY = 0;

  if (devX == 0 && devY == 0) {
    if (_isJogging) {
      // Joystick released, cancel jog
      fluid->jogCancel();
      _isJogging = false;
      _unacked = 0;
    }
    return;
  }

  // Flow control: keep maximum 3 commands unacknowledged
  if (_unacked >= 3) return;

  // Determine which axis has the largest deflection to avoid diagonal jogging 
  // (which is harder to control safely with GRBL relative jogs).
  bool isX = abs(devX) > abs(devY);
  int activeDev = isX ? devX : devY;
  uint8_t axis = isX ? 0 : 1;
  float sign = activeDev > 0 ? 1.0f : -1.0f;

  // Calculate feed based on sensitivity slider (0-100) and deflection
  // dev range is DEADZONE to ~128.
  float magnitude = (float)(abs(activeDev) - DEADZONE) / (128.0f - DEADZONE);
  if (magnitude > 1.0f) magnitude = 1.0f;
  
  float sensMultiplier = g_nunchukSensitivity / 100.0f;
  float feed = MAX_FEED * magnitude * sensMultiplier;
  if (feed < 10.0f) feed = 10.0f;

  // Send small jog commands (0.1s of travel) to keep the planner continuously 
  // fed. This allows smooth dynamic speed changes and stops instantly on cancel.
  float dist = (feed / 60.0f) * 0.1f;
  if (dist < 0.01f) dist = 0.01f;

  fluid->jog(axis, sign * dist, feed);
  _isJogging = true;
  _unacked++;
}
