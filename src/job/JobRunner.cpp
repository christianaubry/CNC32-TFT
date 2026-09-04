// JobRunner.cpp — streaming de jobs G-code depuis la SD (send-response).
#include "job/JobRunner.h"

#include "gcode/GcodeThumb.h"  // gthumb::isGcodeFile (validation d'extension)

bool JobRunner::start(const char* path) {
  if (isActive() || _fluid == nullptr) return false;

  _file = SD.open(path, FILE_READ);
  if (!_file) {
    _state = State::Error;
    return false;
  }

  _size = _file.size();
  _totalLines = countLines(_file);   // une passe, puis on revient au début
  _file.seek(0);

  // Nom court pour l'affichage (sans le chemin).
  const char* slash = strrchr(path, '/');
  const char* base  = slash ? slash + 1 : path;
  strncpy(_name, base, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = '\0';

  _line = 0;
  _head = _tail = _bytesInFlight = 0;
  _pendingM6 = false;
  _macroHead = _macroTail = 0;
  _nextCmd = "";
  _startMs = millis();
  _pausedAccumMs = 0;
  _state = State::Running;

  // On part d'un override d'avance à 100 %.
  _fluid->feedOverrideReset();
  return true;
}

void JobRunner::pump() {
  if (_state != State::Running) return;

  // 1) Dépiler les accusés reçus (ok / error)
  bool err = false;
  while (_fluid->takeAck(&err)) {
    if (err) {
      // Erreur GRBL : on stoppe par sécurité et on signale.
      Serial.printf("JobRunner: Erreur GRBL reçue ! Arrêt de l'usinage à la ligne %lu.\n", _line);
      _fluid->feedHold();
      _file.close();
      _state = State::Error;
      return;
    }
    if (_head != _tail) {
      _bytesInFlight -= _lineLengths[_tail];
      _tail = (_tail + 1) % MAX_IN_FLIGHT_LINES;
    }
  }

  // 2) Remplir le buffer GRBL
  while (true) {
    uint8_t nextHead = (_head + 1) % MAX_IN_FLIGHT_LINES;
    if (nextHead == _tail) break; // file pleine (en nb de lignes)

    if (_nextCmd.length() == 0) {
      if (_macroTail < _macroHead) {
        // Il reste des lignes de la macro à envoyer
        _nextCmd = _macroQueue[_macroTail++];
        if (_macroTail == _macroHead) {
          _macroTail = _macroHead = 0;
        }
      } else {
        if (_pendingM6) {
          // La macro complète de changement d'outil a été envoyée
          _pendingM6 = false;
          _m6State = M6State::WaitToolChange;
          _state = State::Paused;
          _pauseStartMs = millis();
          break; // Sort du remplissage
        }

        if (!readNextCommand(_nextCmd)) {
          // Fin du fichier. Le job est terminé seulement si tout est acquitté.
          if (_bytesInFlight == 0) {
            _file.close();
            _state = State::Done;
          }
          break;
        }

        // Phase 4 : détection de M6
        if (_nextCmd.indexOf("M6") >= 0 || _nextCmd.indexOf("m6") >= 0) {
          int t = 0;
          _macroQueue[t++] = "G53 G0 Z0"; // 1. Remonte Z à zéro (machine)
          _macroQueue[t++] = "M5";        // 2. Arrête la rotation de la fraise
          _macroQueue[t++] = "G4 P2.0";   // 3. Attend 2 secondes
          
          if (_fluid->hasToolChangePos()) {
            const float* tcp = _fluid->toolChangePos();
            char b[64];
            snprintf(b, sizeof(b), "G53 G0 X%.3f", tcp[0]); // 4. Déplace X
            _macroQueue[t++] = b;
            snprintf(b, sizeof(b), "G53 G0 Y%.3f", tcp[1]); // 5. Déplace Y
            _macroQueue[t++] = b;
          } else {
            _macroQueue[t++] = "G53 G0 X0";
            _macroQueue[t++] = "G53 G0 Y0";
          }
          
          _macroQueue[t++] = _nextCmd;  // 6. Envoie la commande originale (M6)
          _macroHead = t;
          _macroTail = 0;
          _nextCmd = _macroQueue[_macroTail++];
          _pendingM6 = true;
        }
      }
    }

    int cmdLen = _nextCmd.length() + 1; // +1 pour '\n'

    // Si on dépasse le buffer série, on arrête pour ce tour, SAUF si le buffer est vide
    if (_bytesInFlight > 0 && (_bytesInFlight + cmdLen > MAX_BUFFER_BYTES)) {
      break;
    }

    Serial.printf("JobRunner: Envoi L%lu (inFlight: %d + %d = %d): %s\n", _line, _bytesInFlight, cmdLen, _bytesInFlight + cmdLen, _nextCmd.c_str());
    _fluid->sendLine(_nextCmd);
    _lineLengths[_head] = cmdLen;
    _head = nextHead;
    _bytesInFlight += cmdLen;
    _line++;
    
    _nextCmd = "";
  }
}

void JobRunner::pause() {
  if (_state != State::Running) return;
  _fluid->feedHold();
  _pauseStartMs = millis();
  _state = State::Paused;
}

void JobRunner::resume() {
  if (_state != State::Paused) return;
  
  if (_m6State == M6State::WaitProbe) {
    // La machine était arrêtée, le buffer GRBL est vide.
    // On réinitialise notre suivi pour ignorer les 'ok' perdus ou excédentaires.
    _head = _tail = _bytesInFlight = 0;

    _macroQueue[0] = "G53 G0 Z0";
    _macroQueue[1] = "G90";
    _macroQueue[2] = "G0 Y0";
    _macroQueue[3] = "G0 X0";
    _macroQueue[4] = "M3";       // Redémarre la broche
    _macroQueue[5] = "G4 P3.0";  // Attend 3 secondes que la broche prenne ses tours
    _macroHead = 6;
    _macroTail = 0;
    _m6State = M6State::None;
  }
  
  _fluid->cycleStart();
  _pausedAccumMs += millis() - _pauseStartMs;
  _state = State::Running;
}

void JobRunner::stop() {
  if (!isActive()) return;
  _fluid->reset();        // soft-reset : vide le planner GRBL
  if (_file) _file.close();
  _head = _tail = _bytesInFlight = 0;
  _pendingM6 = false;
  _probeRunningForM6 = false;
  _m6State = M6State::None;
  _macroHead = _macroTail = 0;
  _nextCmd = "";
  _state = State::Idle;
}

uint8_t JobRunner::progressPercent() const {
  if (_size == 0) return 0;
  // _file.position() avance au fil de la lecture ; borne à 100.
  size_t pos = _file ? _file.position() : _size;
  uint32_t pct = (uint32_t)((uint64_t)pos * 100u / _size);
  return pct > 100 ? 100 : (uint8_t)pct;
}

uint32_t JobRunner::elapsedMs() const {
  if (_state == State::Idle) return 0;
  uint32_t now = (_state == State::Paused) ? _pauseStartMs : millis();
  return now - _startMs - _pausedAccumMs;
}

uint32_t JobRunner::countLines(File& f) {
  uint32_t n = 0;
  uint8_t buf[512];
  while (f.available()) {
    int bytes = f.read(buf, sizeof(buf));
    for (int i = 0; i < bytes; ++i) {
      if (buf[i] == '\n') n++;
    }
  }
  return n;
}

// Lit la prochaine ligne non vide/non commentaire. Renvoie false en fin de
// fichier (aucune commande restante).
bool JobRunner::readNextCommand(String& out) {
  while (_file && _file.available()) {
    char buf[256];
    int len = 0;
    while (_file.available() && len < 255) {
      char c = _file.read();
      if (c == '\n') break;
      if (c != '\r') buf[len++] = c;
    }
    buf[len] = '\0';
    
    // Inline comment stripping
    char cmdBuf[256];
    int cmdLen = 0;
    bool inParen = false;
    for (int i = 0; i < len; ++i) {
      char c = buf[i];
      if (c == ';') break; // end of line comment
      if (c == '(') { inParen = true; continue; }
      if (c == ')') { inParen = false; continue; }
      if (!inParen) cmdBuf[cmdLen++] = c;
    }
    cmdBuf[cmdLen] = '\0';
    
    // Trim right space
    while (cmdLen > 0 && isspace(cmdBuf[cmdLen - 1])) cmdLen--;
    cmdBuf[cmdLen] = '\0';
    
    // Trim left space
    int start = 0;
    while (start < cmdLen && isspace(cmdBuf[start])) start++;
    
    if (cmdLen > start) {
      out = &cmdBuf[start];
      return true;
    }
  }
  return false;
}
