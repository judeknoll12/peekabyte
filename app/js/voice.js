// The pet's voice: Kokoro (open-source neural TTS), the phone's own voices, or babble.
// Pitch, speed and effects are all adjustable; while speaking we report a loudness
// level so the pet's face can move with the words.

import { audioCtx, output } from './sfx.js';

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

export const EFFECTS = [['none', 'None'], ['robot', 'Robot'], ['echo', 'Echo'], ['radio', 'Walkie-talkie'], ['cave', 'Cave']];

export function cleanForSpeech(text) {
  return text
    .replace(/\[[^\]]*\]/g, ' ')
    .replace(/[*_~`#>]/g, ' ')
    .replace(/\p{Extended_Pictographic}|‍|️/gu, '')
    .replace(/\s+/g, ' ')
    .trim();
}

export class Voice extends EventTarget {
  constructor() {
    super();
    this.cfg = { ...PRESETS[0], preset: 'nemo', volume: 1 };
    this.kState = 'off';      // off | loading | ready | error
    this.kProgress = 0;
    this.kInfo = '';
    this.worker = null;
    this.pending = new Map();
    this.queue = [];
    this.busy = false;
    this.current = null;
    this.nextId = 1;
  }

  configure(cfg) {
    this.cfg = { ...this.cfg, ...cfg };
  }

  status() {
    return { engine: this.cfg.engine, kokoro: this.kState, progress: this.kProgress, info: this.kInfo };
  }

  emit() { this.dispatchEvent(new CustomEvent('status', { detail: this.status() })); }

  loadKokoro() {
    if (this.worker) return;
    this.kState = 'loading';
    this.kProgress = 0;
    this.emit();
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
        this.emit();
      } else if (m.type === 'ready') {
        this.kState = 'ready';
        this.kInfo = `${m.device === 'webgpu' ? 'GPU' : 'CPU'} · ${m.dtype}`;
        this.emit();
      } else if (m.type === 'error') {
        this.kState = 'error';
        this.kInfo = m.message;
        this.worker = null;
        this.emit();
      } else if (m.type === 'audio' || m.type === 'fail') {
        const cb = this.pending.get(m.id);
        this.pending.delete(m.id);
        cb?.(m);
      }
    };
    w.onerror = (e) => {
      this.kState = 'error';
      this.kInfo = e.message || 'voice worker failed';
      this.worker = null;
      this.emit();
    };
    w.postMessage({ type: 'load', device: 'auto' });
  }

  // Queue a line; resolves when it has been spoken (or skipped).
  speak(text, hooks = {}) {
    const clean = cleanForSpeech(text);
    if (!clean) return Promise.resolve();
    return new Promise((resolve) => {
      if (this.queue.length > 2) this.queue.splice(0, this.queue.length - 2);   // don't fall behind
      this.queue.push({ text: clean, hooks, resolve });
      this.run();
    });
  }

  stop() {
    this.queue.forEach((q) => q.resolve());
    this.queue = [];
    this.current?.stop?.();
    speechSynthesis?.cancel?.();
  }

  async run() {
    if (this.busy) return;
    this.busy = true;
    while (this.queue.length) {
      const item = this.queue.shift();
      const eng = this.cfg.engine;
      try {
        if (eng === 'kokoro' && this.kState === 'ready') await this.sayKokoro(item);
        else if (eng === 'babble' || (eng === 'kokoro' && this.kState !== 'ready' && this.cfg.fallback === 'babble')) await this.sayBabble(item);
        else await this.saySystem(item);
      } catch (e) {
        console.warn('speak failed', e);
      }
      item.hooks.onLevel?.(0);
      item.hooks.onEnd?.();
      item.resolve();
    }
    this.busy = false;
  }

  // ---- Kokoro ------------------------------------------------------------------------
  sayKokoro(item) {
    const rate = 2 ** (this.cfg.pitch / 12);
    const speed = Math.max(0.5, Math.min(2, this.cfg.speed / rate));
    return new Promise((resolve) => {
      const id = this.nextId++;
      this.pending.set(id, (m) => {
        if (m.type !== 'audio') { this.saySystem(item).then(resolve); return; }
        this.playPcm(m.audio, m.rate, rate, item.hooks).then(resolve);
      });
      this.worker.postMessage({ type: 'speak', id, text: item.text, voice: this.cfg.voice || 'af_heart', speed });
    });
  }

  playPcm(pcm, sampleRate, playbackRate, hooks) {
    const c = audioCtx();
    const buf = c.createBuffer(1, pcm.length, sampleRate);
    buf.copyToChannel(pcm, 0);
    const src = c.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = playbackRate;
    const gain = c.createGain();
    gain.gain.value = this.cfg.volume ?? 1;
    const fxOut = buildEffect(c, this.cfg.fx, src);
    const an = c.createAnalyser();
    an.fftSize = 512;
    fxOut.connect(gain).connect(an).connect(output());
    return this.watch(src, an, buf.duration / playbackRate, hooks);
  }

  watch(src, analyser, seconds, hooks) {
    return new Promise((resolve) => {
      const data = new Float32Array(analyser.fftSize);
      let done = false;
      const tick = setInterval(() => {
        analyser.getFloatTimeDomainData(data);
        let s = 0;
        for (let i = 0; i < data.length; i++) s += data[i] * data[i];
        hooks.onLevel?.(Math.min(1, Math.sqrt(s / data.length) * 5));
      }, 70);
      const finish = () => {
        if (done) return;
        done = true;
        clearInterval(tick);
        this.current = null;
        resolve();
      };
      src.onended = finish;
      this.current = { stop: () => { try { src.stop(); } catch { /* already stopped */ } finish(); } };
      hooks.onStart?.();
      src.start();
      setTimeout(finish, seconds * 1000 + 1500);   // safety net
    });
  }

  // ---- Phone voice ----------------------------------------------------------------------
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
      let level = 0, pulse = null;
      const end = () => {
        clearInterval(pulse);
        this.current = null;
        resolve();
      };
      u.onstart = () => {
        item.hooks.onStart?.();
        pulse = setInterval(() => {
          level = 0.25 + Math.random() * 0.6;
          item.hooks.onLevel?.(level);
        }, 90);
      };
      u.onend = end;
      u.onerror = end;
      this.current = { stop: () => { speechSynthesis.cancel(); end(); } };
      speechSynthesis.speak(u);
      setTimeout(() => { if (this.current) end(); }, 4000 + item.text.length * 120);
    });
  }

  // ---- Babble: chirpy gibberish that follows the syllables ---------------------------------
  sayBabble(item) {
    const c = audioCtx();
    const base = 330 * 2 ** (this.cfg.pitch / 12);
    const speed = this.cfg.speed || 1;
    const syl = item.text.toLowerCase().match(/[bcdfghjklmnpqrstvwxyz]*[aeiouy]+|[.,!?]/g) || ['a'];
    const FORMANT = { a: 900, e: 1900, i: 2400, o: 650, u: 450, y: 2100 };
    let t = c.currentTime + 0.05;
    const gain = c.createGain();
    gain.gain.value = 0.9 * (this.cfg.volume ?? 1);
    const src = c.createGain();
    const fxOut = buildEffect(c, this.cfg.fx, src);
    const an = c.createAnalyser();
    an.fftSize = 512;
    fxOut.connect(gain).connect(an).connect(output());
    const question = item.text.trim().endsWith('?');
    syl.forEach((s, i) => {
      if (/[.,!?]/.test(s)) { t += 0.12 / speed; return; }
      const vowel = s.match(/[aeiouy]/)[0];
      const dur = (0.065 + Math.random() * 0.03) / speed;
      let f = base * 2 ** ((Math.random() * 5 - 2) / 12);
      if (question && i > syl.length - 3) f *= 1.25;
      const o = c.createOscillator(), bp = c.createBiquadFilter(), g = c.createGain();
      o.type = 'square';
      o.frequency.setValueAtTime(f, t);
      o.frequency.exponentialRampToValueAtTime(f * 1.06, t + dur);
      bp.type = 'bandpass';
      bp.frequency.value = FORMANT[vowel] || 900;
      bp.Q.value = 3;
      g.gain.setValueAtTime(0.0001, t);
      g.gain.exponentialRampToValueAtTime(0.5, t + 0.008);
      g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
      o.connect(bp).connect(g).connect(src);
      o.start(t);
      o.stop(t + dur + 0.02);
      t += dur + 0.018 / speed;
    });
    const seconds = t - c.currentTime;
    const fake = { onended: null, start() {}, stop() {} };
    setTimeout(() => fake.onended?.(), seconds * 1000);
    return this.watch(fake, an, seconds, item.hooks);
  }
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
