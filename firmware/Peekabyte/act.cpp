#include "act.h"
#include "fx.h"

namespace act {

using face::Pose;

static uint8_t seq[20];
static uint8_t seqN = 0, idx = 0, curTag = TAG_NONE;
static int8_t curTrick = -1;
static float mt = 0;              // seconds into the current move
static bool started = false;      // current move's start hook has run
static SfxHook sfx = nullptr;

void setSfxHook(SfxHook h) { sfx = h; }

static void sound(const char *s) {
  if (sfx) sfx(s);
}

static float dur(uint8_t m) {
  switch (m) {
    case MV_HOP: return 0.5f;
    case MV_SPIN: return 0.8f;
    case MV_SQUISH: return 0.35f;
    case MV_STRETCH: return 0.45f;
    case MV_BLINK: return 0.3f;
    case MV_FLIP: return 0.85f;
    case MV_HIDE: return 1.4f;
    case MV_ROLL: return 0.9f;
    case MV_KO: return 1.3f;
    case MV_DIZZY: return 1.4f;
    case MV_HEARTS: case MV_STARS: case MV_NOTE: case MV_TIPHAT: case MV_CROSS: return 0.8f;
    default: return 0.6f;
  }
}

static float bell(float t) { return sinf(3.14159f * constrain(t, 0.0f, 1.0f)); }
static float ease(float t) {
  t = constrain(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

static void onStart(uint8_t m, const Pose &p) {
  float x = face::cx(), y = face::eyeY();
  switch (m) {
    case MV_HOP: case MV_BOUNCE: sound("boing"); break;
    case MV_SPIN: case MV_FLIP: case MV_DASH_L: case MV_DASH_R: sound("whoosh"); break;
    case MV_HEARTS: case MV_LOVE: sound("love"); break;
    case MV_SPARKLE: case MV_STARS: sound("sparkle"); break;
    case MV_NOTE: sound("note"); break;
    case MV_KO: sound("bonk"); break;
    case MV_DIZZY: sound("dizzy"); break;
    case MV_TIPHAT: sound("tada"); break;
    case MV_WINK: sound("wink"); break;
    case MV_JOY: sound("giggle"); break;
    case MV_NO: sound("no"); break;
    default: break;
  }
  if (m == MV_STARS) fx::burst(fx::FX_STAR, x, y, 6, 28);
  if (m == MV_SPARKLE) {
    fx::spawn(fx::FX_SPARKLE, face::eyeX(0) - 8, y - 10, 0, 0, 0.6f);
    fx::spawn(fx::FX_SPARKLE, face::eyeX(1) + 8, y - 6, 0, 0, 0.6f);
    fx::spawn(fx::FX_SPARKLE, x, y + 14, 0, 0, 0.6f);
  }
  (void)p;
}

// Applies move m at time t (seconds) to the pose.
static void apply(uint8_t m, float t, float d, Pose &p) {
  float u = t / d;   // 0..1
  switch (m) {
    case MV_HOP:
      if (u < 0.15f) { p.sy *= 1 - 0.18f * bell(u / 0.3f); p.sx *= 1 + 0.12f * bell(u / 0.3f); }
      else if (u < 0.85f) { p.dy -= 11 * bell((u - 0.15f) / 0.7f); p.sy *= 1.08f; p.sx *= 0.95f; }
      else { float k = bell((u - 0.85f) / 0.3f); p.sy *= 1 - 0.15f * k; p.sx *= 1 + 0.1f * k; }
      break;
    case MV_SPIN:
      p.sx *= cosf(6.2832f * ease(u));
      p.dy -= 3 * bell(u);
      break;
    case MV_SQUISH: p.sy *= 1 - 0.28f * bell(u); p.sx *= 1 + 0.22f * bell(u); break;
    case MV_STRETCH: p.sy *= 1 + 0.22f * bell(u); p.sx *= 1 - 0.14f * bell(u); break;
    case MV_LOOK_L: p.lookX = -bell(u) * 1.2f; p.dx -= 3 * bell(u); break;
    case MV_LOOK_R: p.lookX = bell(u) * 1.2f; p.dx += 3 * bell(u); break;
    case MV_LOOK_U: p.lookY = -bell(u); p.dy -= 2 * bell(u); break;
    case MV_LOOK_D: p.lookY = bell(u); p.dy += 2 * bell(u); break;
    case MV_BLINK: p.closed = u > 0.2f && u < 0.8f; break;
    case MV_WINK: p.expr = face::EX_WINK; p.blush = max(p.blush, bell(u)); break;
    case MV_WIGGLE: p.dx += sinf(u * 6 * 3.14159f) * 3.5f; break;
    case MV_FLIP:
      p.sy *= cosf(6.2832f * ease(u));
      p.dy -= 12 * bell(u);
      break;
    case MV_NO: p.dx += sinf(u * 4 * 3.14159f) * 4 * bell(u); p.lookX = -sinf(u * 4 * 3.14159f) * 0.6f; break;
    case MV_NOD: p.dy += sinf(u * 4 * 3.14159f) * 2.5f; p.lookY = sinf(u * 4 * 3.14159f) * 0.5f; break;
    case MV_HEARTS:
    case MV_LOVE:
      p.expr = face::EX_LOVE;
      if ((int)(t * 6) != (int)((t - 0.02f) * 6) && random(0, 2))
        fx::spawn(fx::FX_HEART, face::cx() + random(-24, 25), face::eyeY() - 8, random(-5, 6), -14);
      break;
    case MV_STARS: p.expr = face::EX_STARS; break;
    case MV_SPARKLE: p.expr = face::EX_HAPPY; break;
    case MV_NOTE:
      p.expr = face::EX_CONTENT;
      p.talk = 0.5f + 0.5f * sinf(t * 14);
      p.dy -= fabsf(sinf(u * 2 * 3.14159f)) * 2;
      if ((int)(t * 3) != (int)((t - 0.02f) * 3))
        fx::spawn(fx::FX_NOTE, face::cx() + random(10, 30), face::eyeY() - 12, random(3, 12), -12);
      break;
    case MV_TIPHAT: p.hatLift = 7 * bell(u); p.dy += 2 * bell(u); p.expr = face::EX_HAPPY; break;
    case MV_HIDE:
      if (u < 0.3f) p.dy += 60 * ease(u / 0.3f);
      else if (u < 0.7f) p.hidden = true;
      else {
        float k = (u - 0.7f) / 0.3f;
        p.dy += 60 * (1 - ease(k)) - 6 * bell(k);
        p.expr = face::EX_JOY;
        if (k < 0.1f && !fx::talking()) fx::say("Peekaboo!", 1.2f);
      }
      break;
    case MV_BOUNCE: p.dy -= fabsf(sinf(u * 2 * 3.14159f)) * 5; break;
    case MV_GROW: p.scale *= 1 + 0.25f * bell(u); break;
    case MV_SHRINK: p.scale *= 1 - 0.35f * bell(u); break;
    case MV_CROSS: p.cross = bell(u); break;
    case MV_ROLL: p.lookX = cosf(u * 1.5f * 6.2832f); p.lookY = sinf(u * 1.5f * 6.2832f); break;
    case MV_DASH_L: p.dx -= 20 * bell(u); p.lookX = 0.8f; break;
    case MV_DASH_R: p.dx += 20 * bell(u); p.lookX = -0.8f; break;
    case MV_JOY: p.expr = face::EX_JOY; p.dy -= 3 * bell(u); break;
    case MV_KO: p.expr = face::EX_KO; p.dy += 4 * ease(u * 3); p.sx *= 1.06f; p.sy *= 0.92f; break;
    case MV_DIZZY:
      p.expr = face::EX_DIZZY;
      p.dx += sinf(t * 9) * 2;
      if ((int)(t * 4) != (int)((t - 0.02f) * 4))
        fx::spawn(fx::FX_STAR, face::cx() + random(-26, 27), face::eyeY() - 16, random(-10, 11), -4, 0.8f);
      break;
    case MV_WOBBLE: {
      float w = sinf(u * 6 * 3.14159f) * (1 - u) * 0.14f;
      p.sx *= 1 + w;
      p.sy *= 1 - w;
      break;
    }
  }
}

void play(const uint8_t *moves, uint8_t n, uint8_t t, int8_t trick) {
  seqN = min<uint8_t>(n, sizeof seq);
  memcpy(seq, moves, seqN);
  idx = 0;
  mt = 0;
  started = false;
  curTag = t;
  curTrick = trick;
}

void stop() {
  seqN = 0;
  curTag = TAG_NONE;
  curTrick = -1;
}

bool busy() { return seqN > 0 && idx < seqN; }
uint8_t tag() { return busy() ? curTag : TAG_NONE; }
int8_t trick() { return busy() ? curTrick : -1; }

void update(float dt, Pose &p) {
  if (!busy()) return;
  uint8_t m = seq[idx] < MV_COUNT ? seq[idx] : MV_BLINK;
  if (!started) {
    started = true;
    onStart(m, p);
  }
  float d = dur(m);
  mt += dt;
  apply(m, min(mt, d), d, p);
  if (mt >= d) {
    idx++;
    mt = 0;
    started = false;
    if (idx >= seqN) {
      seqN = 0;
      curTag = TAG_NONE;
      curTrick = -1;
    }
  }
}

// ---- Tricks ---------------------------------------------------------------------------
static const uint8_t T_SPIN[] = {MV_SQUISH, MV_SPIN, MV_JOY};
static const uint8_t T_JUMP[] = {MV_SQUISH, MV_HOP, MV_HOP, MV_JOY};
static const uint8_t T_WINK[] = {MV_LOOK_R, MV_WINK, MV_SPARKLE};
static const uint8_t T_ROLL[] = {MV_ROLL, MV_ROLL, MV_WOBBLE};
static const uint8_t T_DANCE[] = {MV_NOTE, MV_DASH_L, MV_BOUNCE, MV_DASH_R, MV_BOUNCE, MV_SPIN, MV_JOY};
static const uint8_t T_DEAD[] = {MV_SQUISH, MV_KO, MV_KO, MV_BLINK, MV_JOY};
static const uint8_t T_PEEK[] = {MV_HIDE, MV_JOY};
static const uint8_t T_FLIP[] = {MV_SQUISH, MV_FLIP, MV_JOY};
static const uint8_t T_MOON[] = {MV_LOOK_R, MV_DASH_L, MV_DASH_L, MV_SPIN, MV_JOY};
static const uint8_t T_SING[] = {MV_NOTE, MV_NOTE, MV_NOTE, MV_JOY};
static const uint8_t T_BOW[] = {MV_TIPHAT, MV_NOD, MV_WINK};
static const uint8_t T_MAGIC[] = {MV_SPARKLE, MV_SHRINK, MV_STARS, MV_GROW, MV_SPARKLE};

struct Def {
  const uint8_t *m;
  uint8_t n;
};
static const Def BUILTIN[] = {
  {T_SPIN, sizeof T_SPIN}, {T_JUMP, sizeof T_JUMP}, {T_WINK, sizeof T_WINK}, {T_ROLL, sizeof T_ROLL},
  {T_DANCE, sizeof T_DANCE}, {T_DEAD, sizeof T_DEAD}, {T_PEEK, sizeof T_PEEK}, {T_FLIP, sizeof T_FLIP},
  {T_MOON, sizeof T_MOON}, {T_SING, sizeof T_SING}, {T_BOW, sizeof T_BOW}, {T_MAGIC, sizeof T_MAGIC},
};

const uint8_t *builtinTrick(uint8_t id, uint8_t &n) {
  if (id >= sizeof BUILTIN / sizeof BUILTIN[0]) { n = 0; return nullptr; }
  n = BUILTIN[id].n;
  return BUILTIN[id].m;
}

void fumble(uint8_t first) {
  static uint8_t f[4];
  switch (random(0, 3)) {
    case 0:   // confused
      f[0] = MV_LOOK_L; f[1] = MV_LOOK_R; f[2] = MV_NO;
      play(f, 3, TAG_FUMBLE);
      fx::spawn(fx::FX_QUESTION, face::cx() + 22, face::eyeY() - 14, 0, 0, 1.8f);
      break;
    case 1:   // starts well, then flops
      f[0] = first; f[1] = MV_SQUISH; f[2] = MV_DIZZY;
      play(f, 3, TAG_FUMBLE);
      break;
    default:  // does something else entirely
      f[0] = random(0, 2) ? MV_WIGGLE : MV_CROSS; f[1] = MV_BLINK; f[2] = MV_WOBBLE;
      play(f, 3, TAG_FUMBLE);
      fx::spawn(fx::FX_QUESTION, face::cx() - 22, face::eyeY() - 14, 0, 0, 1.4f);
      break;
  }
}

}  // namespace act
