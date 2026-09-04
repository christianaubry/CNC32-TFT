// GcodeThumb.cpp — génération de miniatures G-code (option A).
//
// Pipeline en deux passes sur le fichier (jamais chargé en RAM) :
//   1. boîte englobante des coupes (G1/G2/G3) -> échelle/centrage,
//   2. rastérisation des segments dans un tampon 1 bit/px.
// Le parsing est factorisé : streamFile() émet des segments via un callback
// (EmitFn) ; deux visiteurs s'y branchent (boîte englobante puis raster).
#include "GcodeThumb.h"

#include <SD.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <esp_task_wdt.h>

namespace gthumb {
namespace {

constexpr int   PAD           = 3;       // marge en pixels autour du tracé
constexpr float ARC_STEP_DEG  = 5.0f;    // pas d'interpolation des arcs
constexpr int   ARC_STEPS_MAX = 180;     // garde-fou sur les grands arcs
constexpr float DEG2RAD       = 0.01745329252f;
constexpr float kTwoPi        = 6.28318530718f;  // TWO_PI est déjà un macro d'Arduino.h
const char*     THUMB_DIR     = "/thumbs";

// En-tête du fichier de cache (.thb). Compact, sans rembourrage.
struct __attribute__((packed)) ThumbHeader {
  char     magic[4];   // "CNTE"
  uint16_t w, h;
  uint8_t  bpp;        // 8
  uint8_t  reserved;
  uint32_t srcSize;    // taille du G-code source (invalidation principale)
  uint32_t srcMtime;   // date de modif FAT (indice secondaire)
  uint32_t estTimeSec; // Temps d'usinage estimé
};

// État modal du parseur G-code (persiste entre les lignes).
struct GState {
  float x = 0, y = 0, z = 0;
  bool  absolute = true;   // G90 (vs G91 relatif)
  int   motion   = 0;      // mode de déplacement modal courant : 0/1/2/3
  float feed     = 1000.0f;
};

// Callback recevant un segment en coordonnées « monde » (unités du fichier).
typedef void (*EmitFn)(void* ctx, float x0, float y0, float z0, float x1, float y1, float z1, int motion, float feed);

// --- Interpolation d'un arc G2/G3 en petits segments -----------------------
void emitArc(const GState& st, float ex, float ey, float ez, int dir,
             bool hasIJ, float I, float J, bool hasR, float R,
             EmitFn emit, void* ctx) {
  const float sx = st.x, sy = st.y;
  float cx, cy;
  if (hasIJ) {                          // centre = départ + (I,J), toujours incrémental
    cx = sx + I; cy = sy + J;
  } else if (hasR) {                    // arc défini par rayon
    const float dx = ex - sx, dy = ey - sy;
    const float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1e-6f) return;              // dégénéré : R sans déplacement
    float hsq = R * R - d * d * 0.25f;
    if (hsq < 0) hsq = 0;
    const float h = sqrtf(hsq);
    const float ux = -dy / d, uy = dx / d;
    const float sign = (dir == 2 ? -1.0f : 1.0f) * (R < 0 ? -1.0f : 1.0f);
    cx = (sx + ex) * 0.5f + sign * h * ux;
    cy = (sy + ey) * 0.5f + sign * h * uy;
  } else {                              // ni I/J ni R : on dégrade en ligne
    emit(ctx, sx, sy, st.z, ex, ey, ez, st.motion, st.feed);
    return;
  }

  const float r = sqrtf((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
  if (r < 1e-6f) { emit(ctx, sx, sy, st.z, ex, ey, ez, st.motion, st.feed); return; }

  float a0 = atan2f(sy - cy, sx - cx);
  float a1 = atan2f(ey - cy, ex - cx);
  const bool fullCircle = hasIJ && fabsf(ex - sx) < 1e-4f && fabsf(ey - sy) < 1e-4f;
  if (dir == 2) {                       // G2 horaire : balayage négatif
    if (a1 >= a0) a1 -= kTwoPi;
    if (fullCircle) a1 = a0 - kTwoPi;
  } else {                              // G3 antihoraire : balayage positif
    if (a1 <= a0) a1 += kTwoPi;
    if (fullCircle) a1 = a0 + kTwoPi;
  }

  const float sweep = a1 - a0;
  int steps = (int)ceilf(fabsf(sweep) / (ARC_STEP_DEG * DEG2RAD));
  if (steps < 2) steps = 2;
  if (steps > ARC_STEPS_MAX) steps = ARC_STEPS_MAX;

  float px = sx, py = sy, pz = st.z;
  for (int i = 1; i <= steps; ++i) {
    const float a  = a0 + sweep * i / steps;
    const float nx = cx + r * cosf(a);
    const float ny = cy + r * sinf(a);
    const float nz = st.z + (ez - st.z) * i / steps;
    emit(ctx, px, py, pz, nx, ny, nz, st.motion, st.feed);
    px = nx; py = ny; pz = nz;
  }
}

static bool parseFloat(char** pptr, float* outVal) {
  char* p = *pptr;
  while (*p == ' ' || *p == '\t') ++p;
  bool neg = false;
  if (*p == '-') { neg = true; ++p; }
  else if (*p == '+') { ++p; }
  float v = 0.0f;
  bool hasDig = false;
  while (*p >= '0' && *p <= '9') {
    v = v * 10.0f + (*p - '0');
    ++p; hasDig = true;
  }
  if (*p == '.' || *p == ',') {
    ++p;
    float frac = 1.0f;
    while (*p >= '0' && *p <= '9') {
      frac *= 0.1f;
      v += (*p - '0') * frac;
      ++p; hasDig = true;
    }
  }
  if (!hasDig) return false; // signale qu'aucun nombre n'a été lu
  *pptr = p;
  *outVal = neg ? -v : v;
  return true;
}

void processLine(char* s, GState& st, EmitFn emit, void* ctx) {
  float vx = st.x, vy = st.y, vz = st.z;
  bool  hasX = false, hasY = false, hasZ = false;
  bool  hasI = false, hasJ = false, hasR = false;
  float I = 0, J = 0, R = 0;
  int   lineMotion = st.motion;
  bool  inParen    = false;

  for (char* p = s; *p;) {
    char c = *p;
    if (inParen) { if (c == ')') inParen = false; ++p; continue; }
    if (c == '(') { inParen = true; ++p; continue; }
    if (c == ';') break;                               // commentaire fin de ligne
    if (c >= 'a' && c <= 'z') c -= 32;                 // majuscule
    if (c < 'A' || c > 'Z') { ++p; continue; }

    const char letter = c;
    ++p;
    float val = 0.0f;
    if (!parseFloat(&p, &val)) continue;                          // lettre sans nombre (M, T…)
    
    switch (letter) {
      case 'G': {
        const int g = (int)(val + (val < 0 ? -0.5f : 0.5f));
        if (g >= 0 && g <= 3) { lineMotion = g; }
        else if (g == 90) st.absolute = true;
        else if (g == 91) st.absolute = false;
        // G20/G21 ignorés : la normalisation gère l'échelle.
      } break;
      case 'X': vx = st.absolute ? val : st.x + val; hasX = true; break;
      case 'Y': vy = st.absolute ? val : st.y + val; hasY = true; break;
      case 'Z': vz = st.absolute ? val : st.z + val; hasZ = true; break;
      case 'I': I = val; hasI = true; break;
      case 'J': J = val; hasJ = true; break;
      case 'R': R = val; hasR = true; break;
      case 'F': st.feed = val; break;
      default: break;                                  // S, M, N, T, K… ignorés
    }
  }
  st.motion = lineMotion;

  const bool isArc     = (lineMotion == 2 || lineMotion == 3);
  const bool xyChanged = (vx != st.x) || (vy != st.y);
  const bool zChanged  = (vz != st.z);

  if (isArc && (xyChanged || hasI || hasJ)) {
    // I et J sont optionnels indépendamment (l'absent vaut 0).
    emitArc(st, vx, vy, vz, lineMotion, hasI || hasJ, I, J, hasR, R, emit, ctx);
  } else if (!isArc && (xyChanged || zChanged)) {
    emit(ctx, st.x, st.y, st.z, vx, vy, vz, lineMotion, st.feed); 
  }
  st.x = vx; st.y = vy; st.z = vz;
}

// --- Lecture tamponnée du fichier, ligne par ligne -------------------------
void streamFile(File& f, EmitFn emit, void* ctx, int* lineCount = nullptr) {
  GState st;
  char    line[200];
  int     len = 0;
  uint8_t buf[512];

  while (f.available()) {
    esp_task_wdt_reset();
    const int n = f.read(buf, sizeof buf);
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) {
      const char c = (char)buf[i];
      if (c == '\n' || c == '\r') {
        if (len > 0) {
          line[len] = '\0';
          processLine(line, st, emit, ctx);
          if (lineCount) (*lineCount)++;
          len = 0;
        }
      } else if (len < 199) {
        line[len++] = c;
      }
    }
  }
  if (len > 0) { line[len] = 0; processLine(line, st, emit, ctx); if (lineCount) (*lineCount)++; }
}

// --- Visiteur 1 : boîte englobante des coupes ------------------------------
struct BBox { float minx, miny, maxx, maxy; bool any; float timeSec; };

void bboxEmit(void* ctx, float x0, float y0, float z0, float x1, float y1, float z1, int motion, float feed) {
  BBox* b = (BBox*)ctx;
  
  float dx = x1 - x0;
  float dy = y1 - y0;
  float dz = z1 - z0;
  float dist = sqrtf(dx*dx + dy*dy + dz*dz);
  
  float f = (motion == 0) ? 2000.0f : feed;
  if (f <= 0) f = 1000.0f;
  b->timeSec += (dist / f) * 60.0f;

  bool cutting = (motion > 0) && (z0 <= 0.0f || z1 <= 0.0f);
  if (!cutting) return;
  
  const float xs[2] = {x0, x1}, ys[2] = {y0, y1};
  for (int k = 0; k < 2; ++k) {
    if (!b->any) { b->minx = b->maxx = xs[k]; b->miny = b->maxy = ys[k]; b->any = true; }
    else {
      if (xs[k] < b->minx) b->minx = xs[k];
      if (xs[k] > b->maxx) b->maxx = xs[k];
      if (ys[k] < b->miny) b->miny = ys[k];
      if (ys[k] > b->maxy) b->maxy = ys[k];
    }
  }
}

// --- Visiteur 2 : rastérisation 1 bit/px -----------------------------------
struct Raster {
  uint8_t* bits;
  int      w, h;
  float    minx, maxy;     // origine monde (coin haut-gauche)
  float    scale;          // pixels par unité
  float    offx, offy;     // décalage de centrage en pixels
};

inline void rasterPx(Raster* r, int x, int y, bool cutting) {
  if (!cutting) return; // Laisser neutre pour G0
  if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
  const int idx = y * r->w + x;
  int v = r->bits[idx] + 64; // Ajoute de l'intensité à chaque passage (blanc)
  r->bits[idx] = v > 255 ? 255 : v;
}

void rasterLine(Raster* r, int x0, int y0, int x1, int y1, bool cutting) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    rasterPx(r, x0, y0, cutting);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void rasterEmit(void* ctx, float wx0, float wy0, float wz0, float wx1, float wy1, float wz1, int motion, float feed) {
  bool cutting = (motion > 0) && (wz0 <= 0.0f || wz1 <= 0.0f);
  if (!cutting) return;
  Raster* r = (Raster*)ctx;
  const int x0 = (int)lroundf((wx0 - r->minx) * r->scale + r->offx);
  const int y0 = (int)lroundf((r->maxy - wy0) * r->scale + r->offy);  // Y vers le haut
  const int x1 = (int)lroundf((wx1 - r->minx) * r->scale + r->offx);
  const int y1 = (int)lroundf((r->maxy - wy1) * r->scale + r->offy);
  rasterLine(r, x0, y0, x1, y1, true);
}

// --- Cache SD ---------------------------------------------------------------
bool readSrcMeta(const char* path, uint32_t* size, uint32_t* mtime) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  *size  = (uint32_t)f.size();
  *mtime = (uint32_t)f.getLastWrite();
  f.close();
  return true;
}

bool writeCache(const char* path, const uint8_t* bits, size_t n,
                uint32_t srcSize, uint32_t srcMtime, uint32_t estTimeSec) {
  if (!SD.exists(THUMB_DIR)) SD.mkdir(THUMB_DIR);
  const String cp = cachePath(path);
  File c = SD.open(cp.c_str(), FILE_WRITE);            // "w" : crée/tronque
  if (!c) return false;

  ThumbHeader h;
  memcpy(h.magic, "CNTF", 4);
  h.w = THUMB_W; h.h = THUMB_H; h.bpp = 8; h.reserved = 0;
  h.srcSize = srcSize; h.srcMtime = srcMtime;
  h.estTimeSec = estTimeSec;

  bool ok = c.write((const uint8_t*)&h, sizeof h) == sizeof h;
  ok = ok && (c.write(bits, n) == n);
  c.close();
  return ok;
}

}  // namespace

// ===========================================================================
// API publique
// ===========================================================================

bool isGcodeFile(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  char ext[8]; int j = 0;
  for (const char* p = dot + 1; *p && j < 7; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    ext[j++] = c;
  }
  ext[j] = 0;
  static const char* exts[] = {"nc", "gc", "gco", "gcode", "ngc", "tap", "cnc"};
  for (const char* e : exts) if (strcmp(ext, e) == 0) return true;
  return false;
}

String cachePath(const char* gcodePath) {
  String s = gcodePath;
  if (s.startsWith("/")) s = s.substring(1);
  s.replace('/', '~');                                 // aplatit l'arborescence
  return String(THUMB_DIR) + "/" + s + ".thb";
}

bool cacheValid(const char* gcodePath, uint32_t srcSize, uint32_t srcMtime) {
  File c = SD.open(cachePath(gcodePath).c_str(), FILE_READ);
  if (!c) return false;
  
  uint32_t fileSize = c.size(); // Vérifie la taille totale du cache
  
  ThumbHeader h;
  if (c.read((uint8_t*)&h, sizeof h) != (int)sizeof h) { c.close(); return false; }
  c.close();
  
  if (memcmp(h.magic, "CNTF", 4) != 0) return false;
  if (h.w != THUMB_W || h.h != THUMB_H) return false;
  if (h.srcSize != srcSize) return false;
  if (h.srcMtime != 0 && srcMtime != 0 && h.srcMtime != srcMtime) return false;
  
  // Vérifie que le fichier de cache n'a pas été tronqué lors d'un plantage précédent
  if (fileSize != sizeof(ThumbHeader) + (h.w * h.h)) return false;
  
  return true;
}

bool cacheValid(const char* gcodePath) {
  uint32_t srcSize, srcMtime;
  if (!readSrcMeta(gcodePath, &srcSize, &srcMtime)) return false;
  return cacheValid(gcodePath, srcSize, srcMtime);
}

bool generate(const char* gcodePath, uint32_t srcSize, uint32_t srcMtime) {
  File f = SD.open(gcodePath, FILE_READ);
  if (!f) return false;
  
  BBox bb{0, 0, 0, 0, false, 0};
  streamFile(f, bboxEmit, &bb, nullptr);
  f.close();

  // Tampon miniature (648 o sur la pile).
  uint8_t bits[THUMB_BYTES];
  memset(bits, 0, sizeof bits);

  // Passe 2 : rastérisation (sautée si aucune coupe -> miniature vierge).
  if (bb.any) {
    const float PAD = 4.0f; // Marge interne de 4 pixels
    const float availW = (float)(THUMB_W) - 2.0f * PAD;
    const float availH = (float)(THUMB_H) - 2.0f * PAD;
    const float rangeX = bb.maxx - bb.minx;
    const float rangeY = bb.maxy - bb.miny;

    const float sX = rangeX > 1e-6f ? availW / rangeX : availW;
    const float sY = rangeY > 1e-6f ? availH / rangeY : availH;
    
    Raster r;
    r.scale = sX < sY ? sX : sY;
    Serial.printf("GCODE_DEBUG: rangeX=%.1f, rangeY=%.1f, scale=%.3f\n", rangeX, rangeY, r.scale);
    r.bits  = bits; r.w = THUMB_W; r.h = THUMB_H;
    r.minx  = bb.minx; r.maxy = bb.maxy;
    r.offx  = PAD + (availW - rangeX * r.scale) * 0.5f;
    r.offy  = PAD + (availH - rangeY * r.scale) * 0.5f;

    File f2 = SD.open(gcodePath, FILE_READ);
    if (f2) {
      streamFile(f2, rasterEmit, &r, nullptr); 
      f2.close();
    }
  }

  return writeCache(gcodePath, bits, sizeof bits, srcSize, srcMtime, (uint32_t)bb.timeSec + 30);
}

#include <vector>

static void scanDirRecurse(const char* dir, bool recursive, std::vector<String>& validPaths, int& count) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;

  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    esp_task_wdt_reset();
    const String full = e.path();                      // chemin complet (arduino-esp32 v2+)
    if (e.isDirectory()) {
      if (recursive && full != THUMB_DIR) {
        scanDirRecurse(full.c_str(), true, validPaths, count);
      }
      e.close();
    } else if (isGcodeFile(e.name())) {
      const uint32_t sz = e.size();
      const time_t   mt = e.getLastWrite();
      e.close();

      validPaths.push_back(cachePath(full.c_str()));

      if (cacheValid(full.c_str(), sz, mt)) continue;

      if (generate(full.c_str(), sz, mt)) {++count;}
    } else {
      e.close();
    }
  }
  d.close();
}

int scanAndGenerate(const char* dir, bool recursive) {
  std::vector<String> validPaths;
  int count = 0;
  scanDirRecurse(dir, recursive, validPaths, count);

  // Nettoyage des miniatures obsolètes (fichiers G-code supprimés)
  File td = SD.open(THUMB_DIR);
  if (td && td.isDirectory()) {
    for (File e = td.openNextFile(); e; e = td.openNextFile()) {
      if (!e.isDirectory()) {
        String p = e.path();
        e.close();
        bool found = false;
        for (const String& vp : validPaths) {
          if (p == vp) { found = true; break; }
        }
        if (!found) {
          SD.remove(p.c_str());
          Serial.printf("GCODE_DEBUG: Suppression cache orphelin %s\n", p.c_str());
        }
      } else {
        e.close();
      }
    }
    td.close();
  }
  return count;
}

bool load(const char* gcodePath, uint8_t* outBits, size_t outCap,
          uint16_t* w, uint16_t* h, uint32_t* estTimeSec) {
  File c = SD.open(cachePath(gcodePath).c_str(), FILE_READ);
  if (!c) {
    Serial.printf("load: SD.open failed for %s\n", cachePath(gcodePath).c_str());
    return false;
  }
  ThumbHeader hd;
  if (c.read((uint8_t*)&hd, sizeof hd) != (int)sizeof hd) {
    Serial.printf("load: failed to read ThumbHeader for %s\n", gcodePath);
    c.close(); return false;
  }
  if (memcmp(hd.magic, "CNTF", 4) != 0) {
    Serial.printf("load: bad magic for %s\n", gcodePath);
    c.close(); return false;
  }

  const size_t need = (size_t)hd.w * hd.h;
  if (need > outCap) {
    Serial.printf("load: need > outCap (%u > %u) for %s\n", need, outCap, gcodePath);
    c.close(); return false;
  }
  const size_t rd = c.read(outBits, need);
  c.close();
  if (rd != need) {
    Serial.printf("load: read mismatch (rd=%u, need=%u) for %s\n", rd, need, gcodePath);
    return false;
  }

  if (w) *w = hd.w;
  if (h) *h = hd.h;
  if (estTimeSec) *estTimeSec = hd.estTimeSec;
  return true;
}

}  // namespace gthumb
