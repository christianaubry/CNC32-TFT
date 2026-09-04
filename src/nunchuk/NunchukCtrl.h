#pragma once

#include <Arduino.h>
#include "fluidnc/FluidNC.h"

class NunchukCtrl {
public:
  void begin();
  void update(FluidNC* fluid);

private:
  uint32_t _lastRead = 0;
  bool _isJogging = false;
  uint8_t _unacked = 0;
  uint8_t _readBuf[6];

  bool readData();
  void initNunchuk();
};
