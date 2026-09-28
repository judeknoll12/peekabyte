// Live copy of the pet's OLED, drawn as glowing pixels. Touching it pets the pet.

import { MSG_KEYFRAME, MSG_DELTA, MSG_RAW } from './protocol.js';

export const TINTS = {
  ice: { name: 'Ice white', top: '#e8f6ff', bottom: '#e8f6ff' },
  blue: { name: 'Blue', top: '#5ec8ff', bottom: '#5ec8ff' },
  duo: { name: 'Yellow + blue', top: '#ffd84a', bottom: '#5ec8ff' },
  mint: { name: 'Mint', top: '#6dffc2', bottom: '#6dffc2' },
  pink: { name: 'Bubblegum', top: '#ff8fd0', bottom: '#ff8fd0' },
};

export class Mirror {
  constructor(canvas, { onPet, onLook } = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.frame = new Uint8Array(1024);
    this.hasFrame = false;
    this.tint = TINTS.ice;
    this.dirty = true;
    this.onPet = onPet;
    this.onLook = onLook;
    this.lastMove = 0;
    this.down = false;
    this.resize();
    new ResizeObserver(() => this.resize()).observe(canvas);
    this.bindPointer();
    const loop = () => {
      if (this.dirty) this.draw();
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }

  setTint(key) {
    this.tint = TINTS[key] || TINTS.ice;
    this.dirty = true;
  }

  resize() {
    const r = this.canvas.getBoundingClientRect();
    const dpr = Math.min(3, window.devicePixelRatio || 1);
    const s = Math.max(2, Math.round((r.width * dpr) / 128));
    this.scale = s;
    this.canvas.width = 128 * s;
    this.canvas.height = 64 * s;
    this.dirty = true;
  }

  // Frames: 0x80 raw 1024 bytes, 0x81 keyframe, 0x82 delta (run-length coded, see comms.cpp).
  apply(msg) {
    const kind = msg[0];
    if (kind === MSG_RAW) {
      this.frame.set(msg.subarray(1, 1025));
    } else {
      if (kind === MSG_KEYFRAME) this.frame.fill(0);
      else if (kind !== MSG_DELTA || !this.hasFrame) return false;
      let i = 1, pos = 0;
      while (i < msg.length && pos < 1024) {
        const t = msg[i++];
        if (t < 0x80) pos += t + 1;
        else {
          const n = (t & 0x7f) + 1;
          for (let k = 0; k < n && pos + k < 1024; k++) this.frame[pos + k] ^= msg[i + k];
          i += n;
          pos += n;
        }
      }
    }
    this.hasFrame = true;
    this.dirty = true;
    return true;
  }

  pixel(x, y) { return (this.frame[(y >> 3) * 128 + x] >> (y & 7)) & 1; }

  draw() {
    this.dirty = false;
    const { ctx, scale: s } = this;
    ctx.fillStyle = '#05060b';
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    if (!this.hasFrame) return;
    const size = s;
    for (let band = 0; band < 2; band++) {
      ctx.fillStyle = band === 0 ? this.tint.top : this.tint.bottom;
      const y0 = band === 0 ? 0 : 16, y1 = band === 0 ? 16 : 64;
      for (let y = y0; y < y1; y++) {
        const page = (y >> 3) * 128, bit = 1 << (y & 7);
        let run = -1;
        for (let x = 0; x <= 128; x++) {
          const on = x < 128 && (this.frame[page + x] & bit);
          if (on && run < 0) run = x;
          if (!on && run >= 0) {
            if (size === s) ctx.fillRect(run * s, y * s, (x - run) * s, size);
            else for (let k = run; k < x; k++) ctx.fillRect(k * s, y * s, size, size);
            run = -1;
          }
        }
      }
    }
  }

  toOled(e) {
    const r = this.canvas.getBoundingClientRect();
    return { x: ((e.clientX - r.left) / r.width) * 128, y: ((e.clientY - r.top) / r.height) * 64 };
  }

  bindPointer() {
    const c = this.canvas;
    c.addEventListener('pointerdown', (e) => {
      this.down = true;
      c.setPointerCapture(e.pointerId);
      const p = this.toOled(e);
      this.onPet?.(0, p.x, p.y);
      e.preventDefault();
    });
    c.addEventListener('pointermove', (e) => {
      const now = performance.now();
      if (now - this.lastMove < 70) return;
      this.lastMove = now;
      const p = this.toOled(e);
      if (this.down) this.onPet?.(1, p.x, p.y);
      else if (e.pointerType === 'mouse') this.onLook?.(p.x, p.y);
    });
    const up = (e) => {
      if (!this.down) return;
      this.down = false;
      const p = this.toOled(e);
      this.onPet?.(2, p.x, p.y);
    };
    c.addEventListener('pointerup', up);
    c.addEventListener('pointercancel', up);
    c.addEventListener('pointerleave', (e) => {
      if (e.pointerType === 'mouse' && !this.down) this.onLook?.(null);
    });
  }
}
