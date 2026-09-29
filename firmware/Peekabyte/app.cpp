#include "app.h"
#include "config.h"
#include "gfx.h"
#include "imu.h"
#include "comms.h"
#include "pet.h"
#include "screens.h"
#include "state.h"

namespace app {

static uint32_t lastFrame = 0, lastSim = 0, lastRevive = 0;

// BOOT button: tap = boop the pet, hold = show / hide the connect card.
static void pollButton() {
  static bool down = false, held = false;
  static uint32_t t0 = 0;
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (pressed && !down) {
    down = true;
    held = false;
    t0 = now;
  } else if (pressed && down && !held && now - t0 > 900) {
    held = true;
    pet::toggleCard();
  } else if (!pressed && down) {
    down = false;
    if (!held && now - t0 > 40) pet::boop();
  }
}

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  delay(30);
  Serial.printf("\n\n%s v%s booting\n", APP_NAME, FW_VERSION);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  bool loaded = state::load();
  uint8_t why = (uint8_t)esp_reset_reason();
  state::logBoot(why);
  // Restarted because the supply dipped: use less power from here on (a weak battery can then
  // often keep up), and say so on the screen.
  bool brownout = why == ESP_RST_BROWNOUT;
  if (brownout) setCpuFrequencyMhz(160);
  bool ok = gfx::begin(SET.driver);
  Serial.printf("OLED %s at 0x%02X (%s driver)\n", ok ? "found" : "NOT FOUND", OLED_I2C_ADDR,
                SET.driver ? "SH1106" : "SSD1306");
  if (!imu::begin()) Serial.println("[imu] no motion sensor found - motion features are off");
  pet::begin(loaded);

  char bleName[32];
  snprintf(bleName, sizeof bleName, "Peeka %s", pet::name());
  comms::begin(bleName, pet::handle, pet::connected, brownout);
  Serial.printf("Bluetooth: advertising as \"%s\", free heap %u\n", bleName, (unsigned)ESP.getFreeHeap());
  if (state::runCount()) {
    const state::Run &r = state::runAt(0);
    Serial.printf("[power] last run lasted %lus and ended with reset reason %u%s\n", (unsigned long)r.lasted, r.why,
                  brownout ? " (brownout: low-power mode)" : "");
  }
  if (brownout) screens::toast("Power dipped: saving power", 3500);
  lastFrame = lastSim = millis();
}

void loop() {
  comms::poll();
  pollButton();
  uint32_t now = millis();
  imu::poll(now);
  state::loop();
  if (now - lastSim >= SIM_MS) {
    lastSim += SIM_MS;
    if (now - lastSim > 5 * SIM_MS) lastSim = now;   // don't try to catch up after a stall
    pet::tick();
  }
  if (now - lastFrame >= FRAME_MS) {
    float dt = min(0.1f, (now - lastFrame) / 1000.0f);
    lastFrame = now;
    pet::frame(dt);
    gfx::present();
    comms::pushFrames();
  }
  pet::periodic();
  comms::periodic();
  if (now - lastRevive > 8000) {   // bring the screen back if a power dip reset it
    lastRevive = now;
    gfx::revive();
  }
  delay(1);
}

}  // namespace app
