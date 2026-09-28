// The pet's AI brain: a small open-source language model that runs entirely on the
// phone (no account, no server). It writes the pet's lines in character.

import { describe, emotionFor, line } from './phrases.js';

export const MODELS = [
  {
    key: 'smol', name: 'Pip', model: 'SmolLM2 360M', id: 'onnx-community/SmolLM2-360M-Instruct-ONNX',
    dtypes: { webgpu: 'q4', wasm: 'q8' }, mb: 360, license: 'Apache 2.0',
    blurb: 'Quick and light. The best pick for iPhone.',
  },
  {
    key: 'gemma', name: 'Gem', model: 'Gemma 3 270M', id: 'onnx-community/gemma-3-270m-it-ONNX',
    dtypes: { webgpu: 'q4', wasm: 'q4' }, mb: 310, license: 'Gemma terms',
    blurb: 'Tiny and chatty.',
  },
  {
    key: 'qwen', name: 'Sage', model: 'Qwen3 0.6B', id: 'onnx-community/Qwen3-0.6B-ONNX',
    dtypes: { webgpu: 'q4f16', wasm: 'q8' }, mb: 560, license: 'Apache 2.0', noThink: true,
    blurb: 'The smartest small brain. Needs a recent phone.',
  },
  {
    key: 'nemotron', name: 'Nemo', model: 'NVIDIA Nemotron 3 Nano 4B', id: 'onnx-community/NVIDIA-Nemotron-3-Nano-4B-BF16-ONNX',
    dtypes: { webgpu: 'q4f16', wasm: null }, mb: 2200, license: 'NVIDIA Open Model License', noThink: true, big: true,
    blurb: 'NVIDIA\'s open model. Needs a computer or iPad with a strong GPU - too big for phones.',
  },
];

const MOODS = ['happy', 'joy', 'sad', 'angry', 'surprised', 'scared', 'sleepy', 'love', 'stars', 'thinking', 'smug', 'silly', 'cry', 'wink'];

export class Brain extends EventTarget {
  constructor() {
    super();
    this.state = 'off';          // off | loading | ready | error
    this.progress = 0;
    this.info = '';
    this.model = null;
    this.worker = null;
    this.pending = new Map();
    this.nextId = 1;
    this.history = [];           // recent chat turns
    this.busy = false;
  }

  emit() {
    this.dispatchEvent(new CustomEvent('status', {
      detail: { state: this.state, progress: this.progress, info: this.info, model: this.model },
    }));
  }

  load(key) {
    const m = MODELS.find((x) => x.key === key);
    this.unload();
    if (!m) return;
    this.model = m;
    this.state = 'loading';
    this.progress = 0;
    this.info = '';
    this.emit();
    const w = new Worker(new URL('./llm-worker.js', import.meta.url), { type: 'module' });
    this.worker = w;
    const files = new Map();
    w.onmessage = (e) => {
      const d = e.data;
      if (d.type === 'progress') {
        const p = d.p;
        if (p.file && p.total) files.set(p.file, [p.loaded || 0, p.total]);
        let a = 0, b = 0;
        for (const [l, t] of files.values()) { a += l; b += t; }
        if (b) this.progress = a / b;
        this.emit();
      } else if (d.type === 'ready') {
        this.state = 'ready';
        this.info = `${d.device === 'webgpu' ? 'GPU' : 'CPU'} · ${d.dtype}`;
        this.emit();
      } else if (d.type === 'error') {
        this.state = 'error';
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
    this.worker?.terminate();
    this.worker = null;
    this.pending.forEach((cb) => cb({ type: 'fail', message: 'unloaded' }));
    this.pending.clear();
    this.state = 'off';
    this.model = null;
    this.emit();
  }

  get ready() { return this.state === 'ready'; }

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
  ask(messages, maxTokens = 32) {
    if (!this.ready || !this.worker) return Promise.resolve(null);
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
}

// Clean up whatever the model said into one tidy line plus an optional mood.
export function parse(raw) {
  let t = raw.replace(/<think>[\s\S]*?<\/think>/g, '').replace(/<\/?think>/g, '').trim();
  let emotion = null;
  const m = t.match(/^\s*\[([a-zA-Z]+)\]\s*/);
  if (m) {
    const e = m[1].toLowerCase();
    emotion = MOODS.includes(e) ? e : null;
    t = t.slice(m[0].length);
  }
  t = t.replace(/\[[^\]]*\]/g, '').replace(/^["'\s]+|["'\s]+$/g, '').replace(/\s+/g, ' ');
  const sentences = t.match(/[^.!?]+[.!?]*/g) || [t];
  t = sentences[0].trim();
  if (t.length < 40 && sentences[1]) t = `${t} ${sentences[1].trim()}`;   // keep a second one only if short
  if (!/[.!?]$/.test(t) && sentences.length === 1 && raw.length > 60) t += '…';
  if (t.length > 160) t = `${t.slice(0, 157).replace(/\s+\S*$/, '')}...`;
  if (!t) return null;
  return { text: t, emotion };
}

function fallbackChat(text, ctx) {
  const q = text.toLowerCase();
  if (/how are you|how do you feel|you ok/.test(q)) return `I feel ${ctx.mood}!`;
  if (/love you|like you/.test(q)) return 'I love you too!';
  if (/name/.test(q)) return `I'm ${ctx.name}!`;
  if (/hungry|food|eat/.test(q)) return `I love ${ctx.fav}. Just saying!`;
  if (/joke/.test(q)) return 'Why did the pixel blush? It saw the screen change!';
  const pick = ['Hehe, tell me more!', 'Ooh, really?', 'I like talking with you!', 'Blink blink!', 'You\'re my favorite human.'];
  return pick[Math.floor(Math.random() * pick.length)];
}

export { emotionFor };
