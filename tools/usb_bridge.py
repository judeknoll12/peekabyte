"""Peekabyte USB bridge - drive the pet from a computer over the USB cable.

  python tools/usb_bridge.py [--port COM5] [--http 8080]
      Serves the app/ folder at http://localhost:8080 and relays its messages to
      the ESP32 over serial, so the real app runs against the real pet without
      Bluetooth. Great for working on the app: just reload.

  python tools/usb_bridge.py snap out.png [--port COM5] [--wait 1.5] [--op HEX ...] [--peek]
      Sends the given protocol messages (hex, e.g. 0305 = feed food #5), waits,
      then saves what the OLED is showing as a PNG. --peek just looks, without
      connecting.

Needs pyserial (pip install pyserial).
"""
import argparse
import asyncio
import base64
import hashlib
import mimetypes
import struct
import sys
import threading
import time
import zlib
from pathlib import Path

import serial

try:   # pet log lines can contain bytes the Windows console can't print
    sys.stdout.reconfigure(errors="replace")
except AttributeError:
    pass

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
FAST_BAUD = 460800


class Link:
    """Serial link speaking the '@'/'#' line protocol of firmware/Peekabyte/comms.cpp."""

    def __init__(self, port):
        self.lock = threading.Lock()
        self.on_message = None
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False   # keep the auto-reset circuit idle
        self.ser.rts = False
        self.ser.open()
        self.ser.reset_input_buffer()
        # The board may still be at the fast rate from an earlier session; the
        # leading/trailing newlines flush any half-received garbage line.
        self.line(f"\n@@baud {FAST_BAUD}")
        time.sleep(0.1)
        self.ser.baudrate = FAST_BAUD
        self.line("")
        time.sleep(0.05)
        self.ser.reset_input_buffer()
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._keepalive, daemon=True).start()

    def _keepalive(self):
        # The pet forgets a bridge that stays silent for 15 s (and drops back to
        # 115200 baud), so keep saying hi while we're running.
        while True:
            time.sleep(5)
            try:
                self.line("@@ping")
            except (serial.SerialException, AttributeError, TypeError):
                return

    def line(self, text):
        with self.lock:
            self.ser.write((text + "\n").encode())

    def send(self, payload: bytes):
        self.line("@" + base64.b64encode(payload).decode())

    def _reader(self):
        buf = b""
        while True:
            try:
                chunk = self.ser.read(8192)
            except (serial.SerialException, TypeError, AttributeError) as e:
                if self.ser.is_open:   # a real error; a port closed on purpose just ends quietly
                    print("serial error:", e)
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                raw = raw.rstrip(b"\r")
                if raw.startswith(b"#J"):
                    msg = ("text", raw[2:].decode("utf-8", "replace"))
                elif raw.startswith(b"#B"):
                    try:
                        msg = ("binary", base64.b64decode(raw[2:], validate=True))
                    except Exception:
                        continue
                else:
                    if raw.strip():
                        print("[pet]", raw.decode("utf-8", "replace"))
                    continue
                if self.on_message:
                    self.on_message(msg)


# ------------------------------------------------------------------ frames ---
def decode_frame(data: bytes, prev: bytearray) -> bytearray:
    """Apply a mirror frame (0x80 raw, 0x81 keyframe, 0x82 delta) to prev."""
    kind = data[0]
    if kind == 0x80:
        return bytearray(data[1:1025])
    out = bytearray(1024) if kind == 0x81 else bytearray(prev)
    i, pos = 1, 0
    while i < len(data) and pos < 1024:
        t = data[i]
        i += 1
        if t < 0x80:
            pos += t + 1
        else:
            n = (t & 0x7F) + 1
            for k in range(n):
                if pos + k < 1024:
                    out[pos + k] ^= data[i + k]
            i += n
            pos += n
    return out


def frame_to_png(pages: bytes, scale=6, color=(234, 246, 255)):
    w, h = 128 * scale, 64 * scale
    rows = []
    for y in range(64):
        line = bytearray()
        for x in range(128):
            on = (pages[(y >> 3) * 128 + x] >> (y & 7)) & 1
            px = bytes(color) if on else b"\x06\x07\x0c"
            line += px * scale
        row = b"\x00" + bytes(line)
        rows.extend([row] * scale)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))


def peek(link, timeout=2.0):
    """Grab the current screen without connecting."""
    got = {}
    ev = threading.Event()

    def on_msg(msg):
        kind, data = msg
        if kind == "binary" and data[:1] == b"\x80":
            got["f"] = data
            ev.set()

    old = link.on_message
    link.on_message = on_msg
    link.line("@@peek")
    ev.wait(timeout)
    link.on_message = old
    return decode_frame(got["f"], bytearray(1024)) if "f" in got else None


def snap(args):
    link = Link(args.port)
    if not args.peek:
        link.line("@@hello")
        time.sleep(0.3)
        for op in args.op or []:
            link.send(bytes.fromhex(op))
            time.sleep(0.05)
        time.sleep(args.wait)
    frame = peek(link)
    if not args.peek:
        link.line("@@bye")
    if frame is None:
        sys.exit("no frame received")
    Path(args.out).write_bytes(frame_to_png(frame))
    print("saved", args.out)


# ------------------------------------------------------------ web server ---
class Server:
    def __init__(self, link):
        self.link = link
        self.clients = set()
        self.loop = None
        link.on_message = lambda m: self.loop.call_soon_threadsafe(self.fanout, m)

    def fanout(self, msg):
        kind, data = msg
        pkt = ws_frame(1, data.encode()) if kind == "text" else ws_frame(2, data)
        for w in list(self.clients):
            try:
                w.write(pkt)
            except Exception:
                self.clients.discard(w)

    async def handle(self, reader, writer):
        try:
            head = await reader.readuntil(b"\r\n\r\n")
        except Exception:
            writer.close()
            return
        lines = head.decode("latin-1").split("\r\n")
        try:
            method, path, _ = lines[0].split(" ", 2)
        except ValueError:
            writer.close()
            return
        headers = {}
        for l in lines[1:]:
            if ":" in l:
                k, v = l.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        if headers.get("upgrade", "").lower() == "websocket":
            await self.websocket(reader, writer, headers)
            return
        path = path.split("?")[0]
        if path.endswith("/"):
            path += "index.html"
        target = (APP / path.lstrip("/")).resolve()
        if APP.resolve() not in target.parents or not target.is_file():
            writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            await writer.drain()
            writer.close()
            return
        body = target.read_bytes()
        ctype = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        if target.suffix in (".js", ".mjs"):
            ctype = "text/javascript"
        writer.write(f"HTTP/1.1 200 OK\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n".encode() + body)
        await writer.drain()
        writer.close()

    async def websocket(self, reader, writer, headers):
        accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] + GUID).encode()).digest()).decode()
        writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
        await writer.drain()
        self.clients.add(writer)
        print(f"browser connected ({len(self.clients)})")
        self.link.line("@@hello")
        try:
            while True:
                b0, b1 = await reader.readexactly(2)
                op, n = b0 & 0x0F, b1 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", await reader.readexactly(2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", await reader.readexactly(8))[0]
                mask = await reader.readexactly(4) if b1 & 0x80 else b"\0\0\0\0"
                data = bytearray(await reader.readexactly(n))
                for i in range(n):
                    data[i] ^= mask[i & 3]
                if op == 8:
                    break
                if op == 9:
                    writer.write(ws_frame(10, bytes(data)))
                elif op in (1, 2):
                    self.link.send(bytes(data))
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self.clients.discard(writer)
            print(f"browser disconnected ({len(self.clients)})")
            if not self.clients:
                self.link.line("@@bye")
            writer.close()


def ws_frame(op, payload: bytes):
    n = len(payload)
    if n < 126:
        head = bytes([0x80 | op, n])
    elif n < 65536:
        head = bytes([0x80 | op, 126]) + struct.pack(">H", n)
    else:
        head = bytes([0x80 | op, 127]) + struct.pack(">Q", n)
    return head + payload


async def serve(args):
    server = Server(Link(args.port))
    server.loop = asyncio.get_running_loop()
    srv = await asyncio.start_server(server.handle, "127.0.0.1", args.http)
    print(f"Peekabyte USB bridge on http://localhost:{args.http}  (pet on {args.port})")
    async with srv:
        await srv.serve_forever()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", default="serve", choices=["serve", "snap"])
    ap.add_argument("out", nargs="?", default="oled.png")
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--http", type=int, default=8080)
    ap.add_argument("--wait", type=float, default=1.5)
    ap.add_argument("--op", action="append", help="hex-encoded message to send before the snapshot")
    ap.add_argument("--peek", action="store_true", help="snap: grab the current screen without connecting")
    args = ap.parse_args()
    if args.cmd == "snap":
        snap(args)
    else:
        asyncio.run(serve(args))


if __name__ == "__main__":
    main()
