// JobRunner.h — streaming d'un job G-code depuis la carte SD vers FluidNC.
//
// Protocole de flux : envoi ligne par ligne avec attente de l'accusé ('ok' /
// 'error:') avant d'envoyer la suivante (« send-response »). Simple et robuste
// sur un ESP32 qui rend déjà la main à LVGL ; suffisant pour la CNC32.
//
// Les commentaires (`;...` et `( ... )`) et lignes vides sont retirés. La
// progression est estimée sur l'offset d'octets lus / la taille du fichier ;
// le numéro de ligne et le total (compté à l'ouverture) alimentent l'écran
// d'usinage ("L1832/3904").
#pragma once

#include <Arduino.h>
#include <SD.h>

#include "fluidnc/FluidNC.h"

class JobRunner {
 public:
  enum class State : uint8_t { Idle, Running, Paused, Done, Error };
  enum class M6State : uint8_t { None, WaitToolChange, WaitProbe };

  void begin(FluidNC* fluid) { _fluid = fluid; }

  // Ouvre `path` et démarre le streaming. Renvoie false si l'ouverture échoue
  // ou si un job est déjà en cours.
  bool start(const char* path);

  // À appeler très souvent depuis loop() : avance le streaming selon les accusés.
  void pump();

  void pause();    // feed hold + suspension de l'envoi
  void resume();   // cycle start + reprise
  void stop();     // soft-reset FluidNC + fermeture du fichier

  State    state()    const { return _state; }
  M6State  m6State()  const { return _m6State; }
  void     setM6State(M6State s) { _m6State = s; }
  bool     probeRunningForM6() const { return _probeRunningForM6; }
  void     setProbeRunningForM6(bool v) { 
    _probeRunningForM6 = v; 
    if (v) _probeStartMs = millis();
  }
  uint32_t probeStartMs() const { return _probeStartMs; }
  
  bool     isActive() const { return _state == State::Running || _state == State::Paused; }
  bool     isRunning() const { return _state == State::Running; }
  bool     isPaused()  const { return _state == State::Paused; }
  uint32_t pauseStartMs() const { return _pauseStartMs; }

  uint8_t  progressPercent() const;
  uint32_t lineNumber() const { return _line; }
  uint32_t totalLines() const { return _totalLines; }
  uint32_t elapsedMs()  const;
  const char* name()    const { return _name; }

 private:
  bool readNextCommand(String& out);   // lit/épure la prochaine ligne utile
  uint32_t countLines(File& f);

  FluidNC* _fluid = nullptr;
  File     _file;
  State    _state = State::Idle;

  size_t   _size = 0;
  uint32_t _line = 0;
  uint32_t _totalLines = 0;

  static constexpr uint8_t MAX_IN_FLIGHT_LINES = 16;
  static constexpr int     MAX_BUFFER_BYTES = 120;
  uint8_t  _lineLengths[MAX_IN_FLIGHT_LINES];
  uint8_t  _head = 0;
  uint8_t  _tail = 0;
  int      _bytesInFlight = 0;

  String   _nextCmd;
  String   _macroQueue[8];
  uint8_t  _macroHead = 0;
  uint8_t  _macroTail = 0;
  bool     _pendingM6 = false;
  bool     _probeRunningForM6 = false;
  uint32_t _probeStartMs = 0;
  M6State  _m6State = M6State::None;

  uint32_t _startMs = 0;
  uint32_t _pausedAccumMs = 0;
  uint32_t _pauseStartMs = 0;
  char     _name[40] = {0};
};
