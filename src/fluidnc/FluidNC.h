// FluidNC.h — couche de communication avec une carte FluidNC (GRBL) en UART.
//
// Protocole : commandes G-code/GRBL terminées par '\n', commandes temps réel
// d'un seul octet (status '?', jog cancel 0x85, overrides 0x90..0x9F), rapports
// d'état encadrés par '<' '>' poussés par FluidNC
// (ex: <Idle|MPos:0.000,0.000,0.000|FS:0,0|...>).
//
// v2 : ajoute le suivi d'état de session (homing fait / zéro pièce défini), la
// mémorisation persistante (NVS) de la position de changement d'outil, le
// palpage Z, les overrides d'avance temps réel et le suivi des accusés
// 'ok'/'error' nécessaires au streaming de jobs (voir job/JobRunner).
#pragma once

#include <Arduino.h>

enum class MachineState : uint8_t {
  Unknown, Idle, Run, Hold, Jog, Alarm, Door, Check, Home, Sleep
};

struct MachineStatus {
  MachineState state = MachineState::Unknown;
  float wpos[3] = {0, 0, 0};   // position pièce (Work) X Y Z
  float mpos[3] = {0, 0, 0};   // position machine X Y Z
  float feed    = 0;
  float spindle = 0;
  uint8_t wcs   = 0;           // 0 = G54, 1 = G55, ... (peut rester 0 si non rapporté)
};

class FluidNC {
 public:
  // Démarre la liaison série (configure RX/TX et baud depuis board_config.h) et
  // recharge la position de changement d'outil persistée en NVS.
  void begin(HardwareSerial& serial);

  // À appeler très souvent : consomme les octets reçus et met à jour l'état.
  void update();

  // Demande un rapport d'état immédiat (octet temps réel '?').
  void requestStatus();

  // --- Mouvement -----------------------------------------------------------
  // Jog relatif d'un axe (0=X,1=Y,2=Z) de `dist` mm au feed `feedMmMin`.
  void jog(uint8_t axis, float dist, float feedMmMin);
  // Annule le jog en cours (octet temps réel 0x85).
  void jogCancel();

  // --- Origines / zéro -----------------------------------------------------
  void zeroAxis(uint8_t axis);   // met l'axe courant à 0 dans le WCS actif
  void zeroAll();                // X=Y=Z=0 (définit l'origine pièce)
  void selectWCS(uint8_t wcs);   // 0..5 -> G54..G59

  // --- Cycles --------------------------------------------------------------
  void home();                   // $H
  void unlock();                 // $X (sort de l'alarme)
  void reset();                  // soft-reset (Ctrl-X, 0x18) — réinitialise les flags de session
  void feedHold();               // '!'
  void cycleStart();             // '~'

  // --- Palpage Z -----------------------------------------------------------
  // Descend en G38.2 jusqu'au contact (rapide), remonte légèrement, puis
  // redescend (lent), fixe le Z pièce à `plateThickness`
  // (épaisseur du palpeur), puis remonte de 5 mm. `maxDepth` borne la descente.
  void probeZ(float plateThickness, float fastFeed, float slowFeed, float maxDepth = 25.0f);

  // --- Position de changement d'outil (coord. machine, persistante NVS) -----
  bool         hasToolChangePos() const { return _tcpValid; }
  const float* toolChangePos()    const { return _tcp; }     // 3 floats machine
  void setToolChangePos();        // capture la MPos courante + persiste en NVS
  void clearToolChangePos();      // efface (NVS compris)
  void gotoToolChangePos(float feedMmMin);  // déplacement G53 (Z d'abord)

  // --- Overrides d'avance (temps réel) -------------------------------------
  // delta = +10 / -10 / +1 / -1 (autres valeurs ignorées). Voir bytes GRBL.
  void feedOverrideAdd(int delta);
  void feedOverrideReset();       // retour à 100 % (0x90)

  // --- Broche (utilisée par les macros / lignes de job) --------------------
  void spindleStop();             // M5

  // --- Envoi brut ----------------------------------------------------------
  // Envoie une ligne G-code/commande brute (un '\n' est ajouté).
  void sendLine(const String& line);

  // --- État / session ------------------------------------------------------
  const MachineStatus& status() const { return _status; }
  // true une fois si un nouveau rapport d'état a été reçu depuis le dernier appel.
  bool hasNewStatus();

  // Vrai si un homing a été réalisé depuis le dernier boot/soft-reset.
  bool isHomed() const { return _homed; }
  // Vrai si l'origine pièce (X,Y) a été définie depuis le dernier boot/reset. (Z n'est pas obligatoire pour aller à l'origine XY)
  bool isWorkZeroSet() const { return (_wzMask & 0x03) == 0x03; }

  // Indique si un palpage G38 vient de se terminer avec succès
  bool probeJustFinished() const { return _probeJustFinished; }
  void clearProbeJustFinished() { _probeJustFinished = false; }

  // --- Gestion du streaming ------------------------------------------------

  // Renvoie true une seule fois lorsqu'un 'ok' ou 'error:' a été reçu ;
  // *isError indique s'il s'agissait d'une erreur. Consommé par JobRunner.
  bool takeAck(bool* isError = nullptr);

 private:
  void parseLine(const String& line);
  void parseStatusReport(const String& body);  // contenu entre '<' et '>'
  void loadToolChangePos();
  void saveToolChangePos();
  void loadWorkZero();
  void saveWorkZero();

  HardwareSerial* _serial = nullptr;
  String   _rx;
  MachineStatus _status;
  float    _wco[3] = {0, 0, 0};   // dernier Work Coordinate Offset rapporté

  bool     _newStatus = false;

  // Flags de session
  bool     _homed      = false;   // homing validé
  bool     _homePending = false;  // $H envoyé, en attente de retour Idle
  uint8_t  _wzMask     = 0;       // bits 0..2 : axes X/Y/Z mis à zéro
  bool     _workZeroRestored = false; // indique si le WCO a été restauré vers GRBL

  // Position de changement d'outil (machine), persistée
  float    _tcp[3]   = {0, 0, 0};
  bool     _tcpValid = false;

  // Accusés ok/error
  uint8_t  _ackCount   = 0;
  bool     _ackError   = false;

  bool     _probeJustFinished = false;
  uint8_t  _expectedProbes    = 0;
  uint8_t  _probesDone        = 0;
};
