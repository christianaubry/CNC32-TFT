// GcodeThumb.h — génération de miniatures G-code (option A : parcours d'outil).
//
// Lit un fichier G-code sur la carte SD, en projette le parcours de coupe dans
// le plan XY (vue de dessus), et rastérise le tout dans une miniature 1 bit/px
// mise en cache sur la SD pour ne pas la régénérer à chaque ouverture.
//
// Périmètre : détection des fichiers, génération et cache.
// L'affichage des vignettes dans la liste de sélection n'est PAS géré ici —
// la couche UI consommera load() / cachePath().
//
// Représentation : seules les avances de coupe (G1/G2/G3) sont tracées ; les
// rapides G0 sont ignorés pour un rendu propre. Le cadrage est calculé sur la
// boîte englobante des coupes, donc la pièce remplit la miniature. Ce n'est
// PAS la matière enlevée mais bien le parcours d'outil projeté.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <FS.h>

namespace gthumb {

// Dimensions fixes de la miniature (8 bits/pixel, masque alpha).
constexpr uint16_t THUMB_W     = 60;
constexpr uint16_t THUMB_H     = 60;
constexpr size_t   THUMB_BYTES = THUMB_W * THUMB_H;  // 72 * 72 = 5184 o

// true si `name` (nom ou chemin) porte une extension G-code reconnue.
bool isGcodeFile(const char* name);

// Chemin du fichier de cache associé à un G-code.
// Ex : "/jobs/part.nc" -> "/.thumbs/jobs~part.nc.thb" (chemin aplati).
String cachePath(const char* gcodePath);

// true si un cache valide et à jour existe (en-tête, dimensions et taille du
// fichier source concordent ; la date de modif sert d'indice secondaire).
bool cacheValid(const char* gcodePath);

// Génère la miniature de `gcodePath` et l'écrit dans le cache SD.
// Si `force` est faux et qu'un cache valide existe déjà, ne fait rien et
// renvoie true. Renvoie false en cas d'erreur de lecture/écriture.
bool generate(const char* gcodePath, uint32_t srcSize, uint32_t srcMtime);

// Parcourt `dir` et génère les miniatures manquantes/périmées des G-code.
// `recursive` descend dans les sous-dossiers (en sautant le dossier de cache).
// Renvoie le nombre de miniatures effectivement (re)générées.
int scanAndGenerate(const char* dir = "/", bool recursive = false);

// Charge les pixels d'un cache existant dans `outBits` (capacité `outCap`).
// Renseigne *w/*h si fournis. Destiné à la future couche d'affichage.
// Renvoie false si le cache est absent/invalide ou le tampon trop petit.
bool load(const char* gcodePath, uint8_t* outBits, size_t outCap,
          uint16_t* w = nullptr, uint16_t* h = nullptr, uint32_t* estTimeSec = nullptr);

}  // namespace gthumb
