// The pet's voice: Kokoro (open-source neural TTS), the phone's own voices, or babble.
// Pitch, speed and effects are all adjustable; while speaking we report a loudness
// level so the pet's face can move with the words.
//
// Kokoro runs natively in the iPhone app (sherpa-onnx on the CPU, fast), or in a web worker
// elsewhere (slower on phones). Either way a line that isn't ready in time is said by the
// phone's voice instead, so the pet never goes quiet. In the iPhone app, the app itself plays
// every line (audio.js explains why); pitch and effects are drawn into the samples first.

import {
  Offline, RATE, envelope, native, nativeReady, playNative, sayNative, stopNative, trimEnd,
} from './audio.js';
import { call, isNative, on } from './native.js';
import { audioCtx } from './sfx.js';

export { nativeVoices as phoneVoices } from './audio.js';

export const PRESETS = [
  { id: 'nemo', name: 'Nemotron-style', desc: 'Natural and warm, in the style of NVIDIA\'s Nemotron voice agent', engine: 'kokoro', voice: 'af_heart', pitch: 0, speed: 1.0, fx: 'none' },
  { id: 'sunny', name: 'Sunny', desc: 'Bright and bubbly', engine: 'kokoro', voice: 'af_bella', pitch: 3, speed: 1.05, fx: 'none' },
  { id: 'buddy', name: 'Buddy', desc: 'Playful little dude', engine: 'kokoro', voice: 'am_puck', pitch: 3, speed: 1.05, fx: 'none' },
  { id: 'squeaky', name: 'Squeaky', desc: 'Tiny and high', engine: 'kokoro', voice: 'af_sky', pitch: 7, speed: 1.08, fx: 'none' },
  { id: 'robo', name: 'Robo', desc: 'Beep boop, metal voice', engine: 'kokoro', voice: 'am_echo', pitch: -1, speed: 1.0, fx: 'robot' },
  { id: 'tiny', name: 'Tiny babble', desc: 'Animal Crossing-style chirps', engine: 'babble', voice: '', pitch: 5, speed: 1.1, fx: 'none' },
  { id: 'grumbly', name: 'Grumbly babble', desc: 'Low, mumbly gibberish', engine: 'babble', voice: '', pitch: -5, speed: 0.9, fx: 'none' },
  { id: 'phone', name: 'Phone voice', desc: 'Your phone\'s built-in voice (no download)', engine: 'system', voice: '', pitch: 4, speed: 1.05, fx: 'none' },
];

export const KOKORO_VOICES = [
  ['af_heart', 'Heart (US, warm)'], ['af_bella', 'Bella (US)'], ['af_nicole', 'Nicole (US, soft)'],
  ['af_nova', 'Nova (US)'], ['af_sky', 'Sky (US)'], ['af_sarah', 'Sarah (US)'], ['af_river', 'River (US)'],
  ['am_puck', 'Puck (US)'], ['am_adam', 'Adam (US)'], ['am_echo', 'Echo (US)'], ['am_michael', 'Michael (US)'],
  ['am_liam', 'Liam (US)'], ['am_fenrir', 'Fenrir (US, deep)'], ['bf_emma', 'Emma (UK)'], ['bf_lily', 'Lily (UK)'],
  ['bm_george', 'George (UK)'], ['bm_fable', 'Fable (UK)'],
];

// Speaker numbers in the Kokoro v1.0 voice table (read from the model's own metadata).
const SPEAKER = {
  af_bella: 2, af_heart: 3, af_nicole: 6, af_nova: 7, af_river: 8, af_sarah: 9, af_sky: 10, am_adam: 11,
  am_echo: 12, am_fenrir: 14, am_liam: 15, am_michael: 16, am_puck: 18, bf_emma: 21, bf_lily: 23, bm_fable: 25, bm_george: 26,
};

// The native voice's files (iPhone app): Kokoro v1.0 packaged for sherpa-onnx, Apache 2.0.
const hf = (file) => `https://huggingface.co/csukuangfj/kokoro-multi-lang-v1_0/resolve/main/${file}`;
export const NATIVE_VOICE_FILES = [
  { file: 'kokoro-v1_0.onnx', url: hf('model.onnx'), mb: 326 },
  { file: 'kokoro-voices-v1_0.bin', url: hf('voices.bin'), mb: 28 },
];
export const kokoroDownloadMB = () => (nativeTts ? 354 : 92);

export const EFFECTS = [['none', 'None'], ['robot', 'Robot'], ['echo', 'Echo'], ['radio', 'Walkie-talkie'], ['cave', 'Cave']];

// Does this iPhone app build have the native voice? (Older builds answer "unknown request".)
let nativeTts = false;
export const nativeVoiceCheck = isNative
  ? call('tts.state').then(() => { nativeTts = true; }, () => { nativeTts = false; })
  : Promise.resolve();

export function cleanForSpeech(text) {
  return text
    .replace(/\[[^\]]*\]/g, ' ')
    .replace(/[*_~`#>]/g, ' ')
    .replace(/\p{Extended_Pictographic}|‍|️/gu, '')
    .replace(/\s+/g, ' ')
    .trim();
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Make sure the sound engine is actually playing (iOS parks it after interruptions).
async function audioRunning() {
  const c = audioCtx();
  if (c.state !== 'running') {
    try { await Promise.race([c.resume(), sleep(500)]); } catch { /* not allowed yet */ }
  }
  return c.state === 'running';
}

export class Voice extends EventTarget {
  constructor() {
    super();
    this.cfg = { ...PRESETS[0], preset: 'nemo', volume: 1 };
    this.kState = 'off';      // off | loading | ready | error
    this.kProgress = 0;
    this.kInfo = '';
    this.kBackend = '';       // native | web
    this.kSlowness = 0;       // seconds of work per second of speech, measured on real lines
    this.kWhere = '';         // where it runs, for the settings page
    this.rendering = 0;       // lines being made right now
    this.kMisses = 0;         // lines in a row that weren't ready in time
    this.kTooSlow = false;    // gave up on Kokoro for now: the phone voice fills in
    this.worker = null;
    this.pending = new Map();
    this.queue = [];
    this.busy = false;
    this.current = null;
    this.nextId = 1;
    this.last = null;         // how the last line came out: { how, note, at }
    this.kNapping = false;    // the iPhone app let the voice go to free memory; it comes back when needed
    if (isNative) {
      on('tts.unloaded', () => {
        if (this.kState !== 'ready') return;
        this.kNapping = true;
        this.emit();
      });
    }
  }

  configure(cfg) {
    this.cfg = { ...this.cfg, ...cfg };
  }

  status() {
    return {
      engine: this.cfg.engine, kokoro: this.kState, progress: this.kProgress, info: this.kInfo, tooSlow: this.kTooSlow,
      backend: this.kBackend, last: this.last, appPlays: native.ok === true,
    };
  }

  emit() { this.dispatchEvent(new CustomEvent('status', { detail: this.status() })); }

  get kokoroOn() { return this.cfg.engine === 'kokoro' && this.kState === 'ready' && !this.kTooSlow; }

  async loadKokoro() {
    if (this.kState === 'loading' || this.kState === 'ready') return;
    this.kState = 'loading';
    this.kProgress = 0;
    this.kInfo = '';
    this.kTooSlow = false;
    this.kMisses = 0;
    this.emit();
    await nativeVoiceCheck;
    if (nativeTts) this.loadNative();
    else this.loadWeb();
  }

  // ---- Kokoro in the iPhone app ---------------------------------------------------------
  async loadNative() {
    this.kBackend = 'native';   // runs in the app, not the page
    try {
      const { files } = await call('llm.files');
      const have = new Set(files.map((f) => f.file));
      const missing = NATIVE_VOICE_FILES.filter((f) => !have.has(f.file));
      const total = missing.reduce((a, f) => a + f.mb, 0);
      let done = 0;
      for (const f of missing) {
        await this.downloadNative(f, (share) => {
          this.kProgress = (done + share * f.mb) / total;
          this.emit();
        });
        done += f.mb;
      }
      this.kProgress = 1;
      this.kInfo = 'Warming up…';
      this.emit();
      const r = await call('tts.load', { model: NATIVE_VOICE_FILES[0].file, voices: NATIVE_VOICE_FILES[1].file });
      this.kBackend = 'native';
      this.kWhere = r.rate ? 'built into the app' : 'on this iPhone';
      this.kState = 'ready';
      this.kInfo = this.speedNote();
      this.emit();
    } catch (e) {
      this.kState = 'error';
      this.kInfo = String(e?.message || e);
      this.emit();
    }
  }

  downloadNative(f, onShare) {
    return new Promise((resolve, reject) => {
      const offs = [];
      const finish = (err) => { offs.forEach((off) => off()); if (err) reject(err); else resolve(); };
      offs.push(on('llm.progress', (d) => { if (d.file === f.file && d.total) onShare(d.loaded / d.total); }));
      offs.push(on('llm.downloaded', (d) => { if (d.file === f.file) finish(); }));
      offs.push(on('llm.failed', (d) => { if (d.file === f.file) finish(new Error(d.error)); }));
      call('llm.download', { url: f.url, file: f.file, bytes: f.mb * 1e6 }).catch(finish);
    });
  }

  // ---- Kokoro in a web worker ---------------------------------------------------------
  loadWeb() {
    if (this.worker) return;
    const w = new Worker(new URL('./tts-worker.js', import.meta.url), { type: 'module' });
    this.worker = w;
    const files = new Map();
    w.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'progress') {
        const p = m.p;
        if (p.file && p.total) files.set(p.file, [p.loaded || 0, p.total]);
        let a = 0, b = 0;
        for (const [l, t] of files.values()) { a += l; b += t; }
        if (b) this.kProgress = a / b;
        if (this.kProgress >= 0.999) this.kInfo = 'Warming up…';
        this.emit();
      } else if (m.type === 'ready') {
        this.kBackend = 'web';
        this.warmUpWeb(m);
      } else if (m.type === 'error') {
        this.failWorker(m.message);
      } else if (m.type === 'audio' || m.type === 'fail') {
        const cb = this.pending.get(m.id);
        this.pending.delete(m.id);
        cb?.(m);
      }
    };
    w.onerror = (e) => this.failWorker(e.message || 'The voice stopped working');
    // Phones get the small CPU model (~90 MB): the GPU one is ~330 MB, and a page that uses
    // too much memory gets killed, taking the Bluetooth link with it.
    const phone = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent) || navigator.userAgentData?.mobile;
    w.postMessage({ type: 'load', device: phone ? 'wasm' : 'auto' });
  }

  // Say a short word to itself first: the very first line is always slow (setting up).
  async warmUpWeb(m) {
    await this.renderWeb('Hi there.', 1).catch(() => null);
    this.kWhere = `${m.device === 'webgpu' ? 'GPU' : 'CPU'} · ${m.dtype}`;
    this.kState = 'ready';
    this.kInfo = this.speedNote();
    this.emit();
  }

  noteSpeed(x) {
    this.kSlowness = this.kSlowness ? this.kSlowness * 0.6 + x * 0.4 : x;
    this.kInfo = this.speedNote();
    this.emit();
  }

  speedNote() {
    if (!this.kSlowness) return this.kWhere;
    const lag = this.kSlowness * 2.5;   // a typical line is about 2.5 seconds long
    return `${this.kWhere} · a short line takes about ${lag < 1 ? 'under a second' : `${lag.toFixed(1)} s`} to make`;
  }

  failWorker(message) {
    this.kState = 'error';
    this.kInfo = message;
    this.worker?.terminate();
    this.worker = null;
    for (const cb of this.pending.values()) cb({ type: 'fail', message });   // don't leave any line waiting
    this.pending.clear();
    this.emit();
  }

  renderWeb(text, speed) {
    return new Promise((resolve, reject) => {
      if (!this.worker) { reject(new Error('The voice isn\'t loaded')); return; }
      const id = this.nextId++;
      this.pending.set(id, (m) => (m.type === 'audio' ? resolve({ pcm: m.audio, rate: m.rate }) : reject(new Error(m.message))));
      this.worker.postMessage({ type: 'speak', id, text, voice: this.cfg.voice || 'af_heart', speed });
    });
  }

  async renderNative(text, speed) {
    if (this.kNapping) {   // set aside to free memory: load it again first (the phone voice covers meanwhile)
      await call('tts.load', { model: NATIVE_VOICE_FILES[0].file, voices: NATIVE_VOICE_FILES[1].file });
      this.kNapping = false;
    }
    const r = await call('tts.speak', { text, sid: SPEAKER[this.cfg.voice] ?? 3, speed });
    const bin = atob(r.pcm);
    const pcm = new Float32Array(bin.length >> 1);
    for (let i = 0; i < pcm.length; i++) {
      const v = bin.charCodeAt(2 * i) | (bin.charCodeAt(2 * i + 1) << 8);
      pcm[i] = (v > 32767 ? v - 65536 : v) / 32768;
    }
    return { pcm, rate: r.rate };
  }

  // Start making the audio for a line right away (while earlier lines are still playing).
  prepare(item) {
    if (!this.kokoroOn || item.audio) return;
    const pitchRate = 2 ** (this.cfg.pitch / 12);
    const speed = Math.max(0.5, Math.min(2, this.cfg.speed / pitchRate));
    item.pitchRate = pitchRate;
    const started = performance.now();
    const alone = this.rendering === 0;   // only time lines that didn't wait behind another
    this.rendering++;
    item.audio = (this.kBackend === 'native' ? this.renderNative(item.text, speed) : this.renderWeb(item.text, speed))
      .then((a) => {
        if (alone) this.noteSpeed((performance.now() - started) / 1000 / (a.pcm.length / a.rate));
        return a;
      })
      .finally(() => { this.rendering--; });
    item.audio.catch(() => {});   // looked at later; a failure just means the phone voice says it
    item.queuedAt = performance.now();
  }

  // Queue a line; resolves when it has been spoken (or skipped).
  speak(text, hooks = {}) {
    const clean = cleanForSpeech(text);
    if (!clean) return Promise.resolve();
    return new Promise((resolve) => {
      if (this.queue.length > 2) this.queue.splice(0, this.queue.length - 2).forEach((q) => q.resolve());   // don't fall behind
      const item = { text: clean, hooks, resolve };
      this.prepare(item);
      this.queue.push(item);
      this.run();
    });
  }

  // Resolves once everything queued has been said (or after 8 s at most).
  idle() {
    return new Promise((resolve) => {
      const give = setTimeout(done, 8000);
      const poll = setInterval(() => { if (!this.busy && !this.queue.length) done(); }, 150);
      function done() { clearTimeout(give); clearInterval(poll); resolve(); }
    });
  }

  stop() {
    this.queue.forEach((q) => q.resolve());
    this.queue = [];
    this.current?.stop?.();
    if (native.ok) stopNative('voice');
    else window.speechSynthesis?.cancel?.();
  }

  async run() {
    if (this.busy) return;
    this.busy = true;
    await nativeReady;   // does the app play sound itself? (asked once, at startup)
    while (this.queue.length) {
      const item = this.queue.shift();
      const eng = this.cfg.engine;
      try {
        if (eng === 'kokoro' && this.kokoroOn) await this.sayKokoro(item);
        else if (eng === 'babble') await this.sayBabble(item);
        else await this.sayPhone(item, eng === 'kokoro' ? this.whyNotKokoro() : '');
      } catch (e) {
        console.warn('speak failed', e);
        this.noteLast('none', String(e?.message || e));
      }
      item.hooks.onLevel?.(0);
      item.hooks.onEnd?.();
      item.resolve();
    }
    this.busy = false;
  }

  // How the last line actually came out, for the settings page and the report.
  noteLast(how, note = '') {
    this.last = { how, note, at: Date.now() };
    this.emit();
  }

  whyNotKokoro() {
    if (this.kTooSlow) return 'the natural voice was too slow, so the phone voice is filling in';
    if (this.kState === 'loading') return 'the natural voice is still getting ready';
    if (this.kState === 'error') return `the natural voice failed: ${this.kInfo}`;
    return "the natural voice isn't downloaded";
  }

  // ---- Kokoro ------------------------------------------------------------------------
  async sayKokoro(item) {
    this.prepare(item);
    // How long we'll wait: long enough for a healthy voice, short enough not to feel broken.
    const budget = (this.kBackend === 'native' ? 2500 : 4000) + item.text.length * (this.kBackend === 'native' ? 40 : 90);
    const waited = performance.now() - (item.queuedAt || performance.now());
    let audio = null;
    let why = 'the natural voice took too long';
    try {
      audio = await Promise.race([item.audio, sleep(Math.max(1500, budget - waited)).then(() => null)]);
    } catch (e) {
      why = `the natural voice failed (${e?.message || e})`;
    }
    if (!audio) {
      this.kMisses++;
      if (this.kMisses >= 3) {   // it keeps missing: stop trying for now, the phone voice takes over
        this.kTooSlow = true;
        this.emit();
      }
      return this.sayPhone(item, why);
    }
    this.kMisses = 0;
    const pcm = await this.shape(audio.pcm, audio.rate, item.pitchRate || 1);
    const r = await this.playLine(pcm, RATE, item);
    if (r.ok) return this.noteLast('natural');
    return this.sayPhone(item, `the natural voice couldn't play (${r.error})`);
  }

  // Pitch and effects, drawn into new samples without playing anything.
  async shape(pcm, rate, playbackRate) {
    const fx = this.cfg.fx || 'none';
    if (fx === 'none' && Math.abs(playbackRate - 1) < 0.01 && rate === RATE) return pcm;
    if (!Offline) return pcm;
    try {
      const tail = { echo: 1.2, cave: 1.8 }[fx] || 0.05;
      const oc = new Offline(1, Math.ceil((pcm.length / rate / playbackRate + tail) * RATE), RATE);
      const buf = oc.createBuffer(1, pcm.length, rate);
      buf.getChannelData(0).set(pcm);
      const src = oc.createBufferSource();
      src.buffer = buf;
      src.playbackRate.value = playbackRate;
      buildEffect(oc, fx, src).connect(oc.destination);
      src.start();
      const out = await Promise.race([oc.startRendering(), sleep(3000).then(() => null)]);
      return out ? trimEnd(out.getChannelData(0), 0.0003) : pcm;
    } catch (e) {
      console.warn('voice effects failed', e);
      return pcm;
    }
  }

  // Play finished samples: by the iPhone app when it can, else with Web Audio. The pet's mouth
  // follows the loudness of the samples. Resolves with { ok, error }.
  async playLine(pcm, rate, item) {
    const env = envelope(pcm, rate);
    let meter = null;
    const onStart = () => {
      item.hooks.onStart?.();
      const t0 = performance.now();
      meter = setInterval(() => {
        const i = Math.floor((performance.now() - t0) / 50);
        item.hooks.onLevel?.(Math.min(1, (env[i] || 0) * 5));
      }, 60);
    };
    const volume = this.cfg.volume ?? 1;
    let r;
    if (native.ok) {
      this.current = { stop: () => stopNative('voice') };
      r = await playNative(pcm, rate, { channel: 'voice', volume, onStart });
    } else {
      r = await this.playWeb(pcm, rate, volume, onStart);
    }
    clearInterval(meter);
    this.current = null;
    return r;
  }

  async playWeb(pcm, rate, volume, onStart) {
    if (!(await audioRunning())) return { ok: false, error: 'sound is blocked until you tap the screen' };
    const c = audioCtx();
    const buf = c.createBuffer(1, pcm.length, rate);
    buf.getChannelData(0).set(pcm);
    const src = c.createBufferSource();
    src.buffer = buf;
    const gain = c.createGain();
    gain.gain.value = volume;
    src.connect(gain).connect(c.destination);
    return new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) return;
        done = true;
        this.current = null;
        resolve({ ok: true, error: '' });
      };
      src.onended = finish;
      this.current = { stop: () => { try { src.stop(); } catch { /* already stopped */ } finish(); } };
      onStart();
      src.start();
      setTimeout(finish, buf.duration * 1000 + 1500);   // safety net
    });
  }

  // ---- Phone voice ----------------------------------------------------------------------
  // In the iPhone app: the phone's own speech engine, played by the app (instant, and the
  // Enhanced / Premium voices from iPhone Settings sound very natural). In a browser: its
  // speech synthesis.
  async sayPhone(item, why = '') {
    if (native.ok) {
      let level = 0;
      const pulse = setInterval(() => { level *= 0.7; item.hooks.onLevel?.(level); }, 60);
      this.current = { stop: () => stopNative('voice') };
      const r = await sayNative(item.text, {
        voice: this.cfg.nativeVoice || '',
        rate: Math.max(0.5, Math.min(2, this.cfg.speed || 1)),
        pitch: Math.max(0.5, Math.min(2, 2 ** ((this.cfg.pitch || 0) / 12))),
        volume: this.cfg.volume ?? 1,
        onStart: () => item.hooks.onStart?.(),
        onWord: () => { level = 0.7 + Math.random() * 0.3; },   // the mouth moves with each word
      });
      clearInterval(pulse);
      this.current = null;
      return this.noteLast(r.ok ? 'phone' : 'none', r.ok ? why : r.error);
    }
    await this.saySystem(item);
    return this.noteLast('phone', why);
  }

  saySystem(item) {
    return new Promise((resolve) => {
      if (!window.speechSynthesis) return resolve();
      const u = new SpeechSynthesisUtterance(item.text);
      const v = speechSynthesis.getVoices().find((x) => x.name === this.cfg.systemVoice);
      if (v) u.voice = v;
      else u.lang = 'en-US';
      u.pitch = Math.max(0.1, Math.min(2, 1 + this.cfg.pitch / 12));
      u.rate = Math.max(0.5, Math.min(2, this.cfg.speed));
      u.volume = this.cfg.volume ?? 1;
      let pulse = null;
      const end = () => {
        clearInterval(pulse);
        this.current = null;
        resolve();
      };
      u.onstart = () => {
        item.hooks.onStart?.();
        pulse = setInterval(() => item.hooks.onLevel?.(0.25 + Math.random() * 0.6), 90);
      };
      u.onend = end;
      u.onerror = end;
      this.current = { stop: () => { speechSynthesis.cancel(); end(); } };
      speechSynthesis.speak(u);
      setTimeout(() => { if (this.current) end(); }, 4000 + item.text.length * 120);
    });
  }

  // ---- Babble: chirpy gibberish that follows the syllables ---------------------------------
  async sayBabble(item) {
    const pcm = await this.renderBabble(item.text);
    const r = await this.playLine(pcm, RATE, item);
    if (r.ok) return this.noteLast('babble');
    return this.sayPhone(item, `babble couldn't play (${r.error})`);
  }

  async renderBabble(text) {
    const base = 330 * 2 ** (this.cfg.pitch / 12);
    const speed = this.cfg.speed || 1;
    const syl = text.toLowerCase().match(/[bcdfghjklmnpqrstvwxyz]*[aeiouy]+|[.,!?]/g) || ['a'];
    const FORMANT = { a: 900, e: 1900, i: 2400, o: 650, u: 450, y: 2100 };
    const question = text.trim().endsWith('?');
    const notes = [];
    let t = 0.05;
    syl.forEach((s, i) => {
      if (/[.,!?]/.test(s)) { t += 0.12 / speed; return; }
      const dur = (0.065 + Math.random() * 0.03) / speed;
      let f = base * 2 ** ((Math.random() * 5 - 2) / 12);
      if (question && i > syl.length - 3) f *= 1.25;
      notes.push({ t, dur, f, formant: FORMANT[s.match(/[aeiouy]/)[0]] || 900 });
      t += dur + 0.018 / speed;
    });
    const oc = new Offline(1, Math.ceil((t + ({ echo: 1.2, cave: 1.8 }[this.cfg.fx] || 0.1)) * RATE), RATE);
    const bus = oc.createGain();
    bus.gain.value = 0.9;
    buildEffect(oc, this.cfg.fx, bus).connect(oc.destination);
    for (const n of notes) {
      const o = oc.createOscillator(), bp = oc.createBiquadFilter(), g = oc.createGain();
      o.type = 'square';
      o.frequency.setValueAtTime(n.f, n.t);
      o.frequency.exponentialRampToValueAtTime(n.f * 1.06, n.t + n.dur);
      bp.type = 'bandpass';
      bp.frequency.value = n.formant;
      bp.Q.value = 3;
      g.gain.setValueAtTime(0.0001, n.t);
      g.gain.exponentialRampToValueAtTime(0.5, n.t + 0.008);
      g.gain.exponentialRampToValueAtTime(0.0001, n.t + n.dur);
      o.connect(bp).connect(g).connect(bus);
      o.start(n.t);
      o.stop(n.t + n.dur + 0.02);
    }
    return normalize(trimEnd((await oc.startRendering()).getChannelData(0), 0.0003), 0.7);
  }
}

// Bring the loudest moment up (or down) to `peak`: synth voices come out quiet otherwise.
function normalize(pcm, peak) {
  let max = 0;
  for (let i = 0; i < pcm.length; i++) max = Math.max(max, Math.abs(pcm[i]));
  if (max < 1e-4) return pcm;
  const k = peak / max;
  const out = new Float32Array(pcm.length);
  for (let i = 0; i < pcm.length; i++) out[i] = pcm[i] * k;
  return out;
}

// ---- Effects --------------------------------------------------------------------------------
function buildEffect(c, fx, input) {
  switch (fx) {
    case 'robot': {   // ring modulation plus a short metallic comb
      const ring = c.createGain();
      ring.gain.value = 0;
      const osc = c.createOscillator();
      osc.frequency.value = 55;
      osc.connect(ring.gain);
      osc.start();
      input.connect(ring);
      const dly = c.createDelay(0.05), fb = c.createGain(), mix = c.createGain();
      dly.delayTime.value = 0.007;
      fb.gain.value = 0.45;
      ring.connect(mix);
      ring.connect(dly).connect(fb).connect(dly);
      dly.connect(mix);
      const dry = c.createGain();
      dry.gain.value = 0.35;
      input.connect(dry).connect(mix);
      setTimeout(() => osc.stop(), 30000);
      return mix;
    }
    case 'echo': {
      const out = c.createGain(), d = c.createDelay(1), fb = c.createGain(), wet = c.createGain();
      d.delayTime.value = 0.23;
      fb.gain.value = 0.35;
      wet.gain.value = 0.45;
      input.connect(out);
      input.connect(d).connect(fb).connect(d);
      d.connect(wet).connect(out);
      return out;
    }
    case 'radio': {
      const hp = c.createBiquadFilter(), lp = c.createBiquadFilter(), sh = c.createWaveShaper();
      hp.type = 'highpass';
      hp.frequency.value = 500;
      lp.type = 'lowpass';
      lp.frequency.value = 3200;
      const curve = new Float32Array(256);
      for (let i = 0; i < 256; i++) { const x = i / 128 - 1; curve[i] = Math.tanh(x * 3); }
      sh.curve = curve;
      input.connect(hp).connect(sh).connect(lp);
      return lp;
    }
    case 'cave': {
      const conv = c.createConvolver(), out = c.createGain(), wet = c.createGain();
      const len = Math.floor(c.sampleRate * 1.6), ir = c.createBuffer(2, len, c.sampleRate);
      for (let ch = 0; ch < 2; ch++) {
        const d = ir.getChannelData(ch);
        for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / len) ** 3;
      }
      conv.buffer = ir;
      wet.gain.value = 0.55;
      input.connect(out);
      input.connect(conv).connect(wet).connect(out);
      return out;
    }
    default:
      return input;
  }
}
