#include "face.h"
#include "gfx.h"

namespace face {

// ---- Layout of the current frame --------------------------------------------
// Everything is drawn in "local" coordinates around the face centre and mapped
// to the screen with X()/Y(), so squash, stretch and turning around come free.
static float FX = 64, FY = 32, SX = 1, SY = 1;
static float EXo = 18, EW = 19, EH = 25, K = 1;
static float belowY = 15, mouthLocal = 17, neckLocal = 24, hatBase = -15;
static uint8_t hatNow = 0;
static float clk = 0;

static inline float X(float lx) { return FX + lx * SX; }
static inline float Y(float ly) { return FY + ly * SY; }
static inline float W(float lw) { return lw * fabsf(SX); }
static inline float H(float lh) { return lh * fabsf(SY); }
static inline int R(float v) { return (int)lroundf(v); }

static void lEllipse(float x, float y, float rx, float ry, uint8_t c = 1) {
  gfx::fillEllipse(X(x), Y(y), W(rx), H(ry), c);
}
static void lRing(float x, float y, float rx, float ry, float t, uint8_t c = 1) {
  gfx::ellipse(X(x), Y(y), W(rx), H(ry), t, c);
}
static void lLine(float x0, float y0, float x1, float y1, float w, uint8_t c = 1) {
  gfx::thickLine(X(x0), Y(y0), X(x1), Y(y1), w, c);
}
static void lBez(float x0, float y0, float qx, float qy, float x1, float y1, float w, uint8_t c = 1) {
  gfx::bezier(X(x0), Y(y0), X(qx), Y(qy), X(x1), Y(y1), w, c);
}
static void lTri(float x0, float y0, float x1, float y1, float x2, float y2, uint8_t c = 1) {
  gfx::fillTri(R(X(x0)), R(Y(y0)), R(X(x1)), R(Y(y1)), R(X(x2)), R(Y(y2)), c);
}
static void lBox(float x, float y, float w, float h, float &xa, float &ya, float &xb, float &yb) {
  xa = X(x); xb = X(x + w);
  if (xa > xb) std::swap(xa, xb);
  ya = Y(y); yb = Y(y + h);
  if (ya > yb) std::swap(ya, yb);
}
static void lRect(float x, float y, float w, float h, uint8_t c = 1) {
  float xa, ya, xb, yb;
  lBox(x, y, w, h, xa, ya, xb, yb);
  gfx::fillRect(R(xa), R(ya), R(xb) - R(xa), R(yb) - R(ya), c);
}
static void lRRect(float x, float y, float w, float h, float r, uint8_t c = 1) {
  float xa, ya, xb, yb;
  lBox(x, y, w, h, xa, ya, xb, yb);
  gfx::fillRRect(R(xa), R(ya), R(xb) - R(xa), R(yb) - R(ya), R(r), c);
}
static void lFrame(float x, float y, float w, float h, float r, int thick) {
  float xa, ya, xb, yb;
  lBox(x, y, w, h, xa, ya, xb, yb);
  for (int i = 0; i < thick; i++)
    gfx::rrect(R(xa) + i, R(ya) + i, R(xb) - R(xa) - 2 * i, R(yb) - R(ya) - 2 * i, max(0, R(r) - i), 1);
}
static void lDither(float x, float y, float rx, float ry, uint8_t level) {
  gfx::ditherEllipse(X(x), Y(y), W(rx), H(ry), level);
}
static void lPx(float x, float y, uint8_t c = 1) { gfx::px(R(X(x)), R(Y(y)), c); }

// Stroke along a quadratic curve whose width changes from w0 to w1.
static void lTaper(float x0, float y0, float qx, float qy, float x1, float y1, float w0, float w1, uint8_t c = 1) {
  float len = W(fabsf(qx - x0) + fabsf(x1 - qx)) + H(fabsf(qy - y0) + fabsf(y1 - qy));
  int steps = max(8, (int)(len / 0.9f));
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps, u = 1 - t;
    float x = u * u * x0 + 2 * u * t * qx + t * t * x1;
    float y = u * u * y0 + 2 * u * t * qy + t * t * y1;
    float w = (w0 + (w1 - w0) * t) / 2;
    gfx::fillEllipse(X(x), Y(y), W(w), H(w), c);
  }
}

// Upper half of an ellipse (domes of hats and helmets).
static void halfDome(float x, float y, float rx, float ry, uint8_t c = 1) {
  float yy = Y(y);
  if (SY >= 0) gfx::setClip(0, 0, SCREEN_W, R(yy) + 1);
  else gfx::setClip(0, R(yy), SCREEN_W, SCREEN_H);
  lEllipse(x, y, rx, ry, c);
  gfx::resetClip();
}

// ---- Shapes in screen coordinates -------------------------------------------
static void heartShape(float cx, float cy, float s, uint8_t c = 1) {
  float r = s * 0.29f;
  gfx::fillEllipse(cx - r * 0.95f, cy - r * 0.35f, r, r, c);
  gfx::fillEllipse(cx + r * 0.95f, cy - r * 0.35f, r, r, c);
  gfx::fillTri(R(cx - r * 1.93f), R(cy - r * 0.05f), R(cx + r * 1.93f), R(cy - r * 0.05f), R(cx), R(cy + r * 2.15f), c);
}

static void starShape(float cx, float cy, float ro, float rot, uint8_t c = 1) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = rot - 1.5708f + i * 0.62832f;
    float rr = (i & 1) ? ro * 0.45f : ro;
    px[i] = cx + cosf(a) * rr;
    py[i] = cy + sinf(a) * rr;
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    gfx::fillTri(R(cx), R(cy), R(px[i]), R(py[i]), R(px[j]), R(py[j]), c);
  }
}

static void spiralShape(float cx, float cy, float r, float rot, float w) {
  float lx = cx, ly = cy;
  for (int i = 1; i <= 30; i++) {
    float th = i * 0.4f;
    float rr = r * th / 12.0f;
    float x = cx + cosf(th + rot) * rr, y = cy + sinf(th + rot) * rr;
    gfx::thickLine(lx, ly, x, y, w);
    lx = x;
    ly = y;
  }
}

static void tinyStar(float x, float y, uint8_t c) {   // 5-pixel plus in local coords
  lPx(x, y, c); lPx(x - 1, y, c); lPx(x + 1, y, c); lPx(x, y - 1, c); lPx(x, y + 1, c);
}

// ---- Eyelids, blinking, brows ----------------------------------------------
// top = upper lid (0 open .. 1 shut), slant = lid tilt (+ sad, - angry),
// bot = smiling lower lid (curved), flat = squinting lower lid (straight).
struct Lid {
  float top, slant, bot, flat, scale, pup;
};
static Lid cur[2] = {{0, 0, 0, 0, 1, 1}, {0, 0, 0, 0, 1, 1}};
static float browRaise = 0, browTilt = 0;
static float blinkT = -1, nextBlink = 2.5f;
static bool doubleBlink = false;
static float tremble = 0;
static float antX = 0, antV = 0, lastDx = 0;

enum Glyph : uint8_t { G_NONE, G_HAPPY, G_SLEEP, G_HEART, G_STAR, G_SPIRAL, G_X, G_CHEVRON };

static uint8_t glyphFor(uint8_t ex, int i) {
  switch (ex) {
    case EX_JOY: return G_HAPPY;
    case EX_ASLEEP: return G_SLEEP;
    case EX_LOVE: return G_HEART;
    case EX_STARS: return G_STAR;
    case EX_DIZZY: return G_SPIRAL;
    case EX_KO: return G_X;
    case EX_YUCK: return G_CHEVRON;
    case EX_WINK: return i == 0 ? G_HAPPY : G_NONE;
    default: return G_NONE;
  }
}

static void target(uint8_t ex, int i, Lid &l) {
  l = {0, 0, 0, 0, 1, 1};
  switch (ex) {
    case EX_CONTENT: l.top = 0.1f; l.flat = 0.06f; break;
    case EX_HAPPY: l.top = 0.02f; l.bot = 0.46f; break;
    case EX_SAD:
    case EX_CRY: l.top = 0.22f; l.slant = 0.85f; break;
    case EX_ANGRY: l.top = 0.26f; l.slant = -0.9f; l.flat = 0.08f; break;
    case EX_SURPRISED: l.scale = 1.14f; l.pup = 0.55f; break;
    case EX_SCARED: l.scale = 1.08f; l.pup = 0.42f; l.slant = 0.35f; break;
    case EX_SLEEPY: l.top = 0.55f; l.slant = 0.12f; l.flat = 0.05f; break;
    case EX_SICK: l.top = 0.4f; l.slant = 0.5f; l.flat = 0.1f; l.pup = 0.8f; break;
    case EX_SQUINT: l.top = 0.36f; l.flat = 0.36f; break;
    case EX_BORED: l.top = 0.48f; break;
    case EX_THINK: l.top = 0.1f; break;
    case EX_CHOMP: l.top = 0.16f; l.bot = 0.3f + 0.26f * (0.5f + 0.5f * sinf(clk * 18)); break;
    case EX_SMUG: l.top = 0.34f; l.flat = 0.12f; l.slant = -0.12f; break;
    case EX_WINK:
      if (i == 1) { l.top = 0.02f; l.bot = 0.44f; }
      break;
    default: break;
  }
}

void update(float dt, const Pose &p) {
  clk += dt;
  float k = min(1.0f, dt * 14);
  for (int i = 0; i < 2; i++) {
    Lid t;
    target(p.expr, i, t);
    if (p.stage == ST_BABY) t.pup *= 1.12f;
    cur[i].top += (t.top - cur[i].top) * k;
    cur[i].slant += (t.slant - cur[i].slant) * k;
    cur[i].bot += (t.bot - cur[i].bot) * k;
    cur[i].flat += (t.flat - cur[i].flat) * k;
    cur[i].scale += (t.scale - cur[i].scale) * k;
    cur[i].pup += (t.pup - cur[i].pup) * k;
  }
  float br = 0, bt = 0;
  switch (p.expr) {
    case EX_HAPPY: case EX_JOY: case EX_CONTENT: case EX_LOVE: br = 1.5f; break;
    case EX_SAD: case EX_CRY: case EX_SICK: br = 0.5f; bt = 3.5f; break;
    case EX_ANGRY: case EX_YUCK: br = -1.5f; bt = -3.5f; break;
    case EX_SURPRISED: case EX_SCARED: case EX_STARS: br = 4; bt = 1; break;
    case EX_SLEEPY: case EX_BORED: case EX_ASLEEP: br = -1.5f; break;
    case EX_SMUG: br = 0.5f; bt = -1.5f; break;
    case EX_THINK: br = 0.5f; bt = -1; break;
    case EX_DIZZY: br = 2; bt = 2; break;
    default: break;
  }
  browRaise += (br - browRaise) * k;
  browTilt += (bt - browTilt) * k;

  bool canBlink = glyphFor(p.expr, 0) == G_NONE && glyphFor(p.expr, 1) == G_NONE && !p.closed;
  if (blinkT >= 0) {
    blinkT += dt;
    if (blinkT > 0.2f) {
      blinkT = -1;
      if (doubleBlink) { doubleBlink = false; nextBlink = 0.12f; }
    }
  } else if (canBlink) {
    nextBlink -= dt;
    if (nextBlink <= 0) {
      blinkT = 0;
      nextBlink = 1.8f + random(0, 4000) / 1000.0f;
      doubleBlink = random(0, 100) < 18;
    }
  }
  tremble = p.expr == EX_SCARED ? random(-10, 11) / 10.0f : 0;

  // antenna tip on a spring, pushed around by head movement and shakes
  float push = (p.dx - lastDx) / max(dt, 0.001f);
  lastDx = p.dx;
  antV += (-antX * 90 - antV * 6 - p.swayX * 900 - push * 2) * dt;
  antX += antV * dt;
  antX = constrain(antX, -9.0f, 9.0f);
}

static float blinkAmount() {
  if (blinkT < 0) return 0;
  if (blinkT < 0.06f) return blinkT / 0.06f;
  if (blinkT < 0.09f) return 1;
  return max(0.0f, 1 - (blinkT - 0.09f) / 0.11f);
}

// ---- Layout -------------------------------------------------------------------
static const float EYE_W[5] = {17, 19, 21, 24, 26};
static const float EYE_H[5] = {21, 25, 28, 32, 35};
static const float GAPS[5] = {6, 10, 14, 18, 23};
static const uint8_t HAT_H[20] = {0, 15, 18, 14, 14, 10, 12, 19, 18, 5, 6, 12, 9, 9, 15, 18, 11, 12, 12, 13};

static void layout(const Avatar &av, const Pose &p) {
  float w = EYE_W[min<uint8_t>(av.size, 4)], h = EYE_H[min<uint8_t>(av.size, 4)];
  switch (av.eyes) {
    case EYE_ROUND: w = h = (w + h) * 0.46f; break;
    case EYE_ROBO: w *= 1.08f; h *= 0.92f; break;
    case EYE_WIDE: w *= 1.35f; h *= 0.68f; break;
    case EYE_DROOPY: w *= 1.1f; break;
    case EYE_ANIME: w *= 1.12f; h *= 1.08f; break;
    case EYE_CAT: w *= 1.35f; h *= 0.74f; break;
    default: break;
  }
  float st = p.stage == ST_BABY ? 1.12f : p.stage == ST_TEEN ? 0.97f : p.stage == ST_ADULT ? 0.95f : 1.0f;
  w *= st;
  h *= st;
  float gap = GAPS[min<uint8_t>(av.gap, 4)];
  bool lip = av.face && av.face != FA_BEARD && av.face != FA_GOATEE;
  float top = av.hat ? min<float>(HAT_H[min<uint8_t>(av.hat, 19)], 16) + 1 : 3;
  if (av.brows) top += av.hat ? 3 : 4;
  float bottom = 2 + (lip ? 7 : 0) + (av.mouth ? 7 : 0) + (av.neck ? 7 : 0) + (av.face == FA_GOATEE ? 5 : 0);
  if (av.face == FA_BEARD) bottom += 4;
  float avail = SCREEN_H - top - bottom;
  if (h > avail - 1) {
    float k = (avail - 1) / h;
    w *= k; h *= k; gap *= k;
  }
  float span = 2 * w + gap;
  if (span > 110) {
    float k = 110 / span;
    w *= k; h *= k; gap *= k;
  }
  EW = w * p.scale;
  EH = h * p.scale;
  EXo = (w + gap) / 2 * p.scale;
  K = EW / 19.0f;
  SX = p.sx;
  SY = p.sy;
  FX = SCREEN_W / 2 + p.dx;
  FY = top + avail / 2 + p.dy;
  belowY = EH / 2 + 3;
  mouthLocal = belowY + (lip ? 7.5f : 2.5f);
  neckLocal = (av.mouth ? mouthLocal + 7.5f : (lip ? belowY + 8 : belowY + 3)) + (av.face == FA_GOATEE ? 5 : 0) + 1.5f;
  hatBase = -EH / 2 - 2 - (av.brows ? 5 : 0) - p.hatLift;
  hatNow = av.hat;
}

// ---- Eyes -----------------------------------------------------------------------
static void eyeShape(uint8_t shape, float cx, float cy, float w, float h, int vside, uint8_t c) {
  if (w < 1 || h < 1) return;
  switch (shape) {
    case EYE_ROBO:
      gfx::fillRRect(R(cx - w / 2), R(cy - h / 2), R(w), R(h), (int)(min(w, h) * 0.28f), c);
      break;
    case EYE_CAT: {   // almond, outer corner lifted
      float rx = w / 2, ry = h / 2;
      for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
        float u = (x + 0.5f - cx) / rx;
        if (fabsf(u) >= 1) continue;
        float hh = ry * powf(1 - u * u, 0.7f);
        float lift = -u * vside * ry * 0.22f;
        gfx::vline(x, R(cy - hh + lift), R(2 * hh), c);
      }
      break;
    }
    case EYE_PIXEL: {   // chunky 3px blocks on a grid
      const int s = 3;
      float rx = w / 2, ry = h / 2;
      int gx0 = (int)floorf((cx - rx) / s) * s, gy0 = (int)floorf((cy - ry) / s) * s;
      for (int y = gy0; y < cy + ry; y += s)
        for (int x = gx0; x < cx + rx; x += s) {
          float u = (x + s / 2.0f - cx) / rx, v = (y + s / 2.0f - cy) / ry;
          if (u * u + v * v <= 1.0f) gfx::fillRect(x, y, s, s, c);
        }
      break;
    }
    default:
      gfx::fillEllipse(cx, cy, w / 2, h / 2, c);
      break;
  }
}

static void drawPupil(const Avatar &av, const Pose &p, int i, float cx, float cy, float w, float h, float pscale) {
  uint8_t st = av.pupil;
  if (st == PUP_NONE) return;
  float m = min(w, h), pr;
  switch (st) {
    case PUP_BIG: pr = 0.36f; break;
    case PUP_TINY: pr = 0.14f; break;
    case PUP_RING: pr = 0.3f; break;
    case PUP_SPARKLE: pr = 0.34f; break;
    case PUP_SLIT: pr = 0.2f; break;
    default: pr = 0.26f; break;
  }
  pr *= m * pscale;
  if (p.dark) pr = max(1.5f, pr * 0.55f);
  float side = i == 0 ? -1 : 1;
  float gx = p.lookX + p.swayX * 1.4f - p.cross * side * 0.95f;
  float gy = p.lookY + p.swayY * 1.4f;
  float slitH = min(pr * 2.2f, h / 2 - 1.5f);
  float rx = max(0.0f, w / 2 - pr - 1.2f);
  float ry = max(0.0f, h / 2 - (st == PUP_SLIT ? slitH : pr) - 1.2f);
  float ox = constrain(gx, -1.0f, 1.0f) * rx, oy = constrain(gy, -1.0f, 1.0f) * ry;
  if (rx > 0.1f && ry > 0.1f) {
    float q = ox * ox / (rx * rx) + oy * oy / (ry * ry);
    if (q > 1) {
      q = sqrtf(q);
      ox /= q;
      oy /= q;
    }
  }
  if (SX < 0) ox = -ox;
  float px = cx + ox, py = cy + oy + tremble * 0.5f;
  uint8_t ink = p.dark ? 1 : 0, shine = p.dark ? 0 : 1;
  switch (st) {
    case PUP_RING:
      gfx::ellipse(px, py, pr, pr, max(1.5f, pr * 0.38f), ink);
      break;
    case PUP_SLIT:
      gfx::fillEllipse(px, py, max(1.2f, pr * 0.42f), slitH, ink);
      break;
    default: {
      gfx::fillEllipse(px, py, pr, pr, ink);
      if (p.dark || st == PUP_TINY) break;
      float hr = max(0.8f, pr * 0.3f);
      float hx = px - pr * 0.38f, hy = py - pr * 0.4f;
      gfx::fillEllipse(hx, hy, hr, hr, shine);
      if (st == PUP_BIG || st == PUP_SPARKLE || av.eyes == EYE_ANIME)
        gfx::fillEllipse(px + pr * 0.42f, py + pr * 0.38f, max(0.6f, pr * 0.15f), max(0.6f, pr * 0.15f), shine);
      if (st == PUP_SPARKLE) {
        float a = pr * 0.62f;
        gfx::line(R(hx - a), R(hy), R(hx + a), R(hy), shine);
        gfx::line(R(hx), R(hy - a), R(hx), R(hy + a), shine);
      }
      break;
    }
  }
}

static void lidMask(float cx, float cy, float w, float h, int vside, float top, float slant, float bot, float flat) {
  if (top < 0.01f && fabsf(slant) < 0.01f && bot < 0.01f && flat < 0.01f) return;
  int x0 = (int)floorf(cx - w / 2 - 1), x1 = (int)ceilf(cx + w / 2 + 1);
  int ya = (int)floorf(cy - h / 2 - 2), yb = (int)ceilf(cy + h / 2 + 2);
  int yf = R(cy + h / 2 - flat * h);
  for (int x = x0; x <= x1; x++) {
    float u = (x + 0.5f - cx) / (w / 2);
    float outer = constrain(u, -1.0f, 1.0f) * vside;
    int yt = R(cy - h / 2 + top * h + slant * (outer + 0.15f) * h * 0.36f);
    for (int y = ya; y < yt && y <= yb; y++) gfx::px(x, y, 0);
    int ycut = yb + 1;
    if (bot > 0.01f) {   // smiling: the lower lid bulges up in the middle
      float kk = 1 - u * u;
      if (kk < 0) kk = 0;
      ycut = R(cy + h / 2 + 1 - bot * h * 1.25f * sqrtf(kk));
    }
    if (flat > 0.01f) ycut = min(ycut, yf);
    for (int y = max(ya, ycut); y <= yb; y++) gfx::px(x, y, 0);
  }
}

static void closedLine(float cx, float cy, float w, float h) {
  float th = max(2.0f, min(w, h) * 0.11f);
  gfx::bezier(cx - w * 0.46f, cy + h * 0.06f, cx, cy + h * 0.22f, cx + w * 0.46f, cy + h * 0.06f, th);
}

static void glyph(uint8_t g, float cx, float cy, float w, float h, int vside) {
  float th = max(2.0f, min(w, h) * 0.13f);
  switch (g) {
    case G_HAPPY:
      gfx::bezier(cx - w * 0.45f, cy + h * 0.12f, cx, cy - h * 0.42f, cx + w * 0.45f, cy + h * 0.12f, th);
      break;
    case G_SLEEP:
      gfx::bezier(cx - w * 0.45f, cy + h * 0.02f, cx, cy + h * 0.26f, cx + w * 0.45f, cy + h * 0.02f, th);
      break;
    case G_HEART:
      heartShape(cx, cy - h * 0.05f, min(w, h) * (1.0f + 0.1f * sinf(clk * 9)));
      break;
    case G_STAR:
      starShape(cx, cy, min(w, h) * 0.6f, clk * 1.5f * vside);
      break;
    case G_SPIRAL:
      spiralShape(cx, cy, min(w, h) * 0.5f, clk * 7 * vside, max(1.2f, th * 0.6f));
      break;
    case G_X:
      gfx::thickLine(cx - w * 0.33f, cy - w * 0.33f, cx + w * 0.33f, cy + w * 0.33f, th);
      gfx::thickLine(cx - w * 0.33f, cy + w * 0.33f, cx + w * 0.33f, cy - w * 0.33f, th);
      break;
    case G_CHEVRON: {   // > <, pointing at each other
      float d = -vside;
      gfx::thickLine(cx - d * w * 0.34f, cy - h * 0.28f, cx + d * w * 0.3f, cy, th);
      gfx::thickLine(cx + d * w * 0.3f, cy, cx - d * w * 0.34f, cy + h * 0.28f, th);
      break;
    }
    default: break;
  }
}

static void lashes(uint8_t style, float cx, float cy, float w, float h, int vside, float top) {
  int n = style == LA_FULL ? 5 : 3;
  for (int k = 0; k < n; k++) {
    float a = style == LA_FULL ? 0.4f + k * 0.58f : 0.3f + k * 0.36f;
    float ca = cosf(a) * (style == LA_FULL ? 1.0f : (float)vside), sa = sinf(a);
    float bx = cx + ca * w / 2, by = cy - sa * h / 2 + top * h * 0.9f;
    gfx::thickLine(bx, by, bx + ca * 3.5f, by - sa * 3.2f - 0.8f, 1);
  }
}

static void bags(float cx, float cy, float w, float h) {
  for (int x = R(cx - w * 0.35f); x <= R(cx + w * 0.35f); x += 2) {
    float u = (x - cx) / (w * 0.4f);
    gfx::px(x, R(cy + h / 2 + 2.5f - (1 - u * u) * 1.5f), 1);
  }
}

static void drawEye(const Avatar &av, const Pose &p, int i) {
  int side = i == 0 ? -1 : 1;
  int vside = SX < 0 ? -side : side;
  const Lid &L = cur[i];
  float par = av.pupil == PUP_NONE ? 4.5f : 2.0f;
  float lx = side * EXo + p.lookX * par + p.swayX * 3 + tremble * 0.6f;
  float ly = p.lookY * par * 0.6f + p.swayY * 2;
  float w = EW * L.scale, h = EH * L.scale;
  float cx = X(lx), cy = Y(ly), ww = W(w), hh = H(h);
  uint8_t g = glyphFor(p.expr, i);
  if (g != G_NONE) {
    glyph(g, cx, cy, ww, hh, vside);
    return;
  }
  float top = max(L.top, p.closed ? 1.0f : blinkAmount());
  if (av.eyes == EYE_DROOPY) top = max(top, 0.3f);
  if (top > 0.9f) {
    closedLine(cx, cy, ww, hh);
    return;
  }
  eyeShape(av.eyes, cx, cy, ww, hh, vside, 1);
  if (p.dark) eyeShape(av.eyes, cx, cy, ww - 4, hh - 4, vside, 0);
  drawPupil(av, p, i, cx, cy, ww, hh, L.pup);
  lidMask(cx, cy, ww, hh, vside, top, L.slant, L.bot, L.flat);
  if (av.lashes && top < 0.45f && !p.dark) lashes(av.lashes, cx, cy, ww, hh, vside, top);
  if (p.expr == EX_SICK) bags(cx, cy, ww, hh);
}

// ---- Brows, cheeks, face, mouth, neck -----------------------------------------
static void drawBrows(const Avatar &av, const Pose &p) {
  if (!av.brows || p.dark) return;
  float lxo = p.lookX * 2;
  float yo[2], yi[2], xo[2], xi[2];
  for (int i = 0; i < 2; i++) {
    int side = i == 0 ? -1 : 1;
    float raise = browRaise + (p.expr == EX_THINK && i == 1 ? 3 : 0);
    float lid = min(cur[i].top, 0.6f) * EH * 0.45f;   // brows ride down with heavy lids
    float base = -EH / 2 - 4 - raise - (cur[i].scale - 1) * EH * 0.5f + lid;
    xi[i] = side * (EXo - EW * 0.42f) + lxo;
    xo[i] = side * (EXo + EW * 0.46f) + lxo;
    yi[i] = base - browTilt;
    yo[i] = base + browTilt * 0.35f;
    float mx = (xi[i] + xo[i]) / 2, my = min(yi[i], yo[i]);
    switch (av.brows) {
      case BR_THIN: lBez(xi[i], yi[i], mx, my - 2, xo[i], yo[i] + 1, 1.2f); break;
      case BR_BOLD: lTaper(xi[i], yi[i], mx, my - 1.5f, xo[i], yo[i] + 1, 3.6f, 2.2f); break;
      case BR_FLUFFY:
        for (int k = 0; k <= 6; k++) {
          float t = k / 6.0f, u = 1 - t;
          float x = u * u * xi[i] + 2 * u * t * mx + t * t * xo[i];
          float y = u * u * yi[i] + 2 * u * t * (my - 1.5f) + t * t * (yo[i] + 1) + ((k & 1) ? -0.8f : 0.8f);
          lEllipse(x, y, 1.5f, 1.5f);
        }
        break;
      default: break;
    }
  }
  if (av.brows == BR_UNI) lTaper(xo[0], yo[0] + 1, 0, (yi[0] + yi[1]) / 2 + 2.5f, xo[1], yo[1] + 1, 2.8f, 2.8f);
}

static void drawCheeks(const Avatar &av, const Pose &p) {
  if (p.dark) return;
  float blush = p.blush;
  bool blushy = av.cheeks == CH_BLUSH || av.cheeks == CH_BOTH;
  if (blushy) blush = max(blush, 0.55f);
  float y = EH / 2 + 1.5f;
  for (int s = -1; s <= 1; s += 2) {
    float x = s * (EXo + EW * 0.12f);
    if (blush > 0.05f) lDither(x, y, EW * 0.34f, 2.6f, (uint8_t)(3 + blush * 6));
    if (av.cheeks == CH_FRECKLES || av.cheeks == CH_BOTH) {
      uint8_t c = av.cheeks == CH_BOTH ? 0 : 1;
      lPx(x - 3, y - 0.5f, c); lPx(x, y + 0.5f, c); lPx(x + 3, y - 0.5f, c); lPx(x - 1.5f, y + 2, c);
      lPx(x + 1.5f, y + 2, c);
    }
    if (av.cheeks == CH_WHISKERS)
      for (int k = -1; k <= 1; k++)
        lLine(s * (EXo + EW * 0.35f), y + k * 2.5f, s * (EXo + EW * 0.35f + 11), y - 1 + k * 4.0f, 1);
  }
}

static void drawMouth(const Avatar &av, const Pose &p) {
  if (!av.mouth || p.dark) return;
  float y = mouthLocal, k = K;
  if (p.talk > 0.06f) {   // open and close with the voice
    float rx = (3.2f + p.talk * 1.5f) * k, ry = (1.2f + p.talk * 3.8f) * k;
    lEllipse(0, y + ry * 0.4f, rx, ry);
    lEllipse(0, y + ry * 0.4f, max(0.5f, rx - 1.3f), max(0.4f, ry - 1.3f), 0);
    return;
  }
  uint8_t e = p.expr;
  bool sad = e == EX_SAD || e == EX_CRY || e == EX_SICK;
  bool oh = e == EX_SURPRISED || e == EX_SCARED;
  bool wavy = e == EX_ANGRY || e == EX_YUCK || e == EX_DIZZY;
  bool big = e == EX_JOY || e == EX_LOVE || e == EX_STARS || e == EX_HAPPY;
  if (oh) { lRing(0, y + 2 * k, 2.6f * k, 2.8f * k, 1.3f); return; }
  if (sad) { lBez(-4.5f * k, y + 3 * k, 0, y - 0.8f * k, 4.5f * k, y + 3 * k, 1.6f); return; }
  if (wavy) {
    for (int i = 0; i < 4; i++)
      lLine((-5 + i * 2.5f) * k, y + ((i & 1) ? 2.5f : 0.5f) * k, (-2.5f + i * 2.5f) * k, y + ((i & 1) ? 0.5f : 2.5f) * k, 1.3f);
    return;
  }
  switch (av.mouth) {
    case MO_SMILE: lBez(-5 * k, y, 0, y + (big ? 5 : 3.2f) * k, 5 * k, y, 1.7f); break;
    case MO_GRIN: {   // a D-shaped toothy grin
      float ry = (big ? 5.5f : 4.5f) * k;
      lEllipse(0, y - 0.2f, 6.5f * k, ry);
      lRect(-7.5f * k, y - ry - 1, 15 * k, ry + 1, 0);
      lLine(-4.5f * k, y + 1.6f * k, 4.5f * k, y + 1.6f * k, 1, 0);
      lLine(0, y, 0, y + 1.6f * k, 1, 0);
      break;
    }
    case MO_CAT:
      lBez(-5 * k, y, -2.5f * k, y + 3.5f * k, 0, y + 0.5f * k, 1.3f);
      lBez(0, y + 0.5f * k, 2.5f * k, y + 3.5f * k, 5 * k, y, 1.3f);
      break;
    case MO_FANGS:
      lBez(-5 * k, y, 0, y + 3 * k, 5 * k, y, 1.6f);
      for (int s = -1; s <= 1; s += 2) lTri(s * 3.2f * k, y + 1.6f * k, s * 1.2f * k, y + 2.2f * k, s * 2.2f * k, y + 5 * k);
      break;
    case MO_TONGUE:
      lBez(-5 * k, y, 0, y + 3 * k, 5 * k, y, 1.6f);
      lEllipse(1.5f * k, y + 3.8f * k, 2.4f * k, 2.4f * k);
      lLine(1.5f * k, y + 2.8f * k, 1.5f * k, y + 5 * k, 1, 0);
      break;
    default: break;
  }
}

static void drawFaceAcc(const Avatar &av, const Pose &p) {
  if (!av.face || p.dark) return;
  float y = belowY + 1, k = K;
  switch (av.face) {
    case FA_MOUSTACHE:
      for (int s = -1; s <= 1; s += 2) lTaper(s * 1.0f * k, y + 0.5f, s * 6 * k, y - 2.5f * k, s * 12 * k, y + 2.5f * k, 4.4f * k, 1.4f);
      break;
    case FA_HANDLEBAR:
      for (int s = -1; s <= 1; s += 2) {
        lTaper(s * 1 * k, y, s * 6 * k, y - 1 * k, s * 11 * k, y + 0.5f * k, 3.2f * k, 1.8f);
        lBez(s * 11 * k, y + 0.5f * k, s * 15 * k, y + 0.5f * k, s * 13.5f * k, y - 3.5f * k, 1.4f);
        lBez(s * 13.5f * k, y - 3.5f * k, s * 12 * k, y - 5 * k, s * 11.2f * k, y - 3 * k, 1.2f);
      }
      break;
    case FA_WALRUS:
      lEllipse(-5.5f * k, y + 2 * k, 7 * k, 4.2f * k);
      lEllipse(5.5f * k, y + 2 * k, 7 * k, 4.2f * k);
      for (float x = -11; x <= 11; x += 2.2f) lLine(x * k, y + 3 * k, x * k * 1.05f, y + 6.5f * k, 1, 0);
      break;
    case FA_PENCIL:
      lLine(-10 * k, y + 1, -1.5f * k, y, 1);
      lLine(1.5f * k, y, 10 * k, y + 1, 1);
      break;
    case FA_CURLY:
      for (int s = -1; s <= 1; s += 2) {
        lTaper(s * 1 * k, y, s * 5 * k, y - 1 * k, s * 8 * k, y + 1 * k, 3 * k, 2);
        lBez(s * 8 * k, y + 1 * k, s * 12 * k, y + 1.5f * k, s * 11 * k, y - 2.5f * k, 1.4f);
        lBez(s * 11 * k, y - 2.5f * k, s * 9.2f * k, y - 3.2f * k, s * 9.5f * k, y - 1 * k, 1.2f);
      }
      break;
    case FA_BEARD:
      lDither(0, y + 5 * k, EXo + EW * 0.4f, 7.5f * k, 7);
      break;
    case FA_GOATEE: {
      float gy = mouthLocal + (av.mouth ? 4 : 0);
      lTri(-3 * k, gy + 1 * k, 3 * k, gy + 1 * k, 0, gy + 7 * k);
      break;
    }
    default: break;
  }
}

static void drawNeck(const Avatar &av, const Pose &p) {
  if (!av.neck || p.dark) return;
  float y = neckLocal, k = K;
  switch (av.neck) {
    case NK_BOWTIE:
      lTri(0, y, -8 * k, y - 3.5f * k, -8 * k, y + 3.5f * k);
      lTri(0, y, 8 * k, y - 3.5f * k, 8 * k, y + 3.5f * k);
      lLine(-5.5f * k, y - 1.3f * k, -5.5f * k, y + 1.3f * k, 1, 0);
      lLine(5.5f * k, y - 1.3f * k, 5.5f * k, y + 1.3f * k, 1, 0);
      lEllipse(0, y, 2.2f * k, 2.2f * k);
      break;
    case NK_SCARF: {
      float half = EXo + EW * 0.5f;
      lRRect(-half, y - 2.5f * k, 2 * half, 5 * k, 2);
      for (float x = -half + 2; x < half - 1; x += 4) lLine(x, y - 2.5f * k, x + 2, y + 2.5f * k, 1, 0);
      lRRect(half * 0.45f, y, 5 * k, 9 * k, 1.5f);
      lLine(half * 0.45f + 1, y + 7.5f * k, half * 0.45f + 4 * k, y + 7.5f * k, 1, 0);
      break;
    }
    case NK_COLLAR:
      lBez(-EXo, y - 2, 0, y + 1.5f, EXo, y - 2, 2);
      lEllipse(0, y + 3 * k, 2.8f * k, 2.8f * k);
      lEllipse(0, y + 3 * k, 1 * k, 1 * k, 0);
      break;
    case NK_BANDANA:
      lTri(-11 * k, y - 2 * k, 11 * k, y - 2 * k, 0, y + 6 * k);
      lPx(-5 * k, y, 0); lPx(0, y + 2 * k, 0); lPx(5 * k, y, 0); lPx(-2.5f * k, y - 0.8f * k, 0);
      lPx(2.5f * k, y - 0.8f * k, 0); lPx(0, y - 1 * k, 0);
      break;
    case NK_PEARLS:
      for (float x = -EXo; x <= EXo + 0.1f; x += 4.2f * k) {
        float u = x / max(1.0f, EXo);
        lEllipse(x, y - 1.5f + (1 - u * u) * 3 * k, 1.6f * k, 1.6f * k);
      }
      break;
    default: break;
  }
}

// ---- Glasses ----------------------------------------------------------------------
static void clearEyes(float padX, float padY) {
  for (int s = -1; s <= 1; s += 2) lRect(s * EXo - EW / 2 - padX, -EH / 2 - padY, EW + 2 * padX, EH + 2 * padY, 0);
}

static void bridge(float y, float inset, float w) {
  lBez(-EXo + EW / 2 + inset, y, 0, y - 2.5f, EXo - EW / 2 - inset, y, w);
}

static void temples(float y, float inset, float w) {
  for (int s = -1; s <= 1; s += 2) lLine(s * (EXo + EW / 2 + inset), y, s * (EXo + EW / 2 + inset + 7), y - 1.5f, w);
}

static void drawGlasses(const Avatar &av, const Pose &p) {
  if (!av.glasses || p.dark) return;
  float k = K;
  switch (av.glasses) {
    case GL_ROUND:
      for (int s = -1; s <= 1; s += 2) lRing(s * EXo, 0, EW / 2 + 3, EH / 2 + 3, 1.6f);
      bridge(-EH * 0.12f, 3, 1.5f);
      temples(-EH * 0.15f, 3, 1.2f);
      break;
    case GL_SQUARE:
      for (int s = -1; s <= 1; s += 2) lFrame(s * EXo - EW / 2 - 3, -EH / 2 - 2.5f, EW + 6, EH + 5, 3, 2);
      bridge(-EH * 0.15f, 3, 1.6f);
      temples(-EH * 0.2f, 3, 1.3f);
      break;
    case GL_SHADES:
      clearEyes(2, 2);
      for (int s = -1; s <= 1; s += 2) {
        float x = s * EXo;
        lRRect(x - EW / 2 - 4, -EH * 0.34f, EW + 8, EH * 0.64f, 5);
        lRect(x - EW / 2 - 4, -EH * 0.34f, EW + 8, 2.5f);
        lLine(x - EW * 0.28f, -EH * 0.2f, x - EW * 0.02f, EH * 0.18f, 1.5f, 0);
      }
      bridge(-EH * 0.26f, 4, 2);
      temples(-EH * 0.26f, 4, 1.5f);
      break;
    case GL_HEART:
    case GL_STAR:
      clearEyes(2, 2);
      for (int s = -1; s <= 1; s += 2) {
        float x = X(s * EXo), y = Y(0);
        if (av.glasses == GL_HEART) {
          heartShape(x, y - 1, W(EW + 6));
          gfx::fillEllipse(x - W(EW) * 0.25f, y - H(EH) * 0.15f, 1.3f, 1.3f, 0);
        } else {
          starShape(x, y, W(EW) * 0.72f, 0);
          gfx::fillEllipse(x - W(EW) * 0.12f, y - H(EH) * 0.12f, 1.1f, 1.1f, 0);
        }
      }
      bridge(-EH * 0.1f, 3, 1.6f);
      break;
    case GL_MONOCLE:
      lRing(EXo, 0, EW / 2 + 3, EH / 2 + 3, 1.8f);
      lBez(EXo + EW * 0.3f, EH / 2 + 2.5f, EXo + EW * 0.4f, EH / 2 + 12, EXo + EW * 0.9f, 40, 1);
      break;
    case GL_3D:
      for (int s = -1; s <= 1; s += 2) {
        float xa, ya, xb, yb;
        lBox(s * EXo - EW / 2 - 3, -EH * 0.38f, EW + 6, EH * 0.76f, xa, ya, xb, yb);
        for (int yy = R(ya); yy < R(yb); yy++)
          for (int xx = R(xa); xx < R(xb); xx++) gfx::pxd(xx, yy, s < 0 ? 8 : 5, 0);
        lFrame(s * EXo - EW / 2 - 3, -EH * 0.38f, EW + 6, EH * 0.76f, 1, 1);
      }
      bridge(-EH * 0.2f, 3, 2);
      temples(-EH * 0.22f, 3, 1.3f);
      break;
    case GL_NERD:
      for (int s = -1; s <= 1; s += 2) lFrame(s * EXo - EW / 2 - 3.5f, -EH / 2 - 2, EW + 7, EH + 4, 2, 3);
      bridge(-EH * 0.12f, 3.5f, 2.5f);
      lRect(-2.5f, -EH * 0.12f - 3.5f, 5, 6);
      lLine(-2.5f, -EH * 0.12f - 3.5f, 2.5f, -EH * 0.12f + 2.5f, 1, 0);
      lLine(-2.5f, -EH * 0.12f + 2.5f, 2.5f, -EH * 0.12f - 3.5f, 1, 0);
      temples(-EH * 0.2f, 3.5f, 1.5f);
      break;
    case GL_VISOR: {
      clearEyes(2, 2);
      float half = EXo + EW / 2 + 5;
      lRRect(-half, -EH * 0.28f, 2 * half, EH * 0.56f, 4);
      for (int s = -1; s <= 1; s += 2) {   // eye slits that still look around
        float x = s * EXo + p.lookX * 4, yy = p.lookY * 2;
        bool happy = p.expr == EX_HAPPY || p.expr == EX_JOY || p.expr == EX_LOVE;
        if (happy) lBez(x - EW * 0.3f, yy + 1.5f, x, yy - 2.5f, x + EW * 0.3f, yy + 1.5f, 2, 0);
        else lLine(x - EW * 0.3f, yy, x + EW * 0.3f, yy, 2, 0);
      }
      float scan = -EH * 0.24f + fmodf(clk * 18, EH * 0.48f);
      lLine(-half + 2, scan, half - 2, scan, 1, 0);
      break;
    }
    case GL_GOGGLES:
      for (int s = -1; s <= 1; s += 2) {
        lRing(s * EXo, 0, EW / 2 + 4, EH / 2 + 3.5f, 3);
        lLine(s * (EXo + EW / 2 + 4), -1, s * 70, -2, 3);
      }
      lLine(-EXo + EW / 2 + 4, -1, EXo - EW / 2 - 4, -1, 2.5f);
      break;
    default: break;
  }
  (void)k;
}

// ---- Hats -------------------------------------------------------------------------
static void drawHat(const Avatar &av, const Pose &p) {
  if (!av.hat || p.dark) return;
  float k = K, b = hatBase, t = clk;
  switch (av.hat) {
    case HAT_TOP: {
      float bw = 30 * k, cw = 19 * k, ch = 13 * k;
      lRRect(-bw / 2, b - 2.5f * k, bw, 2.5f * k, 1);
      lRect(-cw / 2, b - 2.5f * k - ch, cw, ch);
      lRect(-cw / 2, b - 5.8f * k, cw, 1.6f * k, 0);
      break;
    }
    case HAT_PARTY: {
      float bw = 17 * k, hh = 18 * k, tip = 2 * k;
      lTri(-bw / 2, b, bw / 2, b, tip, b - hh);
      for (int i = 1; i <= 2; i++) {
        float yy = b - hh * i / 3.0f, half = bw / 2 * (1 - i / 3.0f);
        lLine(-half + tip * i / 3 - 1, yy + 2, half + tip * i / 3 + 1, yy - 1.5f, 1.3f, 0);
      }
      lEllipse(tip, b - hh, 2.6f * k, 2.6f * k);
      break;
    }
    case HAT_CROWN: {
      float w = 26 * k, bh = 5 * k;
      lRect(-w / 2, b - bh, w, bh);
      lTri(-w / 2, b - bh + 0.5f, -w / 2 + 8 * k, b - bh + 0.5f, -w / 2 + 2 * k, b - bh - 7 * k);
      lTri(-4.5f * k, b - bh + 0.5f, 4.5f * k, b - bh + 0.5f, 0, b - bh - 9 * k);
      lTri(w / 2 - 8 * k, b - bh + 0.5f, w / 2, b - bh + 0.5f, w / 2 - 2 * k, b - bh - 7 * k);
      lEllipse(-w / 2 + 2 * k, b - bh - 7.5f * k, 1.6f * k, 1.6f * k);
      lEllipse(0, b - bh - 9.5f * k, 1.8f * k, 1.8f * k);
      lEllipse(w / 2 - 2 * k, b - bh - 7.5f * k, 1.6f * k, 1.6f * k);
      for (int i = -1; i <= 1; i++) lEllipse(i * 8 * k, b - bh / 2, 1.4f * k, 1.4f * k, 0);
      break;
    }
    case HAT_BEANIE: {
      float rx = 17 * k, ry = 11 * k;
      halfDome(0, b + 1, rx, ry);
      for (float x = -rx + 2; x < rx - 1; x += 3 * k) lLine(x, b - 3.5f * k, x, b + 1, 1, 0);
      lLine(-rx, b - 4 * k, rx, b - 4 * k, 1, 0);
      lEllipse(0, b + 1 - ry - 2 * k, 3.2f * k, 3.2f * k);
      break;
    }
    case HAT_CAP:
      halfDome(0, b + 1, 14 * k, 10 * k);
      lEllipse(13 * k, b, 11 * k, 2.6f * k);
      lLine(0, b + 1 - 9 * k, 0, b, 1, 0);
      lEllipse(0, b + 1 - 10 * k, 1.6f * k, 1.2f * k);
      break;
    case HAT_COWBOY:
      lRRect(-10 * k, b - 12 * k, 20 * k, 11 * k, 4 * k);
      lTri(-3 * k, b - 12.5f * k, 3 * k, b - 12.5f * k, 0, b - 9 * k, 0);
      lRect(-10 * k, b - 4.5f * k, 20 * k, 1.5f * k, 0);
      lTaper(-22 * k, b - 6 * k, 0, b + 3.5f * k, 22 * k, b - 6 * k, 2.8f * k, 2.8f * k);
      break;
    case HAT_WIZARD:
      lTri(-12 * k, b, 12 * k, b, 5 * k, b - 17 * k);
      lBez(5 * k, b - 17 * k, 9 * k, b - 20 * k, 12 * k, b - 16 * k, 1.8f);
      lRect(-16 * k, b - 2 * k, 32 * k, 2.2f * k);
      tinyStar(-3 * k, b - 6 * k, 0);
      tinyStar(3 * k, b - 11 * k, 0);
      tinyStar(-6 * k, b - 3.5f * k, 0);
      break;
    case HAT_CHEF:
      lEllipse(-7 * k, b - 9 * k, 6.5f * k, 6 * k);
      lEllipse(7 * k, b - 9 * k, 6.5f * k, 6 * k);
      lEllipse(0, b - 12 * k, 7.5f * k, 7 * k);
      lRect(-10 * k, b - 5 * k, 20 * k, 5 * k);
      for (int i = -1; i <= 1; i++) lLine(i * 5 * k, b - 4 * k, i * 5 * k, b, 1, 0);
      lLine(-9 * k, b - 5.5f * k, 9 * k, b - 5.5f * k, 1, 0);
      break;
    case HAT_BOW: {
      float x = EXo * 0.85f, y = b + 2 * k;
      lTri(x, y, x - 8 * k, y - 4.5f * k, x - 8 * k, y + 4.5f * k);
      lTri(x, y, x + 8 * k, y - 4.5f * k, x + 8 * k, y + 4.5f * k);
      lLine(x - 5 * k, y - 1.5f * k, x - 5 * k, y + 1.5f * k, 1, 0);
      lLine(x + 5 * k, y - 1.5f * k, x + 5 * k, y + 1.5f * k, 1, 0);
      lEllipse(x, y, 2.2f * k, 2.2f * k);
      break;
    }
    case HAT_FLOWER: {
      float x = -EXo * 0.9f, y = b + 1 * k;
      for (int i = 0; i < 5; i++) {
        float a = i * 1.2566f + t * 0.3f;
        lEllipse(x + cosf(a) * 3.8f * k, y + sinf(a) * 3.8f * k, 2.6f * k, 2.6f * k);
      }
      lEllipse(x, y, 2.2f * k, 2.2f * k, 0);
      lEllipse(x, y, 1 * k, 1 * k);
      break;
    }
    case HAT_PHONES: {
      float ex = EXo + EW / 2 + 4 * k;
      lTaper(-ex, -2 * k, 0, b - 14 * k, ex, -2 * k, 2.4f * k, 2.4f * k);
      for (int s = -1; s <= 1; s += 2) {
        lRRect(s * ex - 3.5f * k, -6 * k, 7 * k, 12 * k, 2.5f * k);
        lLine(s * ex, -3 * k, s * ex, 3 * k, 1, 0);
      }
      break;
    }
    case HAT_HALO:
      lRing(0, b - 5 * k + sinf(t * 2.2f) * 1.2f, 12 * k, 3.2f * k, 1.4f);
      break;
    case HAT_HORNS:
      for (int s = -1; s <= 1; s += 2) {
        float x = s * EXo * 0.9f;
        lTri(x - 3.5f * k, b + 1, x + 3.5f * k, b + 1, x + s * 2.5f * k, b - 5 * k);
        lTri(x + s * 2.5f * k - 1.5f * k, b - 4 * k, x + s * 2.5f * k + 1.2f * k, b - 4 * k, x + s * 5 * k, b - 8 * k);
      }
      break;
    case HAT_ANTENNA: {
      float tx = antX, ty = b - 13 * k;
      lLine(0, b + 1, tx, ty, 1.3f);
      lEllipse(tx, ty, 2.6f * k, 2.6f * k);
      if (fmodf(t, 1.6f) < 0.8f) lEllipse(tx - 0.6f, ty - 0.6f, 0.9f, 0.9f, 0);
      break;
    }
    case HAT_BUNNY: {   // one ear up, one flopped over
      float x = -EXo * 0.35f;
      lEllipse(x, b - 9 * k, 3.6f * k, 10 * k);
      lEllipse(x, b - 8 * k, 1.4f * k, 7 * k, 0);
      float x2 = EXo * 0.35f;
      lEllipse(x2, b - 4 * k, 3.6f * k, 5 * k);
      lTaper(x2, b - 7 * k, x2 + 2 * k, b - 14 * k, x2 + 11 * k, b - 11 * k, 6.5f * k, 4 * k);
      lTaper(x2 + 1 * k, b - 7 * k, x2 + 3 * k, b - 12 * k, x2 + 9 * k, b - 11 * k, 2 * k, 1.2f, 0);
      break;
    }
    case HAT_CAT:
      for (int s = -1; s <= 1; s += 2) {
        float x = s * (EXo + EW * 0.05f);
        lTri(x - 6 * k, b + 2, x + 6 * k, b + 2, x + s * 2.5f * k, b - 10 * k);
        lTri(x - 2.4f * k, b + 0.5f, x + 2.4f * k, b + 0.5f, x + s * 1.6f * k, b - 5.5f * k, 0);
        lLine(x - 2.4f * k, b + 1, x + 2.4f * k, b + 1, 1.2f);
      }
      break;
    case HAT_PROPELLER: {
      halfDome(0, b + 1, 12 * k, 8 * k);
      lLine(-12 * k, b - 2.5f * k, 12 * k, b - 2.5f * k, 1, 0);
      lLine(0, b + 1 - 8 * k, 0, b - 10 * k, 1.3f);
      float bl = 10 * k * fabsf(cosf(t * 18));
      lRect(-bl, b - 11.5f * k, 2 * bl, 2 * k);
      lEllipse(0, b - 10.5f * k, 1.4f * k, 1.4f * k);
      break;
    }
    case HAT_PIRATE:
      lTri(-17 * k, b, 17 * k, b, 0, b - 6 * k);
      halfDome(0, b - 2 * k, 12 * k, 9 * k);
      lEllipse(0, b - 7 * k, 2.4f * k, 2.2f * k, 0);
      lPx(-0.8f * k, b - 7.4f * k);
      lPx(0.8f * k, b - 7.4f * k);
      lLine(-3.5f * k, b - 3 * k, 3.5f * k, b - 5.2f * k, 1, 0);
      lLine(-3.5f * k, b - 5.2f * k, 3.5f * k, b - 3 * k, 1, 0);
      break;
    case HAT_VIKING:
      halfDome(0, b + 1, 13 * k, 9.5f * k);
      lRect(-13 * k, b - 2.5f * k, 26 * k, 1.5f * k, 0);
      for (float x = -10; x <= 10; x += 5) lEllipse(x * k, b - 0.6f * k, 0.8f, 0.8f, 0);
      for (int s = -1; s <= 1; s += 2)   // horns sweep out and up
        lTaper(s * 11 * k, b - 4 * k, s * 17 * k, b - 4 * k, s * 16 * k, b - 12 * k, 4 * k, 1.2f);
      break;
    default: break;
  }
}

// ---- Public -------------------------------------------------------------------------
void draw(const Avatar &av, const Pose &p) {
  layout(av, p);
  if (p.hidden) return;
  for (int i = 0; i < 2; i++) drawEye(av, p, i);
  drawBrows(av, p);
  drawCheeks(av, p);
  drawMouth(av, p);
  drawFaceAcc(av, p);
  drawNeck(av, p);
  drawGlasses(av, p);
  drawHat(av, p);
}

float cx() { return FX; }
float cy() { return FY; }
float eyeX(int i) { return X(i == 0 ? -EXo : EXo); }
float eyeY() { return FY; }
float eyeW() { return W(EW); }
float eyeH() { return H(EH); }
float mouthY() { return Y(mouthLocal); }
float hatTop() { return Y(hatBase - HAT_H[min<uint8_t>(hatNow, 19)] * K); }

}  // namespace face
