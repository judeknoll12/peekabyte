// Runs an open-source language model in the browser with Transformers.js.
import { pipeline, TextStreamer } from 'https://cdn.jsdelivr.net/npm/@huggingface/transformers@4.3.0';

let gen = null;
let busy = false;

async function hasWebgpu() {
  try {
    if (!navigator.gpu) return false;
    return !!(await navigator.gpu.requestAdapter());
  } catch {
    return false;
  }
}

// Some GPUs (and software GPU fallbacks) quietly produce gibberish; catch that early.
function looksLikeWords(s) {
  const t = (s || '').trim();
  if (t.length < 2) return false;
  const good = (t.match(/[A-Za-z\s.,!?'-]/g) || []).length;
  return good / t.length > 0.7;
}

async function build(model, device, dtype) {
  return pipeline('text-generation', model, {
    device,
    dtype,
    progress_callback: (p) => self.postMessage({ type: 'progress', p }),
  });
}

async function selfTest(g) {
  const out = await g([{ role: 'user', content: 'Say hello in three words.' }], { max_new_tokens: 10, do_sample: false });
  return looksLikeWords(out[0].generated_text.at(-1).content);
}

self.onmessage = async (e) => {
  const m = e.data;
  if (m.type === 'load') {
    try {
      let device = null, dtype = null;
      if (m.dtypes.webgpu && (await hasWebgpu())) {
        try {
          gen = await build(m.model, 'webgpu', m.dtypes.webgpu);
          if (await selfTest(gen)) { device = 'webgpu'; dtype = m.dtypes.webgpu; }
          else { await gen.dispose?.(); gen = null; }
        } catch (err) {
          console.warn('WebGPU brain failed, trying the CPU', err);
          gen = null;
        }
      }
      if (!gen) {
        if (!m.dtypes.wasm) throw new Error('This brain needs a GPU (WebGPU) that works in this browser.');
        gen = await build(m.model, 'wasm', m.dtypes.wasm);
        device = 'wasm';
        dtype = m.dtypes.wasm;
      }
      self.postMessage({ type: 'ready', device, dtype });
    } catch (err) {
      gen = null;
      self.postMessage({ type: 'error', message: String(err?.message || err) });
    }
  } else if (m.type === 'generate') {
    if (!gen) return self.postMessage({ type: 'fail', id: m.id, message: 'brain not loaded' });
    if (busy) return self.postMessage({ type: 'fail', id: m.id, message: 'busy' });
    busy = true;
    try {
      const t0 = performance.now();
      const streamer = new TextStreamer(gen.tokenizer, {
        skip_prompt: true,
        skip_special_tokens: true,
        callback_function: (t) => self.postMessage({ type: 'token', id: m.id, text: t }),
      });
      const out = await gen(m.messages, {
        max_new_tokens: m.maxTokens || 32,
        do_sample: true,
        temperature: m.temperature ?? 0.8,
        top_p: 0.9,
        repetition_penalty: 1.15,
        streamer,
      });
      const text = out[0].generated_text.at(-1).content;
      self.postMessage({ type: 'done', id: m.id, text, ms: performance.now() - t0 });
    } catch (err) {
      self.postMessage({ type: 'fail', id: m.id, message: String(err?.message || err) });
    } finally {
      busy = false;
    }
  }
};
