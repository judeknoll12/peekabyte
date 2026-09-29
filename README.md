# Peekabyte 👀

A pocket pet made of two very expressive eyes. It lives on an ESP32 with a 0.96" OLED, feels you pick it up, shake it and rock it through an MPU6050/6500 motion sensor, and talks to your phone over **Bluetooth**, so your phone stays on its normal Wi‑Fi. The phone app speaks for it using an **open‑source AI that runs on the phone itself**: no accounts, no servers, no subscription.

## Meet your pet

1. Power the Peekabyte. A fresh egg wobbles on the screen.
2. On your iPhone, install the free **Bluefy – Web BLE Browser** from the App Store (Safari can't use Bluetooth from web pages). On Android, Chrome works as is.
3. In Bluefy, open the app: **https://judeknoll12.github.io/peekabyte/**. Hold the pet's **BOOT** button for a second to show a QR code with the link.
4. Tap **Connect with Bluetooth** and pick **Peeka …**.
5. Name your egg (or keep the random name) and tap **Hatch!**

The app reconnects by itself next time. The pet keeps living even when your phone isn't around. Its name and look never change unless you change them in the app.

**Talk to it:** tap the microphone on the Home screen and just talk. It answers out loud and then listens for your reply, back and forth, until you stop talking or tap to hang up. It also speaks up by itself when it wants something, and now and then just to chat (Settings → Talking → Chattiness).

### The iPhone app (recommended on iPhone)

The Peekabyte app does everything the web page does, plus: Bluetooth stays connected while the phone is locked, and the AI brain runs on the iPhone's graphics chip, so it can be several times bigger and smarter (and it doesn't crash the page). It isn't on the App Store; you install it yourself from a Windows PC with a free Apple ID:

1. On the PC, install **Sideloadly** (sideloadly.io). On Windows it also needs **iTunes** and **iCloud**, the versions from Apple's website rather than the Microsoft Store.
2. Download **Peekabyte.ipa** from the [latest iPhone build](https://github.com/judeknoll12/peekabyte/releases/tag/ios-latest).
3. Plug the iPhone into the PC, unlock it and tap **Trust**.
4. In Sideloadly, drop in `Peekabyte.ipa`, enter your Apple ID and press **Start**. It signs the app with a free developer certificate; your Apple ID and password only go to Apple.
5. On the iPhone, turn on **Settings → Privacy & Security → Developer Mode** (it restarts), then trust your Apple ID under **Settings → General → VPN & Device Management**.
6. Open Peekabyte, allow Bluetooth, and connect. In **Settings → AI brain**, pick a brain; it downloads once over Wi‑Fi (keep the app open meanwhile).

Apps signed with a free Apple ID stop opening after 7 days. Run Sideloadly again (or turn on its auto-refresh) to renew it; your pet, settings and downloaded brains stay. AltStore (altstore.io) can instead renew it automatically over Wi‑Fi while the PC is on. A paid Apple Developer account ($99/year) removes the 7-day limit.

| Brain (iPhone app) | Model | Download | Notes |
| --- | --- | --- | --- |
| Sage | Qwen3 1.7B (Apache 2.0) | 1.1 GB | The smartest; needs an iPhone with 6 GB of memory |
| Gem | Gemma 3 1B | 810 MB | Always in character; fits every iPhone |
| Pip | Qwen3 0.6B (Apache 2.0) | 640 MB | The quickest |

The app shows which one fits your iPhone best.

### If it keeps disconnecting

- **Keep the app open.** iPhones pause Bluetooth for apps in the background and when the screen locks. The app keeps the screen awake while the pet is connected (Settings → Connection → Keep the screen on). If your browser can't do that, set Auto‑Lock to Never while you play. Coming back to the app reconnects by itself within a second or two.
- **Tap the green Connected chip** (or Settings → Connection) to see the signal strength and a log of recent drops with the reason the pet saw: signal lost, the phone closed the link, another phone connected, or the pet restarted. **Copy report** puts it all on the clipboard.
- **Signal lost** means too far away or too much in the way. The pet transmits at full power; walls, pockets and microwaves still get in the way.
- **The pet restarted (power dip)** means the USB supply is too weak. Try another cable or charger.
- If the app itself closes while the AI brain or the natural voice is loading, the phone ran out of memory. The app notices, pauses them, and asks what to do. The phone‑voice and phrase‑book modes use almost no memory.

## Taking care of it

| Need | How to help |
| --- | --- |
| 🍎 Food | **Feed**: 12 foods. Every pet has a secret favorite (and one it hates). |
| ⚡ Energy | It goes to bed and wakes up by itself on a schedule you set (Settings → Sleep). Turn the schedule off to run bedtime yourself: tap **Sleep** and **Wake up** (or say "goodnight"). Laying it face‑down or rocking it gently also sends it to sleep. |
| 🎈 Fun | **Play** Snack Catch or Which Way?, teach it tricks, play peekaboo. |
| 💗 Love | Hold **Pet** in the app (its eyes follow your finger; wiggle fast to tickle), talk to it, rock it. |
| 🩺 Health | Neglect makes it sick (it never dies). **Medicine** fixes it. |
| 🧹 Crumbs | Eating is messy. Tap **Clean**, or tilt the pet and the crumbs slide off the screen. |

It grows up from **baby → kid → teen → adult** over a few days of being powered on, and keeps a diary of big moments (Settings → Diary).

### Things it notices

- **Tap** the case → boop! **Double‑tap** → giggles.
- **Shake** it → dizzy spirals. Keep shaking and it gets grumpy (unless it's brave).
- **Drop / toss** it → "Aaah!" **Spin** it → "Wheee!"
- **Face‑down** then back up → peekaboo. Face‑down at bedtime tucks it in.
- **Rock** it side to side → cozy, and sleepy at night.
- **Upside‑down** → the picture flips (auto‑rotate).
- **Pick it up** after it has been resting → it wakes up and says hi.

Settings → Motion calibration teaches it which way is up if your sensor is mounted at an angle.

### Tricks

Twelve built‑in tricks (spin, jump, wink, eye roll, dance, play dead, peekaboo, backflip, moonwalk, sing, bow, magic). You can also **invent your own** from 32 moves, up to 10 per trick. To train one: pick it, tap the command, then reward the attempt with a 🍪 treat or 👏 praise. Rewarding successes teaches fastest. At 80% a trick is mastered, and a happy pet shows off its mastered tricks by itself. In chat, "do a spin!" works too.

### Style

8 eye shapes, 5 sizes, 5 spacings, 7 pupil styles, lashes, brows, 19 hats, 10 glasses, moustaches and beards, mouths, cheeks and neckwear. Changes preview live on the pet; **Save look** keeps it.

## Voice and AI brain

**Voices** (Settings → Voice), all adjustable (pitch, speed, and robot/echo/walkie‑talkie/cave effects):

- **Nemotron‑style**: a natural, warm voice in the style of NVIDIA's Nemotron voice‑agent demos, made with the open‑source **Kokoro** model running on the phone (a one‑time ~90 MB download). NVIDIA's own Nemotron speech model needs a desktop GPU, so it can't run on a phone.
- More Kokoro voices (Sunny, Buddy, Squeaky, Robo), two **babble** voices (instant, Animal Crossing style) and the phone's built‑in voice.
- In the iPhone app, the **app itself plays every sound** (not the web page inside it), so the pet talks without a tap first, with the ring switch on silent, and after calls, Siri or the microphone. The **phone voice** there is Apple's own speech engine: download an *Enhanced* or *Premium* voice in iPhone Settings › Accessibility › Spoken Content (Read & Speak on newer iPhones) › Voices › English, then pick it in Settings → Voice → Phone voice. It's instant and fills in whenever the natural voice isn't ready in time.
- Settings → Voice shows how the last line was actually said (natural voice, phone voice, or why neither), where the sound goes and how loud the phone is.

**Talking.** Tap the microphone and speak. In the iPhone app your words are recognized by the iPhone's own speech recognition, on the phone itself when it supports that (iPhones from the XS on do, for English); in a browser, by the browser's speech recognition. While you talk the pet looks up at you and perks up with your voice. It speaks up when it wants something (again every few minutes until it gets it) and starts little conversations on its own; **Chattiness** sets how often.

**Looks.** Settings → Appearance: light, dark or match the phone, and five colors.

**AI brain** (Settings → AI brain). The pet chats and reacts in character using a small open‑source language model, run in the browser by [Transformers.js](https://github.com/huggingface/transformers.js):

| Brain | Model | Download | Notes |
| --- | --- | --- | --- |
| Pip | SmolLM2 360M (Apache 2.0) | ~360 MB | The best fit for iPhone |
| Gem | Gemma 3 270M | ~310 MB | Tiny and chatty |
| Sage | Qwen3 0.6B (Apache 2.0) | ~560 MB | The smartest small brain; needs a recent phone |
| Nemo | NVIDIA Nemotron 3 Nano 4B | ~2.2 GB | Needs a computer or iPad with a strong GPU |

With the brain off, it uses a built‑in phrase book (instant). Everything runs on the phone.

## Hardware

| Part | Pin | ESP32 |
| --- | --- | --- |
| OLED 0.96" SSD1306 (0x3C) | SDA / SCL | GPIO 21 / GPIO 22 |
| MPU6050 or MPU6500 (0x68) | SDA / SCL | GPIO 21 / GPIO 22 (same bus) |
| both | VCC / GND | 3V3 / GND |

## Files

```
firmware/Peekabyte/   Arduino sketch (the pet)
  pet.cpp               needs, moods, sleep, reactions, tricks, messages
  face.cpp              eyes, expressions and every accessory
  act.cpp               moves and trick sequences
  fx.cpp                hearts, Zzz, crumbs, speech bubbles
  games.cpp             Snack Catch and Which Way?
  imu.cpp               MPU6050/6500 motion events
  comms.cpp             Bluetooth LE + USB bridge, screen mirror
  screens.cpp, gfx.cpp, state.cpp, protocol.h, petdata.h, sprites.h
app/                  the phone app (static site, no build step)
  js/main.js            UI
  js/link.js            Web Bluetooth / USB bridge connection
  js/brain.js           on-device AI (llm-worker.js runs the model)
  js/voice.js           voices + effects (tts-worker.js runs Kokoro)
  js/listen.js          talking to the pet with your voice
ios/                  the iPhone app: a native shell around app/ (Swift)
  Bridge.swift          messages between the page and the phone (see app/js/native.js)
  BLE.swift             Bluetooth that stays up in the background
  LLM.swift             llama.cpp on the GPU; ModelStore.swift downloads the brains
  Listen.swift          speech recognition for talking to the pet
design/               the logo and app icon (SVG); tools/render_art.py makes the PNGs
tools/usb_bridge.py   run the app on your computer over USB (no Bluetooth needed)
tools/ble_test.py     Bluetooth smoke test from a computer (pip install bleak)
```

## Build and flash

Arduino IDE, board **ESP32 Dev Module**, Partition Scheme **Huge APP (3MB No OTA)**, library **U8g2**. Or with the CLI:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --upload -p COM5 firmware/Peekabyte
```

To go back to GlowByte, flash `Documents/GlowByte/GlowByte` the same way.

## Working on the app

`python tools/usb_bridge.py --port COM5` serves `app/` at http://localhost:8080 and talks to the pet over the USB cable. `localhost` counts as a secure page, so the AI and voices work there too. `python tools/usb_bridge.py snap oled.png` saves what the screen shows.

## Hosting the app

The app is plain files in `app/`. The included GitHub Actions workflow publishes them to GitHub Pages on every push: in the repository, open **Settings → Pages** and set **Source** to **GitHub Actions**. Web Bluetooth needs an https page, which is why the app is hosted rather than served by the pet itself.

The iPhone app (`ios/`) loads the same page, so app changes reach it without reinstalling. Changes under `ios/` rebuild it: `.github/workflows/ios.yml` fetches llama.cpp's prebuilt iOS framework, generates the Xcode project with XcodeGen, builds an unsigned `.ipa` on a GitHub-hosted Mac, and publishes it on the `ios-latest` release.
