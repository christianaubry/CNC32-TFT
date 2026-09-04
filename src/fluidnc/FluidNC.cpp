// FluidNC.cpp — implémentation du dialogue GRBL/FluidNC sur UART (v2).
#include "FluidNC.h"
#include "board_config.h"

#include <Preferences.h>

namespace {
const char AXIS_LETTER[3] = {'X', 'Y', 'Z'};

// Octets temps réel GRBL pour les overrides d'avance.
constexpr uint8_t RT_FEED_RESET   = 0x90;  // 100 %
constexpr uint8_t RT_FEED_PLUS10  = 0x91;
constexpr uint8_t RT_FEED_MINUS10 = 0x92;
constexpr uint8_t RT_FEED_PLUS1   = 0x93;
constexpr uint8_t RT_FEED_MINUS1  = 0x94;

MachineState parseState(const String& s) {
  // s = premier champ du rapport, ex "Idle", "Run", "Jog", "Alarm", "Hold:0"...
  if (s.startsWith("Idle"))  return MachineState::Idle;
  if (s.startsWith("Run"))   return MachineState::Run;
  if (s.startsWith("Jog"))   return MachineState::Jog;
  if (s.startsWith("Hold"))  return MachineState::Hold;
  if (s.startsWith("Alarm")) return MachineState::Alarm;
  if (s.startsWith("Door"))  return MachineState::Door;
  if (s.startsWith("Check")) return MachineState::Check;
  if (s.startsWith("Home"))  return MachineState::Home;
  if (s.startsWith("Sleep")) return MachineState::Sleep;
  return MachineState::Unknown;
}

// Remplit jusqu'à 3 flottants depuis "a,b,c". Retourne le nombre extrait.
int parseVec3(const String& csv, float* out) {
  int n = 0, start = 0;
  while (n < 3 && start <= csv.length()) {
    int comma = csv.indexOf(',', start);
    String tok = (comma < 0) ? csv.substring(start) : csv.substring(start, comma);
    out[n++] = tok.toFloat();
    if (comma < 0) break;
    start = comma + 1;
  }
  return n;
}

Preferences s_prefs;  // espace de noms NVS "cnc32"
}  // namespace

void FluidNC::begin(HardwareSerial& serial) {
  _serial = &serial;
  _serial->begin(FLUIDNC_BAUD, SERIAL_8N1, PIN_FLUIDNC_RX, PIN_FLUIDNC_TX);
  _rx.reserve(128);
  loadToolChangePos();   // position de changement d'outil persistée
  loadWorkZero();        // masque d'origine pièce persisté
}

void FluidNC::update() {
  if (!_serial) return;
  while (_serial->available()) {
    char c = (char)_serial->read();
    if (c == '\n' || c == '\r') {
      if (_rx.length()) { parseLine(_rx); _rx = ""; }
    } else {
      _rx += c;
      if (_rx.length() > 200) _rx = "";  // garde-fou anti-débordement
    }
  }
}

void FluidNC::parseLine(const String& line) {
  if (line.length() == 0) return;

  if (line[0] == '<' && line.endsWith(">")) {
    parseStatusReport(line.substring(1, line.length() - 1));
    return;
  }

  // Accusés du streaming de jobs.
  if (line == "ok") {
    if (_ackCount < 255) _ackCount++;
    _ackError   = false;
    return;
  }
  if (line.startsWith("error:")) {
    Serial.printf("FluidNC RX: %s\n", line.c_str());
    if (_ackCount < 255) _ackCount++;
    _ackError   = true;
    return;
  }
  // 'ALARM:x' : la machine bascule en Alarm ; les flags seront invalidés au
  // prochain rapport d'état. '[MSG:...]' : ignoré.
  if (line.startsWith("ALARM")) {
    _homed = false;
    _newStatus = true; // Force l'UI à se mettre à jour immédiatement
  }
  if (line.startsWith("[PRB:")) {
    if (_expectedProbes > 0) {
      _probesDone++;
      if (_probesDone >= _expectedProbes) {
        _probeJustFinished = true;
        _expectedProbes = 0;
      }
    } else {
      _probeJustFinished = true;
    }
  }
}

void FluidNC::parseStatusReport(const String& body) {
  // body = "State|Field:val|Field:val|..."
  bool haveWpos = false;
  int start = 0;
  bool first = true;
  MachineState prevState = _status.state;

  while (start <= body.length()) {
    int bar = body.indexOf('|', start);
    String field = (bar < 0) ? body.substring(start) : body.substring(start, bar);

    if (first) {
      _status.state = parseState(field);
      first = false;
    } else {
      int colon = field.indexOf(':');
      if (colon > 0) {
        String key = field.substring(0, colon);
        String val = field.substring(colon + 1);
        if (key == "MPos") {
          parseVec3(val, _status.mpos);
        } else if (key == "WPos") {
          parseVec3(val, _status.wpos);
          haveWpos = true;
        } else if (key == "WCO") {
          float new_wco[3];
          parseVec3(val, new_wco);
          if (!_workZeroRestored) {
            _workZeroRestored = true;
            if (_wzMask > 0) {
              char buf[64];
              snprintf(buf, sizeof(buf), "G10 L2 P1 X%.3f Y%.3f Z%.3f", _wco[0], _wco[1], _wco[2]);
              sendLine(buf);
            } else {
              _wco[0] = new_wco[0]; _wco[1] = new_wco[1]; _wco[2] = new_wco[2];
            }
          } else {
            if (fabs(_wco[0] - new_wco[0]) > 0.001f || fabs(_wco[1] - new_wco[1]) > 0.001f || fabs(_wco[2] - new_wco[2]) > 0.001f) {
              _wco[0] = new_wco[0]; _wco[1] = new_wco[1]; _wco[2] = new_wco[2];
              saveWorkZero();
            }
          }
        } else if (key == "FS") {       // feed,spindle
          float fs[3] = {0, 0, 0};
          parseVec3(val, fs);
          _status.feed = fs[0];
          _status.spindle = fs[1];
        } else if (key == "F") {        // certains rapports n'ont que le feed
          _status.feed = val.toFloat();
        }
      }
    }
    if (bar < 0) break;
    start = bar + 1;
  }

  // Si seule la MPos est rapportée, déduire la WPos via le dernier WCO connu.
  if (!haveWpos) {
    for (int i = 0; i < 3; ++i) _status.wpos[i] = _status.mpos[i] - _wco[i];
  } else {
    for (int i = 0; i < 3; ++i) _status.mpos[i] = _status.wpos[i] + _wco[i];
  }

  // Validation du homing : $H envoyé puis retour à Idle sans alarme.
  if (_homePending && _status.state == MachineState::Idle &&
      prevState != MachineState::Idle) {
    _homed = true;
    _homePending = false;
  }
  if (_status.state == MachineState::Alarm) {
    _homePending = false;
    _homed = false;
  }

  _newStatus = true;
}

bool FluidNC::hasNewStatus() {
  if (_newStatus) { _newStatus = false; return true; }
  return false;
}

bool FluidNC::takeAck(bool* isError) {
  if (_ackCount == 0) return false;
  _ackCount--;
  if (isError) *isError = _ackError;
  return true;
}

void FluidNC::requestStatus() {
  if (_serial) _serial->write('?');
}

void FluidNC::jog(uint8_t axis, float dist, float feedMmMin) {
  if (axis > 2) return;
  char buf[48];
  // $J=G91 G21 <axis><dist> F<feed>  (relatif, millimètres)
  snprintf(buf, sizeof(buf), "$J=G91 G21 %c%.3f F%.0f",
           AXIS_LETTER[axis], dist, feedMmMin);
  sendLine(buf);
}

void FluidNC::jogCancel() {
  if (_serial) _serial->write((uint8_t)0x85);
}

void FluidNC::zeroAxis(uint8_t axis) {
  if (axis > 2) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "G10 L20 P0 %c0", AXIS_LETTER[axis]);
  sendLine(buf);
  _wzMask |= (1u << axis);
  saveWorkZero();
}

void FluidNC::zeroAll() {
  sendLine("G10 L20 P0 X0 Y0");
  _wzMask |= 0x03; // Ajoute X et Y au masque (sans écraser Z si déjà palpé)
  saveWorkZero();
}

void FluidNC::selectWCS(uint8_t wcs) {
  if (wcs > 5) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "G%d", 54 + wcs);
  sendLine(buf);
  _status.wcs = wcs;
}

void FluidNC::home()        { sendLine("$H"); _homePending = true; }
void FluidNC::unlock() {
  if (_serial) {
    _serial->write((uint8_t)0x85); // Jog Cancel (débloque l'état Jog fantôme)
    delay(50);
    _serial->write((uint8_t)0x18); // Soft Reset 1
    delay(500); // Wait for GRBL to reboot
    sendLine("$X");
    delay(500);
    _serial->write((uint8_t)0x18); // Soft Reset 2 (demandé par l'utilisateur)
  }
  _homed = false; // Invalide le homing
}

void FluidNC::reset() {
  if (_serial) _serial->write((uint8_t)0x18);
  // Le soft-reset invalide la référence machine. L'origine pièce est mémorisée.
  _homed = false;
  _homePending = false;
}

void FluidNC::feedHold()    { if (_serial) _serial->write('!'); }
void FluidNC::cycleStart()  { if (_serial) _serial->write('~'); }

void FluidNC::probeZ(float plateThickness, float fastFeed, float slowFeed, float maxDepth) {
  _expectedProbes = 2;
  _probesDone = 0;
  _probeJustFinished = false;

  char buf[48];
  sendLine("G91");                                   // relatif
  
  // Phase 1 : Descente rapide
  snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxDepth, fastFeed);
  sendLine(buf);                                     // descente jusqu'au contact
  
  // Petit retrait
  sendLine("G0 Z2.0");
  
  // Phase 2 : Descente lente
  snprintf(buf, sizeof(buf), "G38.2 Z-3.0 F%.0f", slowFeed);
  sendLine(buf);                                     // redescente lente
  
  sendLine("G90");                                   // absolu
  snprintf(buf, sizeof(buf), "G10 L20 P0 Z%.3f", plateThickness);
  sendLine(buf);                                     // Z pièce = épaisseur palpeur
  _wzMask |= (1u << 2);                              // l'axe Z est désormais référencé
  saveWorkZero();
  sendLine("G91");                                   // remontée de sécurité
  sendLine("G0 Z5");
  sendLine("G90");
}

void FluidNC::feedOverrideAdd(int delta) {
  if (!_serial) return;
  switch (delta) {
    case  10: _serial->write(RT_FEED_PLUS10);  break;
    case -10: _serial->write(RT_FEED_MINUS10); break;
    case   1: _serial->write(RT_FEED_PLUS1);   break;
    case  -1: _serial->write(RT_FEED_MINUS1);  break;
    default: break;  // valeur non supportée
  }
}

void FluidNC::feedOverrideReset() {
  if (_serial) _serial->write(RT_FEED_RESET);
}

void FluidNC::spindleStop() { sendLine("M5"); }

// --- Position de changement d'outil ----------------------------------------
void FluidNC::setToolChangePos() {
  for (int i = 0; i < 3; ++i) _tcp[i] = _status.mpos[i];
  _tcpValid = true;
  saveToolChangePos();
}

void FluidNC::clearToolChangePos() {
  _tcpValid = false;
  _tcp[0] = _tcp[1] = _tcp[2] = 0;
  saveToolChangePos();
}

void FluidNC::gotoToolChangePos(float feedMmMin) {
  if (!_tcpValid) return;
  char buf[48];
  
  float target_z = _tcp[2];
  float target_x = _tcp[0];
  float target_y = _tcp[1];

  if (_homed) {
    if (target_z > 0.0f) target_z = 0.0f;
    if (target_x < 0.0f) target_x = 0.0f;
    if (target_y < 0.0f) target_y = 0.0f;
  }

  // Coordonnées machine (G53). On lève Z à 0 en premier pour éviter toute collision.
  sendLine("G53 G0 Z0");
  sendLine("M5");        // Arrête la rotation de la fraise
  sendLine("G4 P2.0");   // Attend 2 secondes
  snprintf(buf, sizeof(buf), "G53 G0 X%.3f", target_x);
  sendLine(buf);
  snprintf(buf, sizeof(buf), "G53 G0 Y%.3f", target_y);
  sendLine(buf);
  (void)feedMmMin;  // déplacement rapide G0 ; feed conservé pour API symétrique
}

void FluidNC::loadToolChangePos() {
  s_prefs.begin("cnc32", true /*readOnly*/);
  _tcpValid = s_prefs.getBool("tcpValid", false);
  if (_tcpValid) {
    _tcp[0] = s_prefs.getFloat("tcpX", 0);
    _tcp[1] = s_prefs.getFloat("tcpY", 0);
    _tcp[2] = s_prefs.getFloat("tcpZ", 0);
  }
  s_prefs.end();
}

void FluidNC::saveToolChangePos() {
  s_prefs.begin("cnc32", false /*readWrite*/);
  s_prefs.putBool("tcpValid", _tcpValid);
  s_prefs.putFloat("tcpX", _tcp[0]);
  s_prefs.putFloat("tcpY", _tcp[1]);
  s_prefs.putFloat("tcpZ", _tcp[2]);
  s_prefs.end();
}

void FluidNC::loadWorkZero() {
  s_prefs.begin("cnc32", true /*readOnly*/);
  _wzMask = s_prefs.getUChar("wzMask", 0);
  _wco[0] = s_prefs.getFloat("wcoX", 0.0f);
  _wco[1] = s_prefs.getFloat("wcoY", 0.0f);
  _wco[2] = s_prefs.getFloat("wcoZ", 0.0f);
  s_prefs.end();
}

void FluidNC::saveWorkZero() {
  s_prefs.begin("cnc32", false /*readWrite*/);
  s_prefs.putUChar("wzMask", _wzMask);
  s_prefs.putFloat("wcoX", _wco[0]);
  s_prefs.putFloat("wcoY", _wco[1]);
  s_prefs.putFloat("wcoZ", _wco[2]);
  s_prefs.end();
}

void FluidNC::sendLine(const String& line) {
  if (!_serial) return;
  _serial->print(line);
  _serial->write('\n');
}
