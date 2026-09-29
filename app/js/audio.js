// Getting sound out of the phone. Inside the iPhone app, the app itself plays every sound the
// pet makes (ios/Peekabyte/Sound.swift): an app's own audio needs no tap to get going, plays
// with the ring switch on silent, and carries on after calls, Siri and the microphone, none of
// which web audio inside the app can count on. Browsers (and older app builds) use Web Audio.
//
// Sounds are made here as plain samples (OfflineAudioContext renders effects and synth sounds
// without playing anything) and handed to the app as 16-bit PCM.

import { call, isNative, on } from './native.js';

export const RATE = 24000;   // the natural voice's own rate; everything we hand the app uses it
export const Offline = window.OfflineAudioContext || window.webkitOfflineAudioContext;

// Can the app play sound itself? null while we're still asking (older builds say no).
export const native = { ok: isNative ? null : false, state: null };
export const nativeReady = isNative
  ? call('audio.state').then((s) => { native.ok = true; native.state = s; return true; },
    () => { native.ok = false; return false; })
  : Promise.resolve(false);

const live = new Map();   // sound id -> callbacks
let nextId = 1;
if (isNative) {
  on('audio.started', ({ id }) => live.get(id)?.started());
  on('audio.word', ({ id }) => live.get(id)?.word());
  on('audio.ended', ({ id }) => live.get(id)?.ended(''));
}

// Resolves with { ok, error } once the app says the sound has finished (or it clearly never will).
function follow(id, seconds, { onStart, onWord } = {}) {
  return new Promise((resolve) => {
    let started = false;
    const finish = (error) => {
      clearTimeout(safety);
      live.delete(id);
      resolve({ ok: !error, error });
    };
    const safety = setTimeout(() => finish(started ? '' : "the iPhone app didn't start playing it"), seconds * 1000 + 4000);
    live.set(id, {
      started: () => { started = true; onStart?.(); },
      word: () => onWord?.(),
      ended: finish,
    });
  });
}

// Play finished samples in the app. channel: 'voice' (one line at a time), 'sfx' (overlapping),
// or 'loop' (repeats until stopped).
export function playNative(pcm, rate, { channel = 'voice', volume = 1, loop = false, onStart } = {}) {
  const id = nextId++;
  const done = follow(id, loop ? 24 * 3600 : pcm.length / rate, { onStart });
  call('audio.play', { id, pcm: toB64(pcm), rate, volume, channel, loop })
    .catch((e) => live.get(id)?.ended(e?.message || "the iPhone app couldn't play it"));
  return done;
}

// Say text with one of the phone's own voices (played by the app).
export function sayNative(text, { voice = '', rate = 1, pitch = 1, volume = 1, onStart, onWord } = {}) {
  const id = nextId++;
  const done = follow(id, 3 + text.length * 0.15, { onStart, onWord });
  call('audio.say', { id, text, voice, rate, pitch, volume })
    .catch((e) => live.get(id)?.ended(e?.message || "the iPhone app couldn't speak"));
  return done;
}

export const stopNative = (channel) => call('audio.stop', { channel }).catch(() => {});

export async function nativeState() {
  try { native.state = await call('audio.state'); } catch { /* older app */ }
  return native.state;
}

export async function nativeVoices() {
  try { return (await call('audio.voices')).voices || []; } catch { return []; }
}

// ---- samples ----------------------------------------------------------------------------------
export function toB64(pcm) {
  const bytes = new Uint8Array(pcm.length * 2);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < pcm.length; i++) view.setInt16(i * 2, Math.max(-32768, Math.min(32767, Math.round(pcm[i] * 32767))), true);
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

// Loudness every `step` seconds, for moving the pet's mouth in time with the sound.
export function envelope(pcm, rate, step = 0.05) {
  const n = Math.max(1, Math.round(rate * step));
  const out = new Float32Array(Math.ceil(pcm.length / n));
  for (let k = 0; k < out.length; k++) {
    const a = k * n, b = Math.min(pcm.length, a + n);
    let s = 0;
    for (let i = a; i < b; i++) s += pcm[i] * pcm[i];
    out[k] = Math.sqrt(s / Math.max(1, b - a));
  }
  return out;
}

// Drop the silence at the end (synth sounds are rendered generously long).
export function trimEnd(pcm, floor = 0.0008) {
  let end = pcm.length;
  while (end > 0 && Math.abs(pcm[end - 1]) < floor) end--;
  return pcm.subarray(0, Math.min(pcm.length, end + 240));
}
