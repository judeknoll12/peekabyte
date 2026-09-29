// Little synthesized sound effects (no audio files needed).

let ac = null, master = null, purrNodes = null;
let volume = 0.6, enabled = true;

export function audioCtx() {
  if (!ac) {
    ac = new (window.AudioContext || window.webkitAudioContext)();
    master = ac.createGain();
    master.gain.value = volume;
    master.connect(ac.destination);
  }
  return ac;
}

export function output() {
  audioCtx();
  return master;
}

// iOS only lets pages make sound after a tap; call this from the first one.
export function unlock() {
  const c = audioCtx();
  if (c.state !== 'running') c.resume().catch(() => {});   // "suspended", or iOS's "interrupted"
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch { /* not supported */ }
  const b = c.createBuffer(1, 1, 22050), s = c.createBufferSource();
  s.buffer = b;
  s.connect(master);
  s.start(0);
}

export function setVolume(v) {
  volume = v;
  if (master) master.gain.value = v;
}
export function setEnabled(on) {
  enabled = on;
  if (!on) purr(false);
}

function tone({ f, to = f, dur = 0.15, type = 'sine', vol = 0.3, at = 0, attack = 0.006, vib = 0, curve = 'exp' }) {
  const c = audioCtx(), t = c.currentTime + at;
  const o = c.createOscillator(), g = c.createGain();
  o.type = type;
  o.frequency.setValueAtTime(f, t);
  if (to !== f) {
    if (curve === 'exp') o.frequency.exponentialRampToValueAtTime(Math.max(20, to), t + dur);
    else o.frequency.linearRampToValueAtTime(to, t + dur);
  }
  if (vib) {
    const l = c.createOscillator(), lg = c.createGain();
    l.frequency.value = 9;
    lg.gain.value = vib;
    l.connect(lg).connect(o.frequency);
    l.start(t);
    l.stop(t + dur + 0.05);
  }
  g.gain.setValueAtTime(0.0001, t);
  g.gain.exponentialRampToValueAtTime(vol, t + attack);
  g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
  o.connect(g).connect(master);
  o.start(t);
  o.stop(t + dur + 0.05);
}

function noise({ dur = 0.2, vol = 0.25, at = 0, type = 'bandpass', f = 1200, to = f, q = 1 }) {
  const c = audioCtx(), t = c.currentTime + at;
  const len = Math.max(1, Math.floor(c.sampleRate * dur));
  const buf = c.createBuffer(1, len, c.sampleRate), d = buf.getChannelData(0);
  for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
  const s = c.createBufferSource(), fl = c.createBiquadFilter(), g = c.createGain();
  s.buffer = buf;
  fl.type = type;
  fl.Q.value = q;
  fl.frequency.setValueAtTime(f, t);
  if (to !== f) fl.frequency.exponentialRampToValueAtTime(to, t + dur);
  g.gain.setValueAtTime(vol, t);
  g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
  s.connect(fl).connect(g).connect(master);
  s.start(t);
}

const N = (n) => 440 * 2 ** ((n - 69) / 12);   // MIDI note -> Hz
const PENTA = [72, 74, 76, 79, 81, 84];

const SOUNDS = {
  boing: () => tone({ f: 180, to: 520, dur: 0.12, vol: 0.25 }) || tone({ f: 520, to: 240, dur: 0.18, at: 0.12, vol: 0.2, vib: 25 }),
  whoosh: () => noise({ dur: 0.35, f: 400, to: 2600, q: 0.8, vol: 0.3 }),
  whee: () => tone({ f: 400, to: 1300, dur: 0.5, vib: 30, vol: 0.2 }),
  scream: () => tone({ f: 500, to: 900, dur: 0.6, type: 'triangle', vib: 60, vol: 0.2 }),
  love: () => { tone({ f: N(88), dur: 0.3, type: 'triangle', vol: 0.18 }); tone({ f: N(91), dur: 0.45, at: 0.12, type: 'triangle', vol: 0.18 }); },
  sparkle: () => [96, 100, 103, 108].forEach((n, i) => tone({ f: N(n), dur: 0.12, at: i * 0.05, vol: 0.1 })),
  note: () => { for (let i = 0; i < 3; i++) tone({ f: N(PENTA[(Math.random() * PENTA.length) | 0]), dur: 0.18, at: i * 0.19, type: 'triangle', vol: 0.2 }); },
  bonk: () => { tone({ f: 140, to: 60, dur: 0.18, type: 'square', vol: 0.18 }); noise({ dur: 0.05, f: 2000, vol: 0.2 }); },
  dizzy: () => tone({ f: 700, to: 250, dur: 0.9, vib: 80, vol: 0.15 }),
  tada: () => { [60, 64, 67].forEach((n, i) => tone({ f: N(n + 12), dur: 0.12, at: i * 0.08, type: 'square', vol: 0.08 })); [72, 76, 79].forEach((n) => tone({ f: N(n), dur: 0.5, at: 0.28, type: 'triangle', vol: 0.12 })); },
  wink: () => tone({ f: 1800, to: 2400, dur: 0.08, vol: 0.12 }),
  giggle: () => { for (let i = 0; i < 4; i++) tone({ f: 620 + i * 60, to: 900 + i * 60, dur: 0.07, at: i * 0.09, vol: 0.14 }); },
  tickle: () => SOUNDS.giggle(),
  no: () => { tone({ f: 330, dur: 0.12, type: 'square', vol: 0.08 }); tone({ f: 260, dur: 0.16, at: 0.16, type: 'square', vol: 0.08 }); },
  boop: () => tone({ f: 900, to: 620, dur: 0.12, vol: 0.2 }),
  yawn: () => tone({ f: 520, to: 190, dur: 1.1, vib: 8, type: 'triangle', vol: 0.12, curve: 'lin' }),
  burp: () => { tone({ f: 95, to: 70, dur: 0.45, type: 'square', vib: 12, vol: 0.12 }); noise({ dur: 0.4, f: 300, vol: 0.08 }); },
  chomp: () => noise({ dur: 0.07, type: 'lowpass', f: 1400, vol: 0.35 }),
  drop: () => tone({ f: 1100, to: 300, dur: 0.3, vol: 0.12 }),
  pill: () => tone({ f: 700, to: 500, dur: 0.1, vol: 0.12 }),
  gulp: () => tone({ f: 320, to: 140, dur: 0.14, vol: 0.2 }),
  yum: () => { tone({ f: N(76), dur: 0.12, type: 'triangle', vol: 0.16 }); tone({ f: N(81), dur: 0.22, at: 0.12, type: 'triangle', vol: 0.16 }); },
  yuck: () => { tone({ f: 150, dur: 0.35, type: 'sawtooth', vol: 0.07 }); tone({ f: 159, dur: 0.35, type: 'sawtooth', vol: 0.07 }); },
  catch: () => { tone({ f: 988, dur: 0.06, type: 'square', vol: 0.08 }); tone({ f: 1319, dur: 0.16, at: 0.06, type: 'square', vol: 0.08 }); },
  ouch: () => tone({ f: 300, to: 110, dur: 0.3, type: 'sawtooth', vol: 0.1 }),
  win: () => [72, 76, 79, 84].forEach((n, i) => tone({ f: N(n), dur: i === 3 ? 0.5 : 0.13, at: i * 0.12, type: 'square', vol: 0.08 })),
  lose: () => [67, 66, 65, 64].forEach((n, i) => tone({ f: N(n - 12), dur: i === 3 ? 0.6 : 0.22, at: i * 0.24, type: 'triangle', vol: 0.14, vib: i === 3 ? 6 : 0 })),
  levelup: () => { [60, 64, 67, 72, 76, 79, 84].forEach((n, i) => tone({ f: N(n + 12), dur: 0.1, at: i * 0.06, type: 'square', vol: 0.07 })); setTimeout(() => SOUNDS.sparkle(), 450); },
  hatch: () => { for (let i = 0; i < 4; i++) noise({ dur: 0.05, at: i * 0.18, f: 2500, vol: 0.3 }); },
  wobble: () => { tone({ f: 500, dur: 0.05, vol: 0.12 }); tone({ f: 420, dur: 0.05, at: 0.08, vol: 0.12 }); },
  sweep: () => noise({ dur: 0.5, f: 3000, to: 900, q: 0.6, vol: 0.2 }),
  start: () => [0, 1, 2].forEach((i) => tone({ f: i === 2 ? 1320 : 880, dur: 0.12, at: i * 0.35, type: 'square', vol: 0.07 })),
  yay: () => [72, 76, 79].forEach((n, i) => tone({ f: N(n), dur: 0.2, at: i * 0.07, type: 'triangle', vol: 0.14 })),
  nope: () => { tone({ f: N(64), dur: 0.15, type: 'triangle', vol: 0.14 }); tone({ f: N(60), dur: 0.25, at: 0.16, type: 'triangle', vol: 0.14 }); },
};

export function play(name) {
  if (!enabled) return;
  if (name === 'purr') return purr(true);
  if (name === 'purr_end') return purr(false);
  try { SOUNDS[name]?.(); } catch (e) { console.warn('sfx', name, e); }
}

// A soft continuous purr while you pet the pet.
export function purr(on) {
  const c = audioCtx();
  if (on && !purrNodes && enabled) {
    const o = c.createOscillator(), lfo = c.createOscillator(), lg = c.createGain(), g = c.createGain(), f = c.createBiquadFilter();
    o.type = 'sawtooth';
    o.frequency.value = 38;
    f.type = 'lowpass';
    f.frequency.value = 260;
    lfo.frequency.value = 22;
    lg.gain.value = 0.05;
    g.gain.value = 0;
    g.gain.linearRampToValueAtTime(0.07, c.currentTime + 0.3);
    lfo.connect(lg).connect(g.gain);
    o.connect(f).connect(g).connect(master);
    o.start();
    lfo.start();
    purrNodes = { o, lfo, g };
  } else if (!on && purrNodes) {
    const { o, lfo, g } = purrNodes;
    purrNodes = null;
    g.gain.cancelScheduledValues(c.currentTime);
    g.gain.setValueAtTime(g.gain.value, c.currentTime);
    g.gain.linearRampToValueAtTime(0, c.currentTime + 0.25);
    o.stop(c.currentTime + 0.3);
    lfo.stop(c.currentTime + 0.3);
  }
}
