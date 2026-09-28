#pragma once
// Everything that makes one particular pet: its look, stats, tricks and settings.
#include <Arduino.h>
#include "config.h"

// ---- Look -------------------------------------------------------------------
#define AVATAR_FIELDS 12
struct Avatar {
  uint8_t eyes, size, gap, pupil, lashes, brows, hat, glasses, face, mouth, cheeks, neck;
};
static_assert(sizeof(Avatar) == AVATAR_FIELDS, "Avatar is sent as raw bytes");

// Number of choices per field (keep in sync with app/js/looks.js).
constexpr uint8_t AV_COUNTS[AVATAR_FIELDS] = {8, 5, 5, 7, 3, 5, 20, 11, 8, 6, 5, 6};

enum EyeShape : uint8_t { EYE_ROUND, EYE_OVAL, EYE_ROBO, EYE_WIDE, EYE_DROOPY, EYE_ANIME, EYE_CAT, EYE_PIXEL };
enum PupilStyle : uint8_t { PUP_DOT, PUP_BIG, PUP_TINY, PUP_RING, PUP_SPARKLE, PUP_NONE, PUP_SLIT };
enum Lashes : uint8_t { LA_NONE, LA_FLIRTY, LA_FULL };
enum Brows : uint8_t { BR_NONE, BR_THIN, BR_BOLD, BR_FLUFFY, BR_UNI };
enum Hat : uint8_t {
  HAT_NONE, HAT_TOP, HAT_PARTY, HAT_CROWN, HAT_BEANIE, HAT_CAP, HAT_COWBOY, HAT_WIZARD, HAT_CHEF, HAT_BOW,
  HAT_FLOWER, HAT_PHONES, HAT_HALO, HAT_HORNS, HAT_ANTENNA, HAT_BUNNY, HAT_CAT, HAT_PROPELLER, HAT_PIRATE,
  HAT_VIKING
};
enum Glasses : uint8_t {
  GL_NONE, GL_ROUND, GL_SQUARE, GL_SHADES, GL_HEART, GL_STAR, GL_MONOCLE, GL_3D, GL_NERD, GL_VISOR, GL_GOGGLES
};
enum FaceAcc : uint8_t { FA_NONE, FA_MOUSTACHE, FA_HANDLEBAR, FA_WALRUS, FA_PENCIL, FA_CURLY, FA_BEARD, FA_GOATEE };
enum MouthStyle : uint8_t { MO_NONE, MO_SMILE, MO_GRIN, MO_CAT, MO_FANGS, MO_TONGUE };
enum Cheeks : uint8_t { CH_NONE, CH_BLUSH, CH_FRECKLES, CH_BOTH, CH_WHISKERS };
enum Neck : uint8_t { NK_NONE, NK_BOWTIE, NK_SCARF, NK_COLLAR, NK_BANDANA, NK_PEARLS };

// ---- Life -------------------------------------------------------------------
enum Stage : uint8_t { ST_EGG, ST_BABY, ST_KID, ST_TEEN, ST_ADULT };

enum Trait : uint8_t { TR_PLAYFUL, TR_SLEEPY, TR_FOODIE, TR_CUDDLY, TR_CURIOUS, TR_SASSY, TR_SHY, TR_BRAVE, TRAIT_COUNT };

struct Food {
  const char *name;
  int8_t fill, fun, health;
  uint8_t crumbs;
};
#define FOOD_COUNT 12
extern const Food FOODS[FOOD_COUNT];

#define BUILTIN_TRICKS 12
#define TRICK_SLOTS (BUILTIN_TRICKS + CUSTOM_TRICKS)

struct CustomTrick {
  char name[16];
  uint8_t n;
  uint8_t moves[TRICK_MOVES_MAX];
};

#define PET_MAGIC 0x5045
#define PET_VER   1
struct PetSave {
  uint16_t magic;
  uint8_t ver;
  uint8_t stage;
  char name[NAME_MAX + 1];
  uint8_t nameRandom;        // name was rolled at random (not typed in)
  uint8_t avatarCustom;      // look was picked in the app (never changes by itself)
  Avatar av;
  uint8_t trait[2];
  uint8_t favFood, hateFood;
  uint8_t knownFav;          // favourite food has been discovered
  float hunger, energy, fun, love, health;   // 0..100 (hunger = how full)
  uint32_t ageSec;           // seconds lived since hatching (while powered)
  uint32_t eggSec;
  uint32_t bornEpoch;        // wall clock at hatch, 0 if unknown
  uint8_t crumbs;
  uint8_t sick, asleep, lightsOff;
  uint8_t skill[TRICK_SLOTS];
  CustomTrick custom[CUSTOM_TRICKS];
  uint32_t fed, petted, shaken, tricksDone, games, bestCatch, sickCount, nights;
};

#define SET_MAGIC 0x5345
struct Settings {
  uint16_t magic;
  uint8_t contrast, flip, autoRotate, driver, bubbles, sens, sleepDim;
  uint16_t bedtime, waketime;   // minutes after midnight
  uint8_t calibrated;
  float calib[9];
  char appUrl[100];
};

struct DiaryEntry {
  uint32_t age;   // pet age in seconds when it happened
  uint8_t type, arg;
};
enum DiaryType : uint8_t {
  DI_HATCHED = 1, DI_STAGE, DI_LEARNED, DI_SICK, DI_CURED, DI_FAVFOOD, DI_RECORD, DI_RENAMED, DI_NIGHT,
  DI_NEWTRICK, DI_NEWLOOK
};
