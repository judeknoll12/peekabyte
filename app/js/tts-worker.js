// Kokoro (open-source neural text-to-speech, Apache-2.0) running in a worker.
import { KokoroTTS } from 'https://cdn.jsdelivr.net/npm/kokoro-js@1.2.1/dist/kokoro.web.js';

const MODEL = 'onnx-community/Kokoro-82M-v1.0-ONNX';
let tts = null;

async function pickDevice() {
  try {
    if (navigator.gpu) {
      const a = await navigator.gpu.requestAdapter();
      if (a) return 'webgpu';
    }
  } catch { /* no WebGPU */ }
  return 'wasm';
}

self.onmessage = async (e) => {
  const m = e.data;
  if (m.type === 'load') {
    try {
      const device = m.device === 'auto' ? await pickDevice() : m.device;
      const dtype = device === 'webgpu' ? 'fp32' : 'q8';
      tts = await KokoroTTS.from_pretrained(MODEL, {
        dtype,
        device,
        progress_callback: (p) => self.postMessage({ type: 'progress', p }),
      });
      self.postMessage({ type: 'ready', device, dtype });
    } catch (err) {
      self.postMessage({ type: 'error', message: String(err?.message || err) });
    }
  } else if (m.type === 'speak') {
    if (!tts) return self.postMessage({ type: 'fail', id: m.id, message: 'voice not loaded' });
    try {
      const t0 = performance.now();
      const out = await tts.generate(m.text, { voice: m.voice, speed: m.speed });
      const audio = out.audio instanceof Float32Array ? out.audio : new Float32Array(out.audio);
      self.postMessage({ type: 'audio', id: m.id, audio, rate: out.sampling_rate, ms: performance.now() - t0 }, [audio.buffer]);
    } catch (err) {
      self.postMessage({ type: 'fail', id: m.id, message: String(err?.message || err) });
    }
  }
};
