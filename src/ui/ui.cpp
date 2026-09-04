// ui.cpp — interface LVGL v3 (480x320). Rail de navigation + 6 ecrans.
//
// Disposition generale :
//   [ rail 62px ][ contenu 418px ]
// Le contenu heberge 7 « pages » empilees (une visible a la fois) :
//   PILOT, ORIG, FILES, DETAIL (sous-page de FILES), JOB, PROBE, SETTINGS.
//
// v3 — refonte du pilotage tactile :
//   * Ecran PILOT epure pour le doigt : croix de jog XY + Z en cibles 60 px,
//     molette de pas agrandie, Homing + Deverrouiller pleine largeur. Les
//     boutons de definition de point ont quitte cet ecran.
//   * Nouvel ecran ORIG « Origines & points » : definition + deplacement de
//     l'origine piece (= point de palpage) et du point de changement d'outil.
//   * REGL : section Broche supprimee (regime fixe) ; carte « Position chgt.
//     outil » deplacee vers ORIG. PALP : section « Vitesse de palpage »
//     supprimee (vitesses fixes).
//
// Le code reste en positionnement absolu (comme la v1) pour un controle fin sur
// un petit ecran tactile resistif (cibles genereuses).
#include "ui.h"

#include <lvgl.h>
#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/Display.h"
#include "storage/Storage.h"
#include "gcode/GcodeThumb.h"
#include <Preferences.h>
#include "led/LedControl.h"

// --- Reglages -------------------------------------------------------------- //
// Pas de jog : 9 crans, valeur active centree dans la molette.
// (Plus de "CONT" : le deplacement continu est gere par l'appui long.)
static const float STEP_VALUES[4] = {0.01f, 0.1f, 1.0f, 10.0f};
static const char* STEP_LABELS[4] = {"0.01", "0.1", "1", "10"};
static const int   STEP_COUNT     = 4;
static const float JOG_FEED_XY    = 1500.0f;
static const float JOG_FEED_Z     = 500.0f;
// Appui long (>= seuil) -> jog continu : grand deplacement, annule au relachement.
static const uint32_t JOG_LONG_MS      = 1000;     // seuil d'appui long (ms)
static const float    JOG_CONT_DIST_XY = 1000.0f;  // distance "continue" XY
static const float    JOG_CONT_DIST_Z  = 200.0f;   // distance "continue" Z
static const float PROBE_FEED_FAST  = 150.0f;
static const float PROBE_FEED_SLOW  = 30.0f;
// Hauteur de securite (coord. piece) au-dessus de l'origine avant traverse XY.
static const float ORIGIN_SAFE_Z    = 5.0f;
static char g_currentDir[128]     = "/";     // dossier scanne pour les G-code
static const int   MAX_FILES      = 24;

// --- Palette (graphite neutre, accent configurable) ------------------------- //
#define C_BG      lv_color_hex(0x111113)
#define C_RAIL    lv_color_hex(0x16161A)
#define C_CARD    lv_color_hex(0x1B1B1E)
#define C_CARD2   lv_color_hex(0x232327)
#define C_BORDER  lv_color_hex(0x2C2C31)
#define C_LINE    lv_color_hex(0x34343A)
// Accent runtime (« couleur de reference ») : choisi dans REGL, persiste en NVS,
// applique au boot. C_ACC / C_ACC_BG / C_ACC_TX sont desormais des variables
// (et non des #define) pour pouvoir etre recalculees — cf. apply_accent().
static lv_color_t C_ACC;        // accent principal
static lv_color_t C_ACC_BG;     // fond teinte de l'accent
static lv_color_t C_ACC_TX;     // texte sombre lisible sur l'accent
#define C_TXT     lv_color_hex(0xF0F0F2)
#define C_MUTE    lv_color_hex(0x6E6E73)
#define C_SUB     lv_color_hex(0xA8A8AE)
#define C_X       lv_color_hex(0xFF5A5F)
#define C_Y       lv_color_hex(0x39C77D)
#define C_Z       lv_color_hex(0x5B8DEF)   // axe Z : bleu fixe (convention X rouge / Y vert / Z bleu)
#define C_OK      lv_color_hex(0x2ECC71)
#define C_WARN    lv_color_hex(0xFFB020)
#define C_ERR     lv_color_hex(0xFF5A5F)
static lv_color_t C_THUMB;      // miniatures : teinte claire de l'accent

#define F12 &lv_font_montserrat_12
#define F14 &lv_font_montserrat_14
#define F16 &lv_font_montserrat_16
#define F18 &lv_font_montserrat_18
#define F20 &lv_font_montserrat_20
#define F22 &lv_font_montserrat_22
#define F24 &lv_font_montserrat_24
#define F28 &lv_font_montserrat_28

// --- Pages ------------------------------------------------------------------ //
enum Page { PG_PILOT = 0, PG_ORIG, PG_FILES, PG_DETAIL, PG_JOB, PG_PROBE, PG_SETTINGS, PG_COUNT };

// --- Etat du module --------------------------------------------------------- //
static FluidNC*   g_fluid = nullptr;
static JobRunner* g_job   = nullptr;
float      g_probeThk = 20.0f;
uint8_t    g_ledBright = 255;
int        g_nunchukSensitivity = 50;

// --- Couleur de reference (accent) ---------------------------------------- //
// Palette proposee dans l'ecran REGL. L'index choisi est persiste en NVS
// ("ui" / "accent_idx") et relu au boot par ui_init(). C_ACC etant utilise a la
// construction de TOUTES les pages, un changement n'est visible qu'apres reboot.
struct AccentDef { const char* name; uint32_t hex; };
static const AccentDef g_accentPalette[] = {
  { "Bleu",   0x2E90FF },
  { "Cyan",   0x22C3D6 },
  { "Vert",   0x2ECC71 },
  { "Ambre",  0xFFB020 },
  { "Orange", 0xFF7A1A },
  { "Violet", 0x9B7BFF },
};
static const uint8_t ACCENT_COUNT = sizeof(g_accentPalette) / sizeof(g_accentPalette[0]);
static const uint8_t ACCENT_DEFAULT = 4;   // 0=Bleu 1=Cyan 2=Vert 3=Ambre 4=Orange 5=Violet
static uint8_t g_accentIdx = ACCENT_DEFAULT;

// Recalcule l'accent et ses derives a partir de l'index choisi.
static void apply_accent(uint8_t idx) {
  if (idx >= ACCENT_COUNT) idx = 0;
  g_accentIdx = idx;
  C_ACC    = lv_color_hex(g_accentPalette[idx].hex);
  C_ACC_BG = lv_color_mix(C_ACC, lv_color_hex(0x111113), 42);   // ~16 % d'accent sur le fond
  C_ACC_TX = lv_color_hex(0x071018);                            // texte sombre lisible
  // Miniatures : teinte claire derivee de l'accent. (L'axe Z reste bleu fixe : C_Z.)
  C_THUMB  = lv_color_mix(lv_color_hex(0xFFFFFF), C_ACC, 110);  // accent eclairci (miniatures)
}

static lv_obj_t* g_content;
static lv_obj_t* g_pages[PG_COUNT];
static lv_obj_t* g_railBtn[6];          // PILOT/ORIG/PALP/FICH/USIN/REGL
static const int g_railOfPage[PG_COUNT] = {0, 1, 3, 3, 4, 2, 5};

static uint8_t g_stepIndex = 2;        // 1 mm par defaut
static lv_obj_t* g_stepCenter;         // valeur active (centre de la molette)
static lv_obj_t* g_jogStepLbl;         // lecture du pas au centre de la croix

// Pilotage : DRO
static lv_obj_t* g_lblMach[3];
static lv_obj_t* g_lblPiece[3];
static lv_obj_t* g_lblState;
static lv_obj_t* g_dotState;           // pastille d'etat (voyant, non cliquable)
static lv_obj_t* g_lblFS;
static lv_obj_t* g_btnHoming;
static lv_obj_t* g_btnUnlock;          // Bouton Homing pour maj de l'icone

// Origines & points
static lv_obj_t* g_origDot;
static lv_obj_t* g_origStatusLbl;
static lv_obj_t* g_tcpDot;
static lv_obj_t* g_tcpStatusLbl;
static lv_obj_t* g_origOverlay;        // Voile grisé quand homing non fait

// Fichiers / detail
static char      g_selPath[128] = {0};
static lv_obj_t* g_fileList;
static lv_obj_t* g_detailName;
static lv_obj_t* g_detailImg;
static lv_obj_t* g_detailBtnGo;         // « Lancer l'usinage »
static lv_obj_t* g_detailMsg;           // message si conditions non remplies
static lv_obj_t* g_detailChipHome;
static lv_obj_t* g_detailChipZero;
static lv_obj_t* g_detailTime;

// Usinage
static lv_obj_t* g_jobName;
static lv_obj_t* g_jobPct;
static lv_obj_t* g_jobTimes;
static lv_obj_t* g_jobLine;
static lv_obj_t* g_jobBar;
static lv_obj_t* g_jobPosLbl;
static lv_obj_t* g_jobBtnPause;
static lv_obj_t* g_jobImg;
static uint32_t g_jobEstTimeSec = 0;

// Palpage
static lv_obj_t* g_probeThkLbl;
static lv_obj_t* g_probeLastLbl;

// Miniatures : tampons alloues dynamiquement (palette + bitmap 1bpp) + dsc.
struct ThumbBuf { uint8_t* data; lv_img_dsc_t dsc; };
static ThumbBuf g_thumbs[MAX_FILES + 2];   // +2 : apercu detail et usinage

// ========================================================================== //
//  Helpers de creation
// ========================================================================== //
static lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h,
                       int radius, lv_color_t bg, lv_color_t border) {
  lv_obj_t* o = lv_obj_create(parent);
  if (!o) return nullptr;
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_border_color(o, border, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* label(lv_obj_t* parent, const char* txt, int x, int y,
                       const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  if (!l) return nullptr;
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, txt);
  return l;
}

static lv_obj_t* button(lv_obj_t* parent, const char* txt, int x, int y, int w, int h,
                        const lv_font_t* font, lv_event_cb_t cb, void* user,
                        lv_color_t bg, lv_color_t fg) {
  lv_obj_t* b = lv_btn_create(parent);
  if (!b) return nullptr;
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 7, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_color(b, C_ACC, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, fg, 0);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

// Pool de memoire statique pour eviter la fragmentation du tas (heap) de l'ESP32
#define THUMB_POOL_SIZE 5
static uint8_t* g_thumb_pool[THUMB_POOL_SIZE] = {nullptr};
static bool g_thumb_pool_used[THUMB_POOL_SIZE] = {false};

static uint8_t* alloc_thumb_buffer() {
  const size_t tc_size = gthumb::THUMB_BYTES * sizeof(lv_color_t);
  for (int i = 0; i < THUMB_POOL_SIZE; i++) {
    if (!g_thumb_pool[i]) {
      g_thumb_pool[i] = (uint8_t*)malloc(tc_size);
    }
    if (g_thumb_pool[i] && !g_thumb_pool_used[i]) {
      g_thumb_pool_used[i] = true;
      return g_thumb_pool[i];
    }
  }
  return nullptr; // Plus de blocs dispo dans le pool
}

static void free_thumb_buffer(uint8_t* ptr) {
  if (!ptr) return;
  for (int i = 0; i < THUMB_POOL_SIZE; i++) {
    if (g_thumb_pool[i] == ptr) {
      g_thumb_pool_used[i] = false;
      return;
    }
  }
  free(ptr); // Fallback au cas ou
}

static lv_obj_t* make_thumb(lv_obj_t* parent, const char* gpath, int targetPx, int slot, uint32_t* estTimeSec = nullptr, lv_obj_t* existing_img = nullptr) {
  if (slot < 0 || slot >= MAX_FILES + 2) return nullptr;

  const size_t bytes = gthumb::THUMB_BYTES;
  const size_t tc_size = bytes * sizeof(lv_color_t);

  if (g_thumbs[slot].data) {
    free_thumb_buffer(g_thumbs[slot].data);
    g_thumbs[slot].data = nullptr;
  }

  uint8_t* tmp = (uint8_t*)malloc(bytes);
  if (!tmp) return nullptr;

  uint8_t* buf = alloc_thumb_buffer();
  if (!buf) { free(tmp); return nullptr; }

  uint16_t w = gthumb::THUMB_W, h = gthumb::THUMB_H;

  if (!gthumb::load(gpath, tmp, bytes, &w, &h, estTimeSec)) {
    free(tmp); free_thumb_buffer(buf); return nullptr;
  }

  // Conversion 8-bit cache -> TRUE_COLOR
  lv_color_t* cbuf = (lv_color_t*)buf;
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t val = tmp[i];
    if (val == 0) {
      cbuf[i] = lv_color_make(30, 35, 40);
    } else {
      uint8_t r = 30 + ((255 - 30) * val) / 255;
      uint8_t g = 35 + ((235 - 35) * val) / 255;
      uint8_t b = 40 + ((200 - 40) * val) / 255;
      cbuf[i] = lv_color_make(r, g, b);
    }
  }
  free(tmp);

  ThumbBuf& tb = g_thumbs[slot];
  tb.data = buf;
  memset(&tb.dsc, 0, sizeof(tb.dsc));
  tb.dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  tb.dsc.header.w  = w;
  tb.dsc.header.h  = h;
  tb.dsc.data_size = tc_size;
  tb.dsc.data      = buf;

  lv_obj_t* img = existing_img ? existing_img : lv_img_create(parent);
  if (!img) { free_thumb_buffer(buf); tb.data = nullptr; return nullptr; }
  lv_img_set_src(img, &tb.dsc);

  if (targetPx > 0 && targetPx != w) {
    lv_img_set_zoom(img, (uint16_t)(256 * targetPx / w));
  }
  return img;
}

static void free_thumbs() {
  for (int i = 0; i < MAX_FILES; ++i) {
    if (g_thumbs[i].data) { free_thumb_buffer(g_thumbs[i].data); g_thumbs[i].data = nullptr; }
  }
}

// ========================================================================== //
//  Navigation
// ========================================================================== //
static void refresh_orig();   // fwd

static void show_page(Page p) {
  // Purge de la grande miniature si on quitte la vue détaillée
  if (p != PG_DETAIL && g_detailImg) {
    lv_obj_del(g_detailImg);
    g_detailImg = nullptr;
    if (g_thumbs[MAX_FILES].data) {
      free_thumb_buffer(g_thumbs[MAX_FILES].data);
      g_thumbs[MAX_FILES].data = nullptr;
    }
  }

  for (int i = 0; i < PG_COUNT; ++i) {
    if (i == (int)p) lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  int rail = g_railOfPage[p];
  for (int i = 0; i < 6; ++i) {
    bool on = (i == rail);
    lv_obj_set_style_bg_color(g_railBtn[i], on ? C_ACC_BG : C_RAIL, 0);
    lv_obj_set_style_border_color(g_railBtn[i], on ? C_ACC : C_RAIL, 0);
  }
}

static void rebuild_file_list();   // fwd
static void open_detail(const char* path);  // fwd

static void cb_nav(lv_event_t* e) {
  Page p = (Page)(intptr_t)lv_event_get_user_data(e);
  show_page(p); // Affiche la page instantanement pour eviter un gel de l'UI

  if (p == PG_ORIG) {
    refresh_orig();
  } else if (p == PG_FILES) {
    if (g_job && !g_job->isRunning()) {
      storage_refresh();
      if (storage_ready()) {
        rebuild_file_list();  // 1. Construit la liste avec les caches existants
        // lv_refr_now(NULL); // DANGER: Unsafe inside event handler!

        const uint32_t t0 = millis();
        int n = gthumb::scanAndGenerate(g_currentDir);
        Serial.printf("SD: %d miniature(s) generee(s)/nettoyee(s) en %lu ms\n", n, millis() - t0);

        if (n > 0) {
          rebuild_file_list(); // 3. Reconstruit si des modifications ont eu lieu
        }
      } else {
        rebuild_file_list();
      }
    } else {
      rebuild_file_list();
    }
  }
}

// ========================================================================== //
//  Callbacks — Pilotage
// ========================================================================== //
struct JogDesc { uint8_t axis; float dir; };
static JogDesc g_jog[6] = {{0, +1}, {0, -1}, {1, +1}, {1, -1}, {2, +1}, {2, -1}};

// Met a jour l'affichage de la molette de pas (valeur active au centre).
static void update_step() {
  lv_label_set_text(g_stepCenter, STEP_LABELS[g_stepIndex]);
  if (g_jogStepLbl) lv_label_set_text(g_jogStepLbl, STEP_LABELS[g_stepIndex]);
}

static void cb_jog(lv_event_t* e);

static lv_obj_t* jog_button(lv_obj_t* parent, const char* txt, int x, int y, int w, int h,
                            const lv_font_t* font, JogDesc* user, lv_color_t bg, lv_color_t fg) {
  lv_obj_t* b = button(parent, txt, x, y, w, h, font, nullptr, nullptr, bg, fg);
  lv_obj_set_style_border_color(b, C_ACC, 0);          // bord bleu : bouton cliquable
  lv_obj_add_event_cb(b, cb_jog, LV_EVENT_PRESSED, user);
  lv_obj_add_event_cb(b, cb_jog, LV_EVENT_PRESSING, user);   // detection appui long
  lv_obj_add_event_cb(b, cb_jog, LV_EVENT_RELEASED, user);
  lv_obj_add_event_cb(b, cb_jog, LV_EVENT_PRESS_LOST, user);
  return b;
}

// Appui court = un pas incremental ; appui long (>= JOG_LONG_MS) = jog continu.
static JogDesc*  g_jogActive  = nullptr;
static uint32_t  g_jogPressMs = 0;
static bool      g_jogCont    = false;

static float clip_jog_dist(uint8_t axis, float mpos, float dist) {
  if (axis == 0 || axis == 1) { // X ou Y : pas de negatif
    if (dist < 0.0f && (mpos + dist) < 0.0f) {
      dist = -mpos;
      if (dist > 0.0f) dist = 0.0f;
    }
  } else if (axis == 2) { // Z : pas de positif
    if (dist > 0.0f && (mpos + dist) > 0.0f) {
      dist = -mpos;
      if (dist < 0.0f) dist = 0.0f;
    }
  }
  return dist;
}

static void cb_jog(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  JogDesc* d = (JogDesc*)lv_event_get_user_data(e);

  if (g_fluid->status().state != MachineState::Idle &&
      g_fluid->status().state != MachineState::Jog &&
      g_fluid->status().state != MachineState::Alarm) { // Autoriser le Jog en alarme pour dégager une butée
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    g_jogActive  = d;
    g_jogPressMs = millis();
    g_jogCont    = false;
  } else if (code == LV_EVENT_PRESSING) {
    // Appui maintenu : au-dela du seuil, bascule en deplacement continu.
    if (!g_jogCont && g_jogActive == d && (millis() - g_jogPressMs) >= JOG_LONG_MS) {
      g_jogCont  = true;
      float feed = (d->axis == 2) ? JOG_FEED_Z : JOG_FEED_XY;
      float dist = d->dir * ((d->axis == 2) ? JOG_CONT_DIST_Z : JOG_CONT_DIST_XY);
      
      if (g_fluid->isHomed()) {
        dist = clip_jog_dist(d->axis, g_fluid->status().mpos[d->axis], dist);
      }
      
      if (dist != 0.0f) {
        g_fluid->jog(d->axis, dist, feed);
      }
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (g_jogCont) {
      g_fluid->jogCancel();                              // fin du deplacement continu
    } else if (g_jogActive == d) {
      // Appui court : un pas incremental.
      float feed = (d->axis == 2) ? JOG_FEED_Z : JOG_FEED_XY;
      float dist = d->dir * STEP_VALUES[g_stepIndex];
      
      if (g_fluid->isHomed()) {
        dist = clip_jog_dist(d->axis, g_fluid->status().mpos[d->axis], dist);
      }
      
      if (dist != 0.0f) {
        g_fluid->jog(d->axis, dist, feed);
      }
    }
    g_jogActive = nullptr;
    g_jogCont   = false;
  }
}

static void cb_step_prev(lv_event_t*) { if (g_stepIndex > 0)              { g_stepIndex--; update_step(); } }
static void cb_step_next(lv_event_t*) { if (g_stepIndex < STEP_COUNT - 1) { g_stepIndex++; update_step(); } }
static void cb_homing(lv_event_t*)  { g_fluid->home(); }
static void cb_unlock(lv_event_t*)  { g_fluid->unlock(); }

// ========================================================================== //
//  Callbacks — Origines & points
// ========================================================================== //
static void cb_zero_axis(lv_event_t* e) {
  g_fluid->zeroAxis((uint8_t)(intptr_t)lv_event_get_user_data(e));
  refresh_orig();
}
// Definir l'origine piece (= point de palpage) : X=Y=Z=0 dans le WCS actif.
static void cb_zeroall(lv_event_t*) { g_fluid->zeroAll(); refresh_orig(); }

// Aller a l'origine piece (= point de palpage) : on remonte au-dessus de
// l'origine puis on se place en XY. On NE descend PAS jusqu'a Z0 (le palpeur /
// la matiere s'y trouvent) : on s'arrete a ORIGIN_SAFE_Z au-dessus.
static void cb_goto_origin(lv_event_t*) {
  if (!g_fluid->isWorkZeroSet()) return;             // garde-fou : origine requise
  
  float target_y_wpos = 0.0f;
  float target_x_wpos = 0.0f;

  if (g_fluid->isHomed()) {
    float wco[3];
    for (int i=0; i<3; ++i) wco[i] = g_fluid->status().mpos[i] - g_fluid->status().wpos[i];

    if (target_y_wpos + wco[1] < 0.0f) target_y_wpos = -wco[1];
    if (target_x_wpos + wco[0] < 0.0f) target_x_wpos = -wco[0];
  }

  char buf[32];
  g_fluid->sendLine("G53 G0 Z0");                    // remonte Z au maximum (machine)
  g_fluid->sendLine("G90");                          // coordonnees absolues (WCS)
  snprintf(buf, sizeof(buf), "G0 Y%.3f", target_y_wpos);
  g_fluid->sendLine(buf);                            // se placer sur l'origine en Y
  snprintf(buf, sizeof(buf), "G0 X%.3f", target_x_wpos);
  g_fluid->sendLine(buf);                            // se placer sur l'origine en X
}

// Definir / aller / effacer le point de changement d'outil.
static void cb_tcp_set(lv_event_t*)   { g_fluid->setToolChangePos();         refresh_orig(); }
static void cb_tcp_clear(lv_event_t*) { g_fluid->clearToolChangePos();       refresh_orig(); }
static void cb_tcp_goto(lv_event_t*)  { g_fluid->gotoToolChangePos(JOG_FEED_XY); }

// Met a jour l'ecran ORIG (etat origine piece + point chgt. outil).
static void refresh_orig() {
  if (!g_origStatusLbl) return;
  bool zero = g_fluid && g_fluid->isWorkZeroSet();
  lv_label_set_text(g_origStatusLbl, zero ? "definie (X0 Y0 Z0)" : "non definie");
  lv_obj_set_style_bg_color(g_origDot, zero ? C_OK : C_MUTE, 0);

  if (g_fluid && g_fluid->hasToolChangePos()) {
    const float* p = g_fluid->toolChangePos();
    char b[48]; snprintf(b, sizeof(b), "X%.1f Y%.1f Z%.1f", p[0], p[1], p[2]);
    lv_label_set_text(g_tcpStatusLbl, b);
    lv_obj_set_style_bg_color(g_tcpDot, C_OK, 0);
  } else {
    lv_label_set_text(g_tcpStatusLbl, "non defini");
    lv_obj_set_style_bg_color(g_tcpDot, C_MUTE, 0);
  }
}

// ========================================================================== //
//  Callbacks — Usinage
// ========================================================================== //
static void cb_feed_minus(lv_event_t*) { g_fluid->feedOverrideAdd(-10); }
static void cb_feed_plus (lv_event_t*) { g_fluid->feedOverrideAdd(+10); }
static void cb_feed_reset(lv_event_t*) { g_fluid->feedOverrideReset(); }
static void cb_job_pause(lv_event_t*) {
  if (!g_job->isActive()) return;
  
  if (g_job->isPaused()) {
    if (g_job->m6State() == JobRunner::M6State::WaitToolChange) {
      // Étape 1 : L'outil a été changé physiquement.
      // On retourne à l'origine (Z0 puis Y0 puis X0)
      g_fluid->sendLine("G53 G0 Z0");
      g_fluid->sendLine("G90 G0 Y0");
      g_fluid->sendLine("G90 G0 X0");
      
      // On bascule dans l'état attente palpage et on affiche l'onglet PALP
      g_job->setM6State(JobRunner::M6State::WaitProbe);
      show_page(PG_PROBE);
      return; // On ne reprend pas l'usinage !
    } 
    else if (g_job->m6State() == JobRunner::M6State::WaitProbe) {
      // Étape 2 : Le palpage a été fait, on reprend vraiment.
      // (Le redémarrage broche est fait dans g_job->resume())
      g_job->resume();
    }
    else {
      g_job->resume();
    }
  } else {
    g_job->pause();
  }
}
static void cb_job_stop(lv_event_t*) {
  g_job->stop();
  show_page(PG_FILES);
  rebuild_file_list();
}

// ========================================================================== //
//  Callbacks — Palpage
// ========================================================================== //
static void save_probe_thk() {
  Preferences prefs;
  prefs.begin("ui", false);
  prefs.putFloat("probe_thk", g_probeThk);
  prefs.end();
}

static void cb_thk_minus(lv_event_t*) {
  g_probeThk -= 0.1f; if (g_probeThk < 0) g_probeThk = 0;
  char b[16]; snprintf(b, sizeof(b), "%.2f", g_probeThk);
  lv_label_set_text(g_probeThkLbl, b);
  save_probe_thk();
}
static void cb_thk_plus(lv_event_t*) {
  g_probeThk += 0.1f;
  char b[16]; snprintf(b, sizeof(b), "%.2f", g_probeThk);
  lv_label_set_text(g_probeThkLbl, b);
  save_probe_thk();
}
static void cb_probe(lv_event_t*) {
  g_fluid->probeZ(g_probeThk, PROBE_FEED_FAST, PROBE_FEED_SLOW);
  char b[32]; snprintf(b, sizeof(b), "Z = %.3f", g_probeThk);
  lv_label_set_text(g_probeLastLbl, b);
  
  if (g_job->m6State() == JobRunner::M6State::WaitProbe) {
    g_job->setProbeRunningForM6(true);
  }
}

// ========================================================================== //
//  Callbacks — Reglages
// ========================================================================== //
static void cb_calibrate(lv_event_t*) { display_calibrate(); }

// ========================================================================== //
//  Callbacks — Fichiers / detail
// ========================================================================== //
static void cb_file_row(lv_event_t* e) {
  const char* path = (const char*)lv_event_get_user_data(e);
  
  if (strcmp(path, "..") == 0) {
    char* lastSlash = strrchr(g_currentDir, '/');
    if (lastSlash != nullptr && lastSlash != g_currentDir) {
      *lastSlash = '\0';
    } else {
      strcpy(g_currentDir, "/");
    }
    rebuild_file_list();
    return;
  }
  
  File f = SD.open(path);
  if (!f) return;
  bool isDir = f.isDirectory();
  f.close();
  
  if (isDir) {
    snprintf(g_currentDir, sizeof(g_currentDir), "%s", path);
    
    // Générer les miniatures pour le nouveau dossier avant de rafraîchir
    if (gthumb::scanAndGenerate(g_currentDir) > 0) {
       // On pourrait forcer un log ici, mais pas indispensable
    }
    rebuild_file_list();
  } else {
    open_detail(path);
  }
}
static void cb_detail_back(lv_event_t*) { show_page(PG_FILES); }
static void cb_detail_go(lv_event_t*) {
  if (!(g_fluid->isHomed() && g_fluid->isWorkZeroSet())) return;  // garde-fou

  // Si on n'est pas à l'origine en X ou Y, on y va (Z safe, puis Y, puis X)
  float wx = g_fluid->status().wpos[0];
  float wy = g_fluid->status().wpos[1];
  if (fabs(wx) > 0.01f || fabs(wy) > 0.01f) {
    g_fluid->sendLine("G53 G0 Z0");
    g_fluid->sendLine("G90");
    g_fluid->sendLine("G0 Y0");
    g_fluid->sendLine("G0 X0");
  }

  if (g_job->start(g_selPath)) {
    lv_label_set_text(g_jobName, g_job->name());
    
    // Affiche le trace sur l'ecran d'usinage
    lv_obj_t* prev = (lv_obj_t*)lv_obj_get_user_data(g_pages[PG_JOB]);
    if (g_jobImg) { lv_obj_del(g_jobImg); g_jobImg = nullptr; }
    uint32_t estTimeSec = 0;
    g_jobImg = make_thumb(prev, g_selPath, 120, MAX_FILES + 1, &estTimeSec);
    if (g_jobImg) lv_obj_align(g_jobImg, LV_ALIGN_CENTER, 0, 8);

    g_jobEstTimeSec = estTimeSec;
    
    char el[8] = "00:00", re[8] = "--:--";
    if (estTimeSec > 0) {
      uint32_t s = estTimeSec;
      snprintf(re, sizeof(re), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
    char t[40]; snprintf(t, sizeof(t), "ecoule %s\nrestant %s", el, re);
    lv_label_set_text(g_jobTimes, t);

    show_page(PG_JOB);
  }
}

// ========================================================================== //
//  Construction du rail
// ========================================================================== //
static const char* RAIL_TXT[6] = {"PILOT", "ORIG", "PALP", "FICH", "USIN", "REGL"};
static const Page  RAIL_PG[6]  = {PG_PILOT, PG_ORIG, PG_PROBE, PG_FILES, PG_JOB, PG_SETTINGS};

static void build_rail(lv_obj_t* scr) {
  lv_obj_t* rail = panel(scr, 0, 0, 62, 320, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_width(rail, 0, 0);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(rail, 1, 0);

  // Marque « CNC-32 »
  lv_obj_t* brand = panel(rail, 7, 8, 48, 22, 6, C_ACC_BG, C_ACC_BG);
  lv_obj_set_style_border_width(brand, 0, 0);
  lv_obj_t* bl = label(brand, "CNC-32", 0, 0, F12, C_ACC);
  lv_obj_center(bl);

  // 6 boutons compactes (pitch 46) pour tenir dans 320 px de haut.
  for (int i = 0; i < 6; ++i) {
    lv_obj_t* b = lv_btn_create(rail);
    lv_obj_set_pos(b, 3, 38 + i * 46);
    lv_obj_set_size(b, 56, 42);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, C_RAIL, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(b, C_RAIL, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb_nav, LV_EVENT_CLICKED, (void*)(intptr_t)RAIL_PG[i]);
    lv_obj_t* l = label(b, RAIL_TXT[i], 0, 0, F14, C_SUB);
    lv_obj_center(l);
    g_railBtn[i] = b;
  }
}

// ========================================================================== //
//  Ecran Pilotage
// ========================================================================== //
static void build_pilot(lv_obj_t* parent) {
  // --- Barre d'etat ---------------------------------------------------------
  // Voyant d'etat (pastille de couleur + texte) - NON cliquable (bord neutre).
  lv_obj_t* badge = panel(parent, 8, 6, 104, 28, 6, C_CARD, C_BORDER);
  g_dotState = lv_obj_create(badge);
  lv_obj_set_size(g_dotState, 9, 9);
  lv_obj_set_pos(g_dotState, 11, 9);
  lv_obj_set_style_radius(g_dotState, 5, 0);
  lv_obj_set_style_bg_color(g_dotState, C_ACC, 0);
  lv_obj_set_style_border_width(g_dotState, 0, 0);
  lv_obj_clear_flag(g_dotState, LV_OBJ_FLAG_SCROLLABLE);
  g_lblState = label(badge, "----", 28, 5, F14, C_TXT);

  // Systeme de coordonnees (affichage)
  lv_obj_t* wcs = panel(parent, 120, 6, 46, 28, 6, C_CARD, C_LINE);
  lv_obj_t* wl = label(wcs, "G54", 0, 0, F14, C_SUB); lv_obj_center(wl);

  // Avance / broche (affichage)
  g_lblFS = label(parent, "F0  Broche OFF", 176, 12, F14, C_MUTE);

  // Legende M (machine) / P (piece)
  label(parent, "M",     330, 4,  F12, C_TXT);
  label(parent, "machine",  346, 4,  F12, C_MUTE);
  label(parent, "P",     330, 18, F12, C_ACC);
  label(parent, "piece", 346, 18, F12, C_MUTE);

  // --- DRO compact : X / Y / Z sur une seule ligne --------------------------
  // M = position machine (gros, blanc) ; P = position piece (petit, bleu).
  lv_obj_t* dro = panel(parent, 8, 40, 402, 74, 8, C_CARD, C_BORDER);
  const lv_color_t aco[3] = {C_X, C_Y, C_Z};
  const char* an[3] = {"X", "Y", "Z"};
  for (int i = 0; i < 3; ++i) {
    int cx = i * 134;
    if (i > 0) {                          // separateur de colonne
      lv_obj_t* sep = lv_obj_create(dro);
      lv_obj_set_size(sep, 1, 56);
      lv_obj_set_pos(sep, cx, 9);
      lv_obj_set_style_bg_color(sep, C_BORDER, 0);
      lv_obj_set_style_border_width(sep, 0, 0);
      lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t* ab = panel(dro, cx + 6, 16, 26, 42, 6, C_CARD2, C_CARD2);
    lv_obj_set_style_border_width(ab, 0, 0);
    lv_obj_t* al = label(ab, an[i], 0, 0, F24, aco[i]); lv_obj_center(al);

    label(dro, "M", cx + 36, 14, F12, C_MUTE);
    g_lblMach[i] = lv_label_create(dro);
    lv_obj_set_size(g_lblMach[i], 78, 22);
    lv_obj_set_pos(g_lblMach[i], cx + 48, 12);
    lv_obj_set_style_text_font(g_lblMach[i], F18, 0);
    lv_obj_set_style_text_color(g_lblMach[i], C_TXT, 0);
    lv_obj_set_style_text_align(g_lblMach[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_lblMach[i], "0.00");

    label(dro, "P", cx + 36, 40, F12, C_ACC);
    g_lblPiece[i] = lv_label_create(dro);
    lv_obj_set_size(g_lblPiece[i], 78, 18);
    lv_obj_set_pos(g_lblPiece[i], cx + 48, 40);
    lv_obj_set_style_text_font(g_lblPiece[i], F14, 0);
    lv_obj_set_style_text_color(g_lblPiece[i], C_SUB, 0);
    lv_obj_set_style_text_align(g_lblPiece[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_lblPiece[i], "?.??");
  }

  // --- Croix de jog XY (cibles tactiles 60 px) ------------------------------
  jog_button(parent, "Y+",  72, 120, 60, 60, F28, &g_jog[2], C_CARD2, C_ACC);
  jog_button(parent, "X-",   6, 186, 60, 60, F28, &g_jog[1], C_CARD2, C_ACC);
  jog_button(parent, "X+", 138, 186, 60, 60, F28, &g_jog[0], C_CARD2, C_ACC);
  jog_button(parent, "Y-",  72, 252, 60, 60, F28, &g_jog[3], C_CARD2, C_ACC);

  // Centre : lecture du pas actif (NON cliquable)
  lv_obj_t* ctr = panel(parent, 72, 186, 60, 60, 8, C_RAIL, C_BORDER);
  g_jogStepLbl = label(ctr, STEP_LABELS[g_stepIndex], 0, 0, F24, C_ACC);
  lv_obj_align(g_jogStepLbl, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t* mmL = label(ctr, "mm", 0, 0, F12, C_MUTE);
  lv_obj_align(mmL, LV_ALIGN_CENTER, 0, 12);

  // --- Jog Z (cibles hautes) ------------------------------------------------
  jog_button(parent, "Z+", 204, 120, 60, 93, F28, &g_jog[4], C_CARD2, C_ACC);
  jog_button(parent, "Z-", 204, 219, 60, 93, F28, &g_jog[5], C_CARD2, C_ACC);

  // --- Molette de pas (valeur active centree) - fleches cliquables ----------
  lv_obj_t* sc = panel(parent, 276, 120, 136, 70, 8, C_RAIL, C_LINE);
  label(sc, "PAS DE JOG (mm)", 8, 6, F12, C_MUTE);

  lv_obj_t* lz = lv_btn_create(sc);
  lv_obj_set_pos(lz, 8, 28); lv_obj_set_size(lz, 34, 34);
  lv_obj_set_style_radius(lz, 6, 0);
  lv_obj_set_style_bg_color(lz, C_CARD2, 0);
  lv_obj_set_style_bg_color(lz, C_ACC, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(lz, 1, 0);
  lv_obj_set_style_border_color(lz, C_ACC, 0);
  lv_obj_set_style_shadow_width(lz, 0, 0);
  lv_obj_add_event_cb(lz, cb_step_prev, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lzl = label(lz, "<", 0, 0, F20, C_ACC); lv_obj_center(lzl);

  lv_obj_t* cz = panel(sc, 47, 28, 42, 34, 6, C_ACC, C_ACC);
  lv_obj_set_style_border_width(cz, 0, 0);
  g_stepCenter = label(cz, STEP_LABELS[g_stepIndex], 0, 0, F22, C_ACC_TX);
  lv_obj_center(g_stepCenter);

  lv_obj_t* rz = lv_btn_create(sc);
  lv_obj_set_pos(rz, 94, 28); lv_obj_set_size(rz, 34, 34);
  lv_obj_set_style_radius(rz, 6, 0);
  lv_obj_set_style_bg_color(rz, C_CARD2, 0);
  lv_obj_set_style_bg_color(rz, C_ACC, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(rz, 1, 0);
  lv_obj_set_style_border_color(rz, C_ACC, 0);
  lv_obj_set_style_shadow_width(rz, 0, 0);
  lv_obj_add_event_cb(rz, cb_step_next, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* rzl = label(rz, ">", 0, 0, F20, C_ACC); lv_obj_center(rzl);

  // --- Actions machine (pleine largeur, tactiles) ---------------------------
  g_btnHoming = button(parent, "Homing", 276, 198, 136, 54, F22, cb_homing, nullptr, C_ACC_BG, C_ACC);
  if (g_btnHoming) {
    lv_obj_set_style_border_color(g_btnHoming, C_ACC, 0);
    lv_obj_t* l = lv_obj_get_child(g_btnHoming, 0);
    lv_label_set_recolor(l, true);
  }
  g_btnUnlock = button(parent, "Deverrouiller", 276, 260, 136, 52, F20, cb_unlock, nullptr, C_CARD2, C_ACC);
  lv_obj_set_style_border_color(g_btnUnlock, C_ACC, 0);
  lv_obj_add_state(g_btnUnlock, LV_STATE_DISABLED); // Grisé par défaut au démarrage

  update_step();   // initialise l'affichage de la molette
}

// ========================================================================== //
//  Ecran Origines & points
// ========================================================================== //
static void build_orig(lv_obj_t* parent) {
  // Barre de titre
  lv_obj_t* bar = panel(parent, 0, 0, 418, 40, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  label(bar, "Origines & points", 14, 11, F16, C_TXT);

  // --- Carte A : origine piece (= point de palpage) -------------------------
  lv_obj_t* ca = panel(parent, 8, 48, 200, 262, 10, C_CARD, C_BORDER);
  label(ca, "Origine piece", 14, 12, F16, C_TXT);
  label(ca, "= point de palpage (Z0)", 14, 34, F12, C_ACC);

  lv_obj_t* sa = panel(ca, 14, 56, 172, 36, 7, C_RAIL, C_LINE);
  g_origDot = lv_obj_create(sa);
  lv_obj_set_size(g_origDot, 9, 9); lv_obj_set_pos(g_origDot, 10, 13);
  lv_obj_set_style_radius(g_origDot, 5, 0);
  lv_obj_set_style_bg_color(g_origDot, C_MUTE, 0);
  lv_obj_set_style_border_width(g_origDot, 0, 0);
  lv_obj_clear_flag(g_origDot, LV_OBJ_FLAG_SCROLLABLE);
  g_origStatusLbl = label(sa, "non definie", 26, 9, F14, C_SUB);

  // Action principale en haut : aller a l'origine piece (= point de palpage).
  lv_obj_t* ga = button(ca, "Aller a l'origine piece", 14, 100, 172, 46, F14, cb_goto_origin, nullptr, C_CARD2, C_ACC);
  lv_obj_set_style_border_color(ga, C_ACC, 0);
  // Definir ici en dessous, plus discret (hauteur reduite, sans accent bleu).
  lv_obj_t* ba = button(ca, "Definir ici", 14, 156, 172, 40, F14, cb_zeroall, nullptr, C_CARD2, C_SUB);
  lv_obj_set_style_border_color(ba, C_LINE, 0);

  // Mise a zero par axe
  const char* z0[3]  = {"X -> 0", "Y -> 0", "Z -> 0"};
  const int   z0x[3] = {14, 73, 132};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* b = button(ca, z0[i], z0x[i], 208, 54, 40, F12,
                         cb_zero_axis, (void*)(intptr_t)i, C_CARD2, C_ACC);
    lv_obj_set_style_border_color(b, C_ACC, 0);
  }

  // --- Carte B : point de changement d'outil --------------------------------
  lv_obj_t* cb = panel(parent, 216, 48, 194, 262, 10, C_CARD, C_BORDER);
  label(cb, "Point chgt. outil", 14, 12, F16, C_TXT);
  label(cb, "coord. machine (NVS)", 14, 34, F12, C_MUTE);

  lv_obj_t* sb = panel(cb, 14, 56, 166, 36, 7, C_RAIL, C_LINE);
  g_tcpDot = lv_obj_create(sb);
  lv_obj_set_size(g_tcpDot, 9, 9); lv_obj_set_pos(g_tcpDot, 10, 13);
  lv_obj_set_style_radius(g_tcpDot, 5, 0);
  lv_obj_set_style_bg_color(g_tcpDot, C_MUTE, 0);
  lv_obj_set_style_border_width(g_tcpDot, 0, 0);
  lv_obj_clear_flag(g_tcpDot, LV_OBJ_FLAG_SCROLLABLE);
  g_tcpStatusLbl = label(sb, "non defini", 26, 9, F12, C_SUB);

  // Action principale en haut : aller au point de changement d'outil.
  lv_obj_t* bg = button(cb, "Aller au chgt. outil", 14, 100, 166, 46, F14, cb_tcp_goto, nullptr, C_CARD2, C_ACC);
  lv_obj_set_style_border_color(bg, C_ACC, 0);
  // Definir ici en dessous, plus discret (hauteur reduite, sans accent bleu).
  lv_obj_t* bs = button(cb, "Definir ici", 14, 156, 166, 40, F14, cb_tcp_set, nullptr, C_CARD2, C_SUB);
  lv_obj_set_style_border_color(bs, C_LINE, 0);
  lv_obj_t* bc = button(cb, "Effacer le point", 14, 208, 166, 40, F12, cb_tcp_clear, nullptr, C_CARD2, C_WARN);
  lv_obj_set_style_border_color(bc, C_WARN, 0);

  // Overlay de blocage si Homing non fait
  g_origOverlay = panel(parent, 0, 40, 418, 280, 0, lv_color_make(0, 0, 0), lv_color_make(0, 0, 0));
  if (g_origOverlay) {
    lv_obj_set_style_bg_opa(g_origOverlay, 200, 0); // Semi-transparent
    lv_obj_t* lbl = label(g_origOverlay, "Homing requis !", 0, 0, F20, C_WARN);
    if (lbl) lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t* lbl2 = label(g_origOverlay, "Veuillez effectuer un Homing (onglet PILOT)\navant d'utiliser les origines.", 0, 0, F14, C_TXT);
    if (lbl2 && lbl) {
      lv_obj_align_to(lbl2, lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
      lv_obj_set_style_text_align(lbl2, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_add_flag(g_origOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_origOverlay, LV_OBJ_FLAG_CLICKABLE); // Intercepte les clics
  }

  refresh_orig();
}

// ========================================================================== //
//  Ecran Fichiers (liste)
// ========================================================================== //
static void build_files(lv_obj_t* parent) {
  // Barre de titre
  lv_obj_t* bar = panel(parent, 0, 0, 418, 40, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  label(bar, "Carte SD", 12, 11, F16, C_TXT);

  // Conteneur defilant de la liste
  g_fileList = lv_obj_create(parent);
  lv_obj_set_pos(g_fileList, 0, 40);
  lv_obj_set_size(g_fileList, 418, 280);
  lv_obj_set_style_bg_color(g_fileList, C_BG, 0);
  lv_obj_set_style_border_width(g_fileList, 0, 0);
  lv_obj_set_style_pad_all(g_fileList, 8, 0);
  lv_obj_set_style_pad_row(g_fileList, 6, 0);
  lv_obj_set_flex_flow(g_fileList, LV_FLEX_FLOW_COLUMN);
}

// (Re)construit les lignes de fichiers depuis la SD.
struct FileEntry {
  char path[128];
  uint32_t size;
  time_t modTime;
  bool isDir;
};

enum ThumbState { THUMB_IDLE, THUMB_SCANNING };
static ThumbState g_thumbState = THUMB_IDLE;

static char g_filePaths[MAX_FILES][128];
static uint32_t g_fileSizes[MAX_FILES];
static bool g_fileIsDir[MAX_FILES];
static int g_fileCount = 0;

static void rebuild_file_list() {
  if (!g_fileList) return;
  lv_obj_clean(g_fileList);   // supprime les anciennes lignes
  free_thumbs();              // libere les tampons de miniatures

  if (!storage_ready()) {
    label(g_fileList, "Carte SD absente", 0, 0, F16, C_MUTE);
    return;
  }

  File dir = SD.open(g_currentDir);
  if (!dir) { label(g_fileList, "Dossier introuvable", 0, 0, F16, C_MUTE); return; }

  FileEntry* entries = (FileEntry*)malloc(sizeof(FileEntry) * MAX_FILES);
  if (!entries) {
    label(g_fileList, "Erreur memoire", 0, 0, F16, C_MUTE);
    dir.close();
    return;
  }

  int count = 0;
  if (strcmp(g_currentDir, "/") != 0) {
    snprintf(entries[count].path, sizeof(entries[count].path), "..");
    entries[count].size = 0;
    entries[count].modTime = 0;
    entries[count].isDir = true;
    count++;
  }

  for (File f = dir.openNextFile(); f && count < MAX_FILES; f = dir.openNextFile()) {
    bool isD = f.isDirectory();
    if (isD) {
      const char* fn = f.name();
      const char* baseFn = strrchr(fn, '/');
      baseFn = baseFn ? baseFn + 1 : fn;
      if (strcasecmp(baseFn, "System Volume Information") == 0 ||
          strcasecmp(baseFn, "thumbs") == 0) {
        f.close();
        continue;
      }
    } else {
      if (!gthumb::isGcodeFile(f.name())) { f.close(); continue; }
    }

    snprintf(entries[count].path, sizeof(entries[count].path), "%s", f.path());
    entries[count].size = f.size();
    entries[count].modTime = f.getLastWrite();
    entries[count].isDir = isD;
    count++;
    f.close();
  }
  dir.close();

  // Sort files: ".." first, then folders (alphabetical), then files by modTime (descending)
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(entries[i].path, "..") == 0) continue;
      
      bool swap_needed = false;
      if (strcmp(entries[j].path, "..") == 0) {
        swap_needed = true;
      } else if (entries[i].isDir && !entries[j].isDir) {
        swap_needed = false;
      } else if (!entries[i].isDir && entries[j].isDir) {
        swap_needed = true;
      } else if (entries[i].isDir && entries[j].isDir) {
        if (strcasecmp(entries[i].path, entries[j].path) > 0) swap_needed = true;
      } else {
        if (entries[i].modTime < entries[j].modTime) swap_needed = true;
      }

      if (swap_needed) {
        FileEntry temp = entries[i];
        entries[i] = entries[j];
        entries[j] = temp;
      }
    }
  }

  g_fileCount = count;

  for (int i = 0; i < count; i++) {
    snprintf(g_filePaths[i], sizeof(g_filePaths[i]), "%s", entries[i].path);
    uint32_t sz = entries[i].size;
    g_fileSizes[i] = sz;
    g_fileIsDir[i] = entries[i].isDir;

    lv_obj_t* row = lv_obj_create(g_fileList);
    if (!row) break; // OOM, stop creating rows
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_color(row, C_CARD, 0);
    lv_obj_set_style_border_color(row, C_BORDER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 7, 0);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb_file_row, LV_EVENT_CLICKED, g_filePaths[i]);

    // 0: Placeholder
    lv_obj_t* th = panel(row, 0, 0, 48, 48, 4, C_CARD2, C_LINE);
    lv_obj_align(th, LV_ALIGN_LEFT_MID, 0, 0);

    // 1: Image (cachée par défaut)
    lv_obj_t* img = lv_img_create(row);
    lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);

    // 2: Vide (remplace l'ancien label ERR pour garder le même nombre d'enfants)
    lv_obj_t* err = lv_obj_create(row);
    lv_obj_add_flag(err, LV_OBJ_FLAG_HIDDEN);

    // 3: Nom du fichier
    const char* base = strrchr(g_filePaths[i], '/');
    base = base ? base + 1 : g_filePaths[i];
    if (strcmp(g_filePaths[i], "..") == 0) base = "Retour";
    
    lv_obj_t* nm = label(row, base, 58, 8, F16, C_TXT);
    if (nm) {
      lv_obj_set_width(nm, 300);
      lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    }

    // 4: Infos
    char info[64];
    if (entries[i].isDir) {
      snprintf(info, sizeof(info), "Dossier");
      label(row, info, 58, 32, F12, C_MUTE);
      
      if (strcmp(entries[i].path, "..") == 0) {
        lv_obj_t* dl = label(th, LV_SYMBOL_UP, 0, 0, F24, C_ACC);
        lv_obj_center(dl);
      } else {
        lv_obj_t* dl = label(th, LV_SYMBOL_DIRECTORY, 0, 0, F24, C_ACC);
        lv_obj_center(dl);
      }
    } else {
      snprintf(info, sizeof(info), "%lu Ko  |  ...", (unsigned long)(sz / 1024));
      label(row, info, 58, 32, F12, C_MUTE);
    }
  }

  free(entries);

  if (count == 0) label(g_fileList, "Aucun fichier G-code", 0, 0, F16, C_MUTE);
}

// ========================================================================== //
//  Ecran Detail (apercu + lancement conditionne)
// ========================================================================== //
static void build_detail(lv_obj_t* parent) {
  // En-tete avec bouton « Retour a la liste »
  lv_obj_t* bar = panel(parent, 0, 0, 418, 44, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  button(bar, "< Retour a la liste", 8, 6, 160, 32, F14, cb_detail_back, nullptr,
         C_CARD2, C_TXT);

  // Zone d'apercu (la miniature y est inseree a l'ouverture)
  lv_obj_t* prev = panel(parent, 12, 54, 394, 150, 8, lv_color_hex(0x0E0E10), C_BORDER);
  lv_obj_set_user_data(parent, prev);   // memorise le conteneur d'apercu
  label(prev, "parcours XY - vue de dessus", 8, 6, F12, C_MUTE);
  g_detailTime = label(prev, "Temps estim.: --", 186, 6, F12, C_SUB);
  lv_obj_set_width(g_detailTime, 200);
  lv_obj_set_style_text_align(g_detailTime, LV_TEXT_ALIGN_RIGHT, 0);

  g_detailName = label(parent, "-", 12, 210, F20, C_TXT);
  lv_obj_set_width(g_detailName, 394);
  lv_label_set_long_mode(g_detailName, LV_LABEL_LONG_DOT);

  // Statuts homing / zero piece
  g_detailChipHome = panel(parent, 12, 242, 120, 26, 6, C_CARD, C_BORDER);
  lv_obj_t* ch = label(g_detailChipHome, "Homing", 0, 0, F14, C_SUB); lv_obj_center(ch);
  g_detailChipZero = panel(parent, 140, 242, 120, 26, 6, C_CARD, C_BORDER);
  lv_obj_t* cz = label(g_detailChipZero, "Zero piece", 0, 0, F14, C_SUB); lv_obj_center(cz);

  // Bouton de lancement (visible si conditions OK)
  g_detailBtnGo = button(parent, "Lancer l'usinage", 12, 276, 394, 38, F18,
                         cb_detail_go, nullptr, C_ACC, C_ACC_TX);

  // Message de blocage (visible sinon)
  g_detailMsg = panel(parent, 12, 274, 394, 40, 8, lv_color_hex(0x2A2210), C_WARN);
  lv_obj_t* m = label(g_detailMsg,
      "Faites le homing puis definissez le zero piece pour lancer.",
      8, 0, F14, C_WARN);
  lv_obj_set_width(m, 376);
  lv_obj_align(m, LV_ALIGN_LEFT_MID, 4, 0);
}

static void set_chip(lv_obj_t* chip, bool ok) {
  lv_obj_t* l = lv_obj_get_child(chip, 0);
  lv_obj_set_style_bg_color(chip, ok ? lv_color_hex(0x16301F) : lv_color_hex(0x301618), 0);
  lv_obj_set_style_border_color(chip, ok ? C_OK : C_ERR, 0);
  lv_obj_set_style_text_color(l, ok ? C_OK : C_ERR, 0);
}

static uint32_t g_lastScrollTime = 0;
static lv_coord_t g_lastScrollY = -1;

// Fonction utilitaire pour décharger proprement une miniature SANS recréer d'objet
static void unload_thumbnail(lv_obj_t* row, int i) {
  if (lv_obj_get_child_cnt(row) < 5) return;
  lv_obj_t* th  = lv_obj_get_child(row, 0); // Placeholder
  lv_obj_t* img = lv_obj_get_child(row, 1); // Image
  lv_obj_t* lbl = lv_obj_get_child(row, 4); // Label infos

  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(th, LV_OBJ_FLAG_HIDDEN); // Réaffiche le placeholder

  if (g_thumbs[i].data) {
    free_thumb_buffer(g_thumbs[i].data);
    g_thumbs[i].data = nullptr;
  }

  // Remet les infos à l'état de base
  if (lbl) {
    char info[64];
    if (g_fileIsDir[i]) {
        snprintf(info, sizeof(info), "Dossier");
    } else {
        snprintf(info, sizeof(info), "%lu Ko  |  ...", (unsigned long)(g_fileSizes[i] / 1024));
    }
    lv_label_set_text(lbl, info);
  }
}

// Décharge toutes les miniatures de la liste d'un coup (utile avant d'ouvrir une autre page)
static void unload_all_thumbnails() {
  if (!g_fileList) return;
  uint32_t cnt = lv_obj_get_child_cnt(g_fileList);
  for (uint32_t i = 0; i < cnt && i < (uint32_t)g_fileCount; i++) {
    lv_obj_t* row = lv_obj_get_child(g_fileList, i);
    if (row) unload_thumbnail(row, i);
  }
}

// Fonction appelée périodiquement pour charger/décharger les miniatures visibles
static void manage_thumbnails() {
  if (g_thumbState != THUMB_IDLE) return; // Attendre la fin du scan
  if (!g_fileList || lv_obj_get_child_cnt(g_fileList) == 0) return;
  if (!lv_obj_is_visible(g_fileList)) return; // Pas affiché à l'écran

  uint32_t cnt = lv_obj_get_child_cnt(g_fileList);
  
  lv_coord_t current_scroll_y = lv_obj_get_scroll_y(g_fileList);
  bool is_scrolling = (current_scroll_y != g_lastScrollY);

  if (is_scrolling) {
    g_lastScrollY = current_scroll_y;
    g_lastScrollTime = millis();
    // Si on scroll, on libère TOUTES les miniatures pour fluidifier et économiser la RAM
    for (uint32_t i = 0; i < cnt && i < (uint32_t)g_fileCount; i++) {
      lv_obj_t* row = lv_obj_get_child(g_fileList, i);
      if (row) unload_thumbnail(row, i);
    }
    return;
  }

  uint32_t elapsed = millis() - g_lastScrollTime;
  if (elapsed < 500) return; // Attendre que le défilement se stabilise

  // --- Le scroll est arrêté ---
  lv_area_t list_area;
  lv_obj_get_coords(g_fileList, &list_area);
  // On remet le décalage demandé pour ignorer le premier élément s'il est à moitié coupé
  list_area.y1 += 40; 
  list_area.y2 -= 0;

  int loaded_count = 0;

  for (uint32_t i = 0; i < cnt && i < (uint32_t)g_fileCount; i++) {
    lv_obj_t* row = lv_obj_get_child(g_fileList, i);
    if (!row || lv_obj_get_child_cnt(row) < 5) continue;

    lv_area_t row_area;
    lv_obj_get_coords(row, &row_area);

    lv_area_t res;
    bool visible = _lv_area_intersect(&res, &list_area, &row_area);

    if (!visible) {
      unload_thumbnail(row, i);
      continue;
    }

    // Limite stricte à 4 miniatures simultanées dans la liste 
    // (Laisse 2 slots libres dans le pool pour la page Détail et Job)
    if (loaded_count >= 4) {
      unload_thumbnail(row, i);
      continue;
    }

    if (g_fileIsDir[i]) continue; // Skip directory thumbnails

    // --- Visible et sous la limite : on gère le chargement ---
    lv_obj_t* th  = lv_obj_get_child(row, 0);
    lv_obj_t* img = lv_obj_get_child(row, 1);
    lv_obj_t* lbl = lv_obj_get_child(row, 4);
    
    bool is_loaded = !lv_obj_has_flag(img, LV_OBJ_FLAG_HIDDEN);

    if (is_loaded) {
      loaded_count++;
      continue; // Déjà chargé
    }

    // Tente de charger
    uint32_t estTimeSec = 0;
    lv_obj_t* th_res = make_thumb(row, g_filePaths[i], 48, i, &estTimeSec, img);
    
    if (th_res) {
      lv_obj_add_flag(th, LV_OBJ_FLAG_HIDDEN); // Cache le gris
      lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN); // Affiche l'image
      loaded_count++;
      
      // Met à jour l'info (temps / poids)
      if (lbl) {
        char info[64];
        uint32_t sz = g_fileSizes[i];
        if (estTimeSec > 0) {
          int m = estTimeSec / 60;
          int h = m / 60; m %= 60;
          if (h > 0) snprintf(info, sizeof(info), "%lu Ko  |  ~%dh%02d", (unsigned long)(sz / 1024), h, m);
          else       snprintf(info, sizeof(info), "%lu Ko  |  ~%d min", (unsigned long)(sz / 1024), m);
        } else {
          snprintf(info, sizeof(info), "%lu Ko", (unsigned long)(sz / 1024));
        }
        lv_label_set_text(lbl, info);
      }
    }
  }
}

static void open_detail(const char* path) {
  // On libère la totalité du pool utilisé par la liste pour garantir que
  // la vue détaillée et l'écran d'usinage auront 100% de la RAM disponible.
  unload_all_thumbnails();

  snprintf(g_selPath, sizeof(g_selPath), "%s", path);
  const char* base = strrchr(g_selPath, '/');
  lv_label_set_text(g_detailName, base ? base + 1 : g_selPath);

  // Recharge l'apercu (grande miniature)
  lv_obj_t* prev = (lv_obj_t*)lv_obj_get_user_data(g_pages[PG_DETAIL]);
  if (g_detailImg) { lv_obj_del(g_detailImg); g_detailImg = nullptr; }
  uint32_t estTimeSec = 0;
  g_detailImg = make_thumb(prev, g_selPath, 120, MAX_FILES, &estTimeSec);
  if (g_detailImg) lv_obj_align(g_detailImg, LV_ALIGN_CENTER, 0, 8);

  char tb[32];
  if (estTimeSec > 0) {
      int m = estTimeSec / 60;
      int h = m / 60; m %= 60;
      if (h > 0) snprintf(tb, sizeof(tb), "Temps estim.: ~%dh%02d", h, m);
      else       snprintf(tb, sizeof(tb), "Temps estim.: ~%d min", m);
  } else {
      snprintf(tb, sizeof(tb), "Temps estim.: inconnu");
  }
  lv_label_set_text(g_detailTime, tb);

  // Conditions de lancement
  bool homed = g_fluid->isHomed();
  bool zero  = g_fluid->isWorkZeroSet();
  set_chip(g_detailChipHome, homed);
  set_chip(g_detailChipZero, zero);
  bool ready = homed && zero;
  if (ready) { lv_obj_clear_flag(g_detailBtnGo, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_detailMsg, LV_OBJ_FLAG_HIDDEN); }
  else       { lv_obj_add_flag(g_detailBtnGo, LV_OBJ_FLAG_HIDDEN);   lv_obj_clear_flag(g_detailMsg, LV_OBJ_FLAG_HIDDEN); }

  show_page(PG_DETAIL);
}

// ========================================================================== //
//  Ecran Usinage
// ========================================================================== //
static void build_job(lv_obj_t* parent) {
  lv_obj_t* bar = panel(parent, 0, 0, 418, 40, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_t* st = panel(bar, 12, 7, 90, 26, 6, C_ACC_BG, C_ACC);
  lv_obj_t* sl = label(st, "USINAGE", 0, 0, F12, C_ACC); lv_obj_center(sl);
  g_jobName = label(bar, "-", 110, 11, F14, C_TXT);
  lv_obj_set_width(g_jobName, 200);
  lv_label_set_long_mode(g_jobName, LV_LABEL_LONG_DOT);

  // Apercu + position
  lv_obj_t* prev = panel(parent, 12, 50, 130, 130, 7, lv_color_hex(0x0E0E10), C_BORDER);
  lv_obj_set_user_data(parent, prev);
  g_jobPosLbl = label(parent, "X --  Y --  Z --", 12, 188, F12, C_SUB);

  // Pourcentage + temps
  g_jobPct   = label(parent, "0%", 158, 56, F28, C_ACC);
  g_jobTimes = label(parent, "ecoule --:--\nrestant --:--", 240, 60, F12, C_SUB);
  g_jobLine  = label(parent, "L0/0", 158, 96, F14, C_SUB);

  // Override d'avance
  lv_obj_t* fc = panel(parent, 158, 120, 248, 60, 7, C_CARD, C_BORDER);
  label(fc, "AVANCE (FEED)", 10, 8, F12, C_SUB);
  button(fc, "-",    8,  28, 70, 26, F20, cb_feed_minus, nullptr, C_CARD2, C_TXT);
  button(fc, "100%", 88, 28, 70, 26, F14, cb_feed_reset, nullptr, C_CARD2, C_SUB);
  button(fc, "+",    168, 28, 70, 26, F20, cb_feed_plus, nullptr, C_CARD2, C_TXT);

  // Barre de progression
  g_jobBar = lv_bar_create(parent);
  lv_obj_set_pos(g_jobBar, 12, 236);
  lv_obj_set_size(g_jobBar, 394, 8);
  lv_obj_set_style_bg_color(g_jobBar, C_CARD2, 0);
  lv_obj_set_style_bg_color(g_jobBar, C_ACC, LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_jobBar, 4, 0);
  lv_obj_set_style_radius(g_jobBar, 4, LV_PART_INDICATOR);
  lv_bar_set_range(g_jobBar, 0, 100);
  lv_bar_set_value(g_jobBar, 0, LV_ANIM_OFF);

  // Pause / Stop
  g_jobBtnPause = button(parent, "Pause", 12, 254, 250, 40, F18, cb_job_pause,
                         nullptr, lv_color_hex(0x2A2210), C_WARN);
  button(parent, "Stop", 270, 254, 136, 40, F18, cb_job_stop, nullptr,
         lv_color_hex(0x2A1416), C_ERR);
}

// ========================================================================== //
//  Ecran Palpage
// ========================================================================== //
static void build_probe(lv_obj_t* parent) {
  lv_obj_t* bar = panel(parent, 0, 0, 418, 40, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  label(bar, "Palpage de l'outil - axe Z", 12, 11, F16, C_TXT);

  // Epaisseur du palpeur
  lv_obj_t* c1 = panel(parent, 12, 52, 394, 50, 7, C_CARD, C_BORDER);
  label(c1, "Epaisseur palpeur", 10, 7, F14, C_TXT);
  label(c1, "offset applique au Z", 10, 27, F12, C_MUTE);
  button(c1, "-", 250, 10, 30, 30, F20, cb_thk_minus, nullptr, C_CARD2, C_TXT);
  g_probeThkLbl = lv_label_create(c1);
  lv_obj_set_pos(g_probeThkLbl, 286, 14); lv_obj_set_width(g_probeThkLbl, 56);
  lv_obj_set_style_text_font(g_probeThkLbl, F18, 0);
  lv_obj_set_style_text_color(g_probeThkLbl, C_TXT, 0);
  lv_obj_set_style_text_align(g_probeThkLbl, LV_TEXT_ALIGN_CENTER, 0);
  char pt_b[16]; snprintf(pt_b, sizeof(pt_b), "%.2f", g_probeThk);
  lv_label_set_text(g_probeThkLbl, pt_b);
  button(c1, "+", 350, 10, 30, 30, F20, cb_thk_plus, nullptr, C_CARD2, C_TXT);

  // Dernier palpage (les vitesses de palpage sont fixes : 150 puis 30 mm/min)
  lv_obj_t* c3 = panel(parent, 12, 110, 394, 40, 7, lv_color_hex(0x16301F), C_OK);
  g_probeLastLbl = label(c3, "Dernier palpage : -", 10, 0, F14, C_OK);
  lv_obj_align(g_probeLastLbl, LV_ALIGN_LEFT_MID, 4, 0);

  // Palper Z
  button(parent, "Palper Z", 12, 270, 394, 44, F20, cb_probe, nullptr, C_ACC, C_ACC_TX);
}

// ========================================================================== //
//  Ecran Reglages
// ========================================================================== //
static void settings_row(lv_obj_t* parent, int y, const char* title, const char* sub,
                         const char* value) {
  lv_obj_t* c = panel(parent, 12, y, 394, 48, 8, C_CARD, C_BORDER);
  label(c, title, 12, 8, F16, C_TXT);
  label(c, sub, 12, 28, F12, C_MUTE);
  if (value) {
    lv_obj_t* v = label(c, value, 220, 16, F14, C_SUB);
    lv_obj_set_width(v, 160);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  }
}

static void build_settings(lv_obj_t* parent) {
  lv_obj_t* bar = panel(parent, 0, 0, 418, 40, 0, C_RAIL, C_BORDER);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  label(bar, "Reglages", 12, 11, F16, C_TXT);

  // Calibration tactile
  lv_obj_t* c2 = panel(parent, 12, 108, 394, 40, 8, C_CARD, C_BORDER);
  label(c2, "Calibration tactile", 12, 6, F14, C_TXT);
  label(c2, "recaler le pointage resistif", 12, 24, F12, C_MUTE);
  button(c2, "Lancer", 290, 7, 90, 26, F14, cb_calibrate, nullptr, C_CARD2, C_SUB);

  // Intensite LED
  lv_obj_t* c3 = panel(parent, 12, 154, 394, 56, 8, C_CARD, C_BORDER);
  label(c3, "Intensite LED (WS2812)", 12, 7, F14, C_TXT);
  label(c3, "luminosite de la LED d'etat", 12, 25, F12, C_MUTE);
  
  lv_obj_t* slider = lv_slider_create(c3);
  lv_obj_set_size(slider, 370, 12);
  lv_obj_set_pos(slider, 12, 38);
  lv_slider_set_range(slider, 0, 255);
  lv_slider_set_value(slider, g_ledBright, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, C_LINE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, C_ACC,  LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, C_ACC,  LV_PART_KNOB);
  
  auto cb_led_brightness = [](lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    led_set_brightness((uint8_t)v);
    
    if (e->code == LV_EVENT_RELEASED) {
      Preferences prefs;
      prefs.begin("ui", false);
      prefs.putUChar("led_bright", (uint8_t)v);
      prefs.end();
    }
  };
  lv_obj_add_event_cb(slider, cb_led_brightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slider, cb_led_brightness, LV_EVENT_RELEASED, nullptr);

  // Sensibilite Nunchuk
  lv_obj_t* c4 = panel(parent, 12, 216, 394, 56, 8, C_CARD, C_BORDER);
  label(c4, "Sensibilite Nunchuk", 12, 7, F14, C_TXT);
  label(c4, "vitesse max. du deplacement manuel", 12, 25, F12, C_MUTE);
  
  lv_obj_t* sliderN = lv_slider_create(c4);
  lv_obj_set_size(sliderN, 370, 12);
  lv_obj_set_pos(sliderN, 12, 38);
  lv_slider_set_range(sliderN, 1, 100);
  lv_slider_set_value(sliderN, g_nunchukSensitivity, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sliderN, C_LINE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sliderN, C_ACC,  LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sliderN, C_ACC,  LV_PART_KNOB);
  
  auto cb_nunchuk_sens = [](lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    g_nunchukSensitivity = v;
    
    if (e->code == LV_EVENT_RELEASED) {
      Preferences prefs;
      prefs.begin("ui", false);
      prefs.putInt("nunchuk_sens", v);
      prefs.end();
    }
  };
  lv_obj_add_event_cb(sliderN, cb_nunchuk_sens, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sliderN, cb_nunchuk_sens, LV_EVENT_RELEASED, nullptr);

  // Couleur de reference (accent) — en haut ; appliquee apres redemarrage.
  lv_obj_t* c5 = panel(parent, 12, 48, 394, 54, 8, C_CARD, C_BORDER);
  label(c5, "Couleur de reference", 12, 10, F16, C_TXT);
  label(c5, "accent de l'interface", 12, 32, F12, C_MUTE);

  // Liste d'options "Bleu\nCyan\n..." (statique : lv_dropdown garde le pointeur).
  static char accOpts[160];
  accOpts[0] = '\0';
  for (uint8_t i = 0; i < ACCENT_COUNT; ++i) {
    strcat(accOpts, g_accentPalette[i].name);
    if (i + 1 < ACCENT_COUNT) strcat(accOpts, "\n");
  }

  lv_obj_t* dd = lv_dropdown_create(c5);
  lv_obj_set_pos(dd, 232, 12);
  lv_obj_set_size(dd, 150, 30);
  lv_dropdown_set_options_static(dd, accOpts);
  lv_dropdown_set_selected(dd, g_accentIdx);
  lv_obj_set_style_bg_color(dd, C_CARD2, 0);
  lv_obj_set_style_border_color(dd, C_LINE, 0);
  lv_obj_set_style_border_width(dd, 1, 0);
  lv_obj_set_style_radius(dd, 7, 0);
  lv_obj_set_style_text_color(dd, C_TXT, 0);
  lv_obj_set_style_text_font(dd, F14, 0);
  lv_obj_t* ddlist = lv_dropdown_get_list(dd);
  lv_obj_set_style_bg_color(ddlist, C_RAIL, 0);
  lv_obj_set_style_border_color(ddlist, C_LINE, 0);
  lv_obj_set_style_text_color(ddlist, C_TXT, 0);
  lv_obj_set_style_text_font(ddlist, F14, 0);

  // Selection : validation + sauvegarde immediate en NVS, puis redemarrage
  // (C_ACC etant fige a la construction des pages, le reboot applique la couleur).
  lv_obj_add_event_cb(dd, [](lv_event_t* e) {
    lv_obj_t* d = lv_event_get_target(e);
    uint8_t sel = (uint8_t)lv_dropdown_get_selected(d);
    if (sel == g_accentIdx) return;            // pas de changement -> rien a faire
    Preferences prefs;
    prefs.begin("ui", false);
    prefs.putUChar("accent_idx", sel);         // persiste la couleur de reference
    prefs.end();
    delay(120);
    ESP.restart();                             // reboot pour appliquer l'accent
  }, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ========================================================================== //
//  Init + mises a jour
// ========================================================================== //
void ui_init(FluidNC* fluid, JobRunner* job) {
  g_fluid = fluid;
  g_job   = job;

  Preferences prefs;
  prefs.begin("ui", false);
  if (!prefs.isKey("probe_thk")) {
    prefs.putFloat("probe_thk", 20.0f);
  }
  g_probeThk = prefs.getFloat("probe_thk", 20.0f);
  
  if (!prefs.isKey("led_bright")) {
    prefs.putUChar("led_bright", 255);
  }
  g_ledBright = prefs.getUChar("led_bright", 255);
  led_set_brightness(g_ledBright);
  
  if (!prefs.isKey("nunchuk_sens")) {
    prefs.putInt("nunchuk_sens", 50);
  }
  g_nunchukSensitivity = prefs.getInt("nunchuk_sens", 50);

  // Couleur de reference (accent) : relue ici puis appliquee AVANT toute
  // construction d'UI, pour que rail/pages/boutons prennent la bonne couleur.
  if (!prefs.isKey("accent_idx")) {
    prefs.putUChar("accent_idx", ACCENT_DEFAULT);
  }
  apply_accent(prefs.getUChar("accent_idx", ACCENT_DEFAULT));

  prefs.end();

  // Theme LVGL neutralise : sans cela, le theme par defaut applique un BLEU fixe
  // aux sliders, scrollbars et surbrillances (independamment de nos #define).
  // Primaire = accent courant, secondaire = gris neutre, mode sombre.
  lv_disp_t* disp = lv_disp_get_default();
  lv_theme_t* th = lv_theme_default_init(disp, C_ACC,
                                         lv_palette_main(LV_PALETTE_GREY),
                                         true, LV_FONT_DEFAULT);
  lv_disp_set_theme(disp, th);

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  build_rail(scr);

  // Conteneur de contenu (a droite du rail)
  g_content = lv_obj_create(scr);
  lv_obj_set_pos(g_content, 62, 0);
  lv_obj_set_size(g_content, 418, 320);
  lv_obj_set_style_bg_color(g_content, C_BG, 0);
  lv_obj_set_style_border_width(g_content, 0, 0);
  lv_obj_set_style_pad_all(g_content, 0, 0);
  lv_obj_clear_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < PG_COUNT; ++i) {
    g_pages[i] = lv_obj_create(g_content);
    lv_obj_set_pos(g_pages[i], 0, 0);
    lv_obj_set_size(g_pages[i], 418, 320);
    lv_obj_set_style_bg_color(g_pages[i], C_BG, 0);
    lv_obj_set_style_border_width(g_pages[i], 0, 0);
    lv_obj_set_style_pad_all(g_pages[i], 0, 0);
    lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  build_pilot(g_pages[PG_PILOT]);
  build_orig(g_pages[PG_ORIG]);
  build_files(g_pages[PG_FILES]);
  build_detail(g_pages[PG_DETAIL]);
  build_job(g_pages[PG_JOB]);
  build_probe(g_pages[PG_PROBE]);
  build_settings(g_pages[PG_SETTINGS]);

  show_page(PG_PILOT);
}

static void fmt_mmss(uint32_t ms, char* out, size_t n) {
  uint32_t s = ms / 1000; snprintf(out, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void ui_update(const MachineStatus& st) {
  bool zeroSet = g_fluid->isWorkZeroSet();
  for (int i = 0; i < 3; ++i) {
    char b[20];
    snprintf(b, sizeof(b), "%.2f", st.mpos[i]);
    lv_label_set_text(g_lblMach[i], b);
    if (zeroSet) { snprintf(b, sizeof(b), "%.2f", st.wpos[i]); lv_label_set_text(g_lblPiece[i], b); }
    else         { lv_label_set_text(g_lblPiece[i], "?.??"); }
  }

  const char* txt = "----"; lv_color_t col = C_MUTE;
  switch (st.state) {
    case MachineState::Idle:  txt = "PRET";  col = C_ACC; break;
    case MachineState::Run:   txt = "RUN";   col = C_ACC; break;
    case MachineState::Jog:   txt = "JOG";   col = C_ACC; break;
    case MachineState::Hold:  txt = "HOLD";  col = C_WARN; break;
    case MachineState::Alarm: txt = "ALARM"; col = C_ERR; break;
    case MachineState::Door:  txt = "DOOR";  col = C_WARN; break;
    case MachineState::Home:  txt = "HOME";  col = C_ACC; break;
    case MachineState::Sleep: txt = "SLEEP"; col = C_MUTE; break;
    case MachineState::Check: txt = "CHECK"; col = C_ACC; break;
    default: break;
  }
  if (g_lblState) lv_label_set_text(g_lblState, txt);
  lv_obj_set_style_text_color(g_lblState, C_TXT, 0);             // texte du voyant en blanc
  if (g_dotState) lv_obj_set_style_bg_color(g_dotState, col, 0); // couleur d'etat sur la pastille

  if (g_btnUnlock) {
    if (st.state == MachineState::Alarm) {
      lv_obj_clear_state(g_btnUnlock, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(g_btnUnlock, LV_STATE_DISABLED);
    }
  }

  char fs[32];
  snprintf(fs, sizeof(fs), "F%.0f  Broche: %s", st.feed, (st.spindle > 0) ? "ON" : "OFF");
  lv_label_set_text(g_lblFS, fs);

  // Position live sur l'ecran d'usinage
  if (g_jobPosLbl) {
    char b[40]; snprintf(b, sizeof(b), "X%.1f  Y%.1f  Z%.1f", st.wpos[0], st.wpos[1], st.wpos[2]);
    lv_label_set_text(g_jobPosLbl, b);
  }

  // Track state transitions for probing during M6
  if (g_fluid->probeJustFinished()) {
    g_fluid->clearProbeJustFinished();
    if (g_job->probeRunningForM6()) {
      g_job->setProbeRunningForM6(false);
      show_page(PG_JOB);
    }
  } else if (g_job->probeRunningForM6() && st.state == MachineState::Alarm) {
    g_job->setProbeRunningForM6(false);
  }

  // Update Pause button text
  if (g_jobBtnPause) {
    static bool lastPausedState = false;
    bool currentPausedState = false;
    
    if (g_job->isActive() && g_job->isPaused()) {
      if (g_job->m6State() == JobRunner::M6State::WaitToolChange) {
        // Wait 20 seconds before switching to 'Reprendre' to allow machine movement to finish
        if (millis() - g_job->pauseStartMs() >= 20000) {
          currentPausedState = true;
        } else {
          currentPausedState = false;
        }
      } else {
        currentPausedState = true;
      }
    }

    if (currentPausedState != lastPausedState) {
      lv_obj_t* l = lv_obj_get_child(g_jobBtnPause, 0);
      if (l) lv_label_set_text(l, currentPausedState ? "Reprendre" : "Pause");
      lastPausedState = currentPausedState;
    }
  }

  // Etat du Homing (coche verte sur bouton et voile de l'onglet ORIG)
  bool homed = g_fluid->isHomed();
  static bool lastHomed = false;
  static bool firstHomed = true;
  if (firstHomed || homed != lastHomed) {
    if (g_btnHoming) {
      lv_obj_t* l = lv_obj_get_child(g_btnHoming, 0);
      if (l) {
        if (homed) lv_label_set_text(l, "Homing  #00FF00 " LV_SYMBOL_OK "#");
        else       lv_label_set_text(l, "Homing");
      }
    }
    if (g_origOverlay) {
      if (homed) lv_obj_add_flag(g_origOverlay, LV_OBJ_FLAG_HIDDEN);
      else       lv_obj_clear_flag(g_origOverlay, LV_OBJ_FLAG_HIDDEN);
    }
    firstHomed = false;
    lastHomed = homed;
  }
}

void ui_task() {
  manage_thumbnails();
  if (!g_job) return;

  // Bascule automatique : si le job se termine/erreur, mettre la barre a 100 %.
  if (g_job->isActive() || g_job->state() == JobRunner::State::Done) {
    uint8_t pct = g_job->progressPercent();
    lv_bar_set_value(g_jobBar, pct, LV_ANIM_OFF);

    char b[16]; snprintf(b, sizeof(b), "%u%%", pct);
    lv_label_set_text(g_jobPct, b);

    snprintf(b, sizeof(b), "L%lu/%lu", (unsigned long)g_job->lineNumber(),
             (unsigned long)g_job->totalLines());
    lv_label_set_text(g_jobLine, b);

    char el[8], re[8];
    uint32_t elapsed = g_job->elapsedMs();
    fmt_mmss(elapsed, el, sizeof(el));
    
    uint32_t remain = 0;
    if (g_jobEstTimeSec > 0) {
      uint32_t elapsedSec = elapsed / 1000;
      if (g_jobEstTimeSec > elapsedSec) remain = (g_jobEstTimeSec - elapsedSec) * 1000;
      else remain = 0;
    } else {
      remain = (pct > 0 && pct < 100)
                        ? (uint32_t)((uint64_t)elapsed * (100 - pct) / pct) : 0;
    }
    
    fmt_mmss(remain, re, sizeof(re));
    char t[40]; snprintf(t, sizeof(t), "ecoule %s\nrestant %s", el, re);
    lv_label_set_text(g_jobTimes, t);
  }

  // Basculer sur l'onglet Fichiers 30 secondes après la fin d'un usinage
  static JobRunner::State lastJobState = JobRunner::State::Idle;
  static uint32_t jobDoneTimeMs = 0;

  if (g_job->state() != lastJobState) {
    if (g_job->state() == JobRunner::State::Done) {
      jobDoneTimeMs = millis();
    }
    lastJobState = g_job->state();
  }

  if (g_job->state() == JobRunner::State::Done && jobDoneTimeMs != 0) {
    if (millis() - jobDoneTimeMs >= 30000) {
      jobDoneTimeMs = 0;
      show_page(PG_FILES);
    }
  }
}
