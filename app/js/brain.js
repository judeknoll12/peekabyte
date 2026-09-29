// The pet's AI brain: a small open-source language model that runs entirely on the
// phone (no account, no server). It writes the pet's lines in character.
//
// In a browser it runs with Transformers.js in a worker. In the iPhone app it runs natively
// on the GPU through llama.cpp (see native.js and ios/), which can fit a much bigger brain.

import { appInfo, call, isNative, on } from './native.js';
import { describe, emotionFor, line } from './phrases.js';

const hf = (repo, file) => `https://huggingface.co/${repo}/resolve/main/${file}`;

const WEB_MODELS = [
  {
    key: 'smol', name: 'Pip', model: 'SmolLM2 360M', id: 'onnx-community/SmolLM2-360M-Instruct-ONNX',
    dtypes: { webgpu: 'q4', wasm: 'q8' }, mb: 380, license: 'Apache 2.0',
    blurb: 'Quick and light. For big brains on iPhone, get the Peekabyte app.',
  },
  {
    key: 'gemma', name: 'Gem', model: 'Gemma 3 270M', id: 'onnx-community/gemma-3-270m-it-ONNX',
    dtypes: { webgpu: 'q4', wasm: 'q4' }, mb: 320, license: 'Gemma terms',
    blurb: 'Tiny and chatty.',
  },
  {
    key: 'qwen', name: 'Sage', model: 'Qwen3 0.6B', id: 'onnx-community/Qwen3-0.6B-ONNX',
    dtypes: { webgpu: 'q4f16', wasm: 'q8' }, mb: 600, license: 'Apache 2.0', noThink: true,
    blurb: 'The smartest small brain. Needs a computer or a recent phone with WebGPU.',
  },
  {
    key: 'nemotron', name: 'Nemo', model: 'NVIDIA Nemotron 3 Nano 4B', id: 'onnx-community/NVIDIA-Nemotron-3-Nano-4B-BF16-ONNX',
    dtypes: { webgpu: 'q4f16', wasm: null }, mb: 2300, license: 'NVIDIA Open Model License', noThink: true, big: true,
    blurb: 'NVIDIA\'s open model. Needs a computer with a strong GPU - too big for phones.',
  },
];

// GGUF files for llama.cpp in the iPhone app. Picked by playing the pet with each candidate.
const PHONE_MODELS = [
  {
    key: 'sage', name: 'Sage', model: 'Qwen3 1.7B', file: 'Qwen3-1.7B-Q4_K_M.gguf',
    url: hf('unsloth/Qwen3-1.7B-GGUF', 'Qwen3-1.7B-Q4_K_M.gguf'), mb: 1110, license: 'Apache 2.0', noThink: true, ramGB: 5.5,
    blurb: 'The smartest: real jokes, remembers the chat. Needs an iPhone with 6 GB of memory.',
  },
  {
    key: 'gem', name: 'Gem', model: 'Gemma 3 1B', file: 'gemma-3-1b-it-Q4_K_M.gguf',
    url: hf('unsloth/gemma-3-1b-it-GGUF', 'gemma-3-1b-it-Q4_K_M.gguf'), mb: 810, license: 'Gemma terms',
    blurb: 'Sweet and always in character. Runs on every iPhone.',
  },
  {
    key: 'pip', name: 'Pip', model: 'Qwen3 0.6B', file: 'Qwen3-0.6B-Q8_0.gguf',
    url: hf('Qwen/Qwen3-0.6B-GGUF', 'Qwen3-0.6B-Q8_0.gguf'), mb: 640, license: 'Apache 2.0', noThink: true,
    blurb: 'The quickest and lightest.',
  },
];

export const MODELS = isNative ? PHONE_MODELS : WEB_MODELS;
export const fmtSize = (mb) => (mb >= 1000 ? `${(mb / 1000).toFixed(1)} GB` : `${Math.round(mb)} MB`);

const MOODS = ['happy', 'joy', 'sad', 'angry', 'surprised', 'scared', 'sleepy', 'love', 'stars', 'thinking', 'smug', 'silly', 'cry', 'wink'];
const MOOD_ALIASES = {
  joyful: 'joy', excited: 'joy', glad: 'happy', cheerful: 'happy', playful: 'silly', lovely: 'love', loving: 'love',
  loveyou: 'love', mad: 'angry', grumpy: 'angry', afraid: 'scared', tired: 'sleepy', sleep: 'sleepy', curious: 'thinking',
  think: 'thinking', proud: 'smug', sassy: 'smug', crying: 'cry', upset: 'sad', wow: 'surprised', amazed: 'stars',
};

export class Brain extends EventTarget {
  constructor() {
    super();
    this.state = 'off';          // off | loading | ready | error
    this.stage = '';             // while loading: download | start
    this.progress = 0;
    this.bytes = null;           // [done, total] while downloading
    this.info = '';
    this.model = null;
    this.worker = null;
    this.pending = new Map();
    this.nextId = 1;
    this.history = [];           // recent chat turns
    this.files = new Set();      // brains already downloaded (iPhone app)
    this.memoryGB = 0;           // this iPhone's memory (iPhone app)
    this.napping = false;        // the iPhone app set the brain aside to free memory
    this.token = 0;
    if (isNative) {
      on('llm.token', (d) => this.dispatchEvent(new CustomEvent('token', { detail: d })));
      on('llm.unloaded', () => { if (this.state === 'ready') this.napping = true; });
      appInfo().then((i) => { if (i) { this.memoryGB = i.memory / 2 ** 30; this.emit(); } });
      this.refreshFiles();
    }
  }

  emit() {
    this.dispatchEvent(new CustomEvent('status', {
      detail: { state: this.state, progress: this.progress, info: this.info, model: this.model },
    }));
  }

  // The brain this phone runs best (iPhone app).
  recommended() {
    if (!isNative) return MODELS[0];
    return MODELS.find((m) => !m.ramGB || this.memoryGB >= m.ramGB) || MODELS[MODELS.length - 1];
  }

  async refreshFiles() {
    if (!isNative) return;
    try {
      const r = await call('llm.files');
      this.files = new Set(r.files.map((f) => f.file));
      this.freeBytes = r.free;
      this.emit();
    } catch { /* ignore */ }
  }

  load(key) {
    const m = MODELS.find((x) => x.key === key);
    this.unload();
    if (!m) return;
    this.model = m;
    this.state = 'loading';
    this.stage = '';
    this.progress = 0;
    this.bytes = null;
    this.info = '';
    this.emit();
    if (isNative) this.loadNative(m);
    else this.loadWeb(m);
  }

  // ---- iPhone app: llama.cpp on the GPU ----------------------------------------------------
  async loadNative(m) {
    const token = ++this.token;
    const stale = () => token !== this.token;
    try {
      await this.refreshFiles();
      if (!this.files.has(m.file)) {
        this.stage = 'download';
        this.emit();
        await this.download(m, stale);
        if (stale()) return;
        this.files.add(m.file);
      }
      this.stage = 'start';
      this.progress = 1;
      this.emit();
      const info = await call('llm.load', { file: m.file, ctx: 1024 });
      if (stale()) return;
      this.state = 'ready';
      this.stage = '';
      this.napping = false;
      this.info = `iPhone GPU · ${fmtSize(info.bytes / 1e6)}`;
      this.emit();
    } catch (e) {
      if (stale()) return;
      this.state = 'error';
      this.stage = '';
      this.info = String(e?.message || e);
      this.emit();
    }
  }

  download(m, stale) {
    return new Promise((resolve, reject) => {
      const offs = [];
      const done = (err) => { offs.forEach((off) => off()); if (err) reject(err); else resolve(); };
      offs.push(on('llm.progress', (d) => {
        if (d.file !== m.file || stale()) return;
        this.retrying = !!d.retrying;
        if (d.total) {
          this.progress = d.loaded / d.total;
          this.bytes = [d.loaded, d.total];
        }
        this.emit();
      }));
      offs.push(on('llm.downloaded', (d) => { if (d.file === m.file) done(); }));
      offs.push(on('llm.failed', (d) => { if (d.file === m.file) done(new Error(d.error)); }));
      call('llm.download', { url: m.url, file: m.file, bytes: m.mb * 1e6 }).catch(done);
    });
  }

  cancelDownload() {
    if (!isNative || !this.model) return;
    call('llm.cancel', { file: this.model.file }).catch(() => {});
  }

  async deleteFile(m) {
    if (!isNative) return;
    if (this.model?.key === m.key) this.unload();
    await call('llm.delete', { file: m.file }).catch(() => {});
    await this.refreshFiles();
  }

  // ---- browser: Transformers.js in a worker -----------------------------------------------
  loadWeb(m) {
    const w = new Worker(new URL('./llm-worker.js', import.meta.url), { type: 'module' });
    this.worker = w;
    this.stage = 'download';
    const files = new Map();
    w.onmessage = (e) => {
      const d = e.data;
      if (d.type === 'progress') {
        const p = d.p;
        if (p.file && p.total) files.set(p.file, [p.loaded || 0, p.total]);
        let a = 0, b = 0;
        for (const [l, t] of files.values()) { a += l; b += t; }
        if (b) {
          this.progress = a / b;
          this.bytes = [a, b];
          if (this.progress >= 0.999) this.stage = 'start';   // downloaded; now it's warming up
        }
        this.emit();
      } else if (d.type === 'ready') {
        this.state = 'ready';
        this.stage = '';
        this.info = `${d.device === 'webgpu' ? 'GPU' : 'CPU'} · ${d.dtype}`;
        this.emit();
      } else if (d.type === 'error') {
        this.state = 'error';
        this.stage = '';
        this.info = d.message;
        this.worker?.terminate();
        this.worker = null;
        this.emit();
      } else if (d.type === 'token') {
        this.dispatchEvent(new CustomEvent('token', { detail: d }));
      } else if (d.type === 'done' || d.type === 'fail') {
        const cb = this.pending.get(d.id);
        this.pending.delete(d.id);
        cb?.(d);
      }
    };
    w.onerror = (e) => {
      this.state = 'error';
      this.info = e.message || 'brain worker failed';
      this.worker = null;
      this.emit();
    };
    w.postMessage({ type: 'load', model: m.id, dtypes: m.dtypes });
  }

  unload() {
    this.token++;
    this.worker?.terminate();
    this.worker = null;
    this.pending.forEach((cb) => cb({ type: 'fail', message: 'unloaded' }));
    this.pending.clear();
    if (isNative && this.state !== 'off') call('llm.unload').catch(() => {});
    this.state = 'off';
    this.stage = '';
    this.model = null;
    this.emit();
  }

  get ready() { return this.state === 'ready'; }

  // What to show while loading.
  get loadingText() {
    if (this.stage === 'start') return `Waking ${this.model?.name || 'the brain'} up…`;
    if (this.retrying) return 'Connection hiccup, picking the download back up…';
    if (this.bytes) return `Downloading: ${fmtSize(this.bytes[0] / 1e6)} of ${fmtSize(this.bytes[1] / 1e6)} · only once`;
    return 'Getting ready…';
  }

  persona(ctx) {
    const stageStyle = {
      1: 'You are a baby: use very short, simple baby talk (2-6 words), cute sounds like "goo" allowed.',
      2: 'You are a kid: short, playful, curious and silly.',
      3: 'You are a teen: playful with a dash of sass.',
      4: 'You are grown up: warm, witty and sweet.',
    }[ctx.stageNum] || 'Keep it short and playful.';
    const sass = ctx.traits.includes('Sassy') ? ' You are a bit dramatic and cheeky.' : '';
    return [
      `You are ${ctx.name}, a tiny digital pet made of two big expressive eyes that lives on a small glowing screen and talks through your owner's phone.`,
      `Your owner is ${ctx.owner}. Personality: ${ctx.traits.join(' and ')}.${sass}`,
      `You love ${ctx.fav} and hate ${ctx.hate}. You are wearing: ${ctx.wearing}.`,
      `Right now you feel ${ctx.mood}. Hunger ${ctx.needs.food}/100, energy ${ctx.needs.energy}/100, fun ${ctx.needs.fun}/100, love ${ctx.needs.love}/100${ctx.sick ? ', and you are sick' : ''}.`,
      ctx.tricks.length ? `Tricks you have mastered: ${ctx.tricks.join(', ')}.` : 'You haven\'t mastered any tricks yet.',
      stageStyle,
      'Reply as the pet in ONE short sentence of at most 15 words. No emojis, no lists, no narration, never mention being an AI.',
      `Start your reply with one mood tag in brackets from: ${MOODS.map((m) => `[${m}]`).join(' ')}.`,
      this.model?.noThink ? '/no_think' : '',
    ].filter(Boolean).join('\n');
  }

  // Ask the model; resolves to { text, emotion } or null if it failed.
  async ask(messages, maxTokens = 32) {
    if (!this.ready) return null;
    if (isNative) {
      try {
        if (this.napping) {   // put aside to free memory: wake it up first
          await call('llm.load', { file: this.model.file, ctx: 1024 });
          this.napping = false;
        }
        const stop = setTimeout(() => call('llm.stop').catch(() => {}), 30000);
        const r = await call('llm.generate', {
          gen: this.nextId++, messages, maxTokens: maxTokens + 6, temperature: 0.8, topP: 0.9, repeatPenalty: 1.1,
        });
        clearTimeout(stop);
        return parse(r.text);
      } catch (e) {
        console.warn('brain failed', e);
        return null;
      }
    }
    if (!this.worker) return null;
    return new Promise((resolve) => {
      const id = this.nextId++;
      this.pending.set(id, (d) => resolve(d.type === 'done' ? parse(d.text) : null));
      this.worker.postMessage({ type: 'generate', id, messages, maxTokens, temperature: 0.85 });
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          resolve(null);
        }
      }, 45000);
    });
  }

  async react(intent, ctx) {
    const res = await this.ask([
      { role: 'system', content: this.persona(ctx) },
      { role: 'user', content: `(What just happened: ${describe(intent, ctx)}.) Say one short line.` },
    ]);
    return res || line(intent, ctx);
  }

  async chat(text, ctx) {
    this.history.push({ role: 'user', content: text });
    this.history = this.history.slice(-6);
    const res = await this.ask([{ role: 'system', content: this.persona(ctx) }, ...this.history], 40);
    const out = res || { text: fallbackChat(text, ctx), emotion: 'happy' };
    this.history.push({ role: 'assistant', content: `[${out.emotion || 'happy'}] ${out.text}` });
    return out;
  }

  // The pet starts a conversation by itself. Resolves to { text, emotion } or null.
  async starter(idea, ctx) {
    const res = await this.ask([
      { role: 'system', content: this.persona(ctx) },
      { role: 'user', content: `(Nobody has said anything for a while and you feel like chatting: ${idea}.) Say one short line to ${ctx.owner}.` },
    ], 36);
    if (res) this.remember(res.text, res.emotion);
    return res;
  }

  // Something the pet said on its own, so the AI knows what a reply refers to.
  remember(text, emotion) {
    this.history.push({ role: 'assistant', content: `[${emotion || 'happy'}] ${text}` });
    this.history = this.history.slice(-6);
  }
}

// Clean up whatever the model said into one tidy line plus an optional mood.
export function parse(raw) {
  let t = String(raw || '').replace(/<think>[\s\S]*?<\/think>/g, '').replace(/<\/?think>/g, '');
  t = t.replace(/[\p{Extended_Pictographic}\u{1F1E6}-\u{1F1FF}‍️]/gu, '').trim();   // the pet's screen can't show emoji
  let emotion = null;
  const m = t.match(/^\s*\[([a-zA-Z ]+)\]\s*/);
  if (m) {
    const e = m[1].toLowerCase().replace(/\s+/g, '');
    emotion = MOODS.includes(e) ? e : MOOD_ALIASES[e] || null;
    t = t.slice(m[0].length);
  }
  t = t.replace(/\[[^\]]*\]/g, '').replace(/^["'\s]+|["'\s]+$/g, '').replace(/\s+/g, ' ').replace(/ ([!?.,])/g, '$1');
  const sentences = t.match(/[^.!?]+[.!?]*/g) || [t];
  t = sentences[0].trim();
  if (t.length < 40 && sentences[1]) t = `${t} ${sentences[1].trim()}`;   // keep a second one only if short
  if (!/[.!?]$/.test(t) && sentences.length === 1 && raw.length > 60) t += '…';
  if (t.length > 160) t = `${t.slice(0, 157).replace(/\s+\S*$/, '')}...`;
  if (!t || !/[a-z]/i.test(t)) return null;
  return { text: t, emotion };
}

function fallbackChat(text, ctx) {
  const q = text.toLowerCase();
  if (/how are you|how do you feel|you ok/.test(q)) return `I'm ${ctx.mood}! Thanks for asking.`;
  if (/love you|like you/.test(q)) return 'I love you too!';
  if (/name/.test(q)) return `I'm ${ctx.name}!`;
  if (/hungry|food|eat/.test(q)) return `I love ${ctx.fav}. Just saying!`;
  if (/joke/.test(q)) return 'Why did the pixel blush? It saw the screen change!';
  const pick = ['Hehe, tell me more!', 'Ooh, really?', 'I like talking with you!', 'Blink blink!', 'You\'re my favorite human.'];
  return pick[Math.floor(Math.random() * pick.length)];
}

export { emotionFor };
