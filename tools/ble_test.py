"""Bluetooth smoke test: connects to the pet like the phone app does, checks the
messages and the screen mirror, and saves what the OLED shows as a PNG.

  python tools/ble_test.py [out.png] [--seconds 8] [--feed 3]

Needs bleak (pip install bleak) and a Bluetooth adapter.
"""
import argparse
import asyncio
import json
import struct
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

import usb_bridge  # frame decoder + PNG writer

SERVICE = "f3a10001-5b1e-4c6b-9e0f-7065656b6162"
RX = "f3a10002-5b1e-4c6b-9e0f-7065656b6162"
TX = "f3a10003-5b1e-4c6b-9e0f-7065656b6162"
F_FIRST, F_LAST = 0x80, 0x40


async def main(args):
    print("scanning...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE in [u.lower() for u in ad.service_uuids] or (d.name or "").startswith("Peeka"),
        timeout=15)
    if not dev:
        raise SystemExit("no Peekabyte found - is it powered and advertising?")
    print(f"found {dev.name} ({dev.address})")

    stats = {"msgs": 0, "frames": 0, "bytes": 0, "json": []}
    frame = bytearray(1024)
    buf = None
    got_frame = asyncio.Event()

    async with BleakClient(dev) as client:
        mtu = getattr(client, "mtu_size", 23)
        print(f"connected, MTU {mtu}")
        chunk = max(20, min(180, mtu - 4))

        async def send(msg: bytes):
            for pos in range(0, max(1, len(msg)), chunk):
                part = msg[pos:pos + chunk]
                head = (F_FIRST if pos == 0 else 0) | (F_LAST if pos + chunk >= len(msg) else 0)
                await client.write_gatt_char(RX, bytes([head]) + part, response=False)

        def on_notify(_, data: bytearray):
            nonlocal buf, frame
            stats["bytes"] += len(data)
            if data[0] & F_FIRST:
                buf = bytearray()
            if buf is None:
                return
            buf += data[1:]
            if data[0] & F_LAST:
                msg, buf = bytes(buf), None
                stats["msgs"] += 1
                if msg[:1] == b"{":
                    obj = json.loads(msg)
                    stats["json"].append(obj)
                    if obj.get("t") == "ev":
                        print("  event:", obj)
                elif msg[0] in (0x81, 0x82):
                    frame = usb_bridge.decode_frame(msg, frame)
                    stats["frames"] += 1
                    got_frame.set()

        await client.start_notify(TX, on_notify)
        epoch = int(time.time())
        tz = -time.timezone // 60 if not time.daylight else -time.altzone // 60
        await send(bytes([0x01]) + struct.pack("<Ih", epoch, tz) + b"\x00\x00")   # HELLO
        t0 = time.time()
        fed = False
        while time.time() - t0 < args.seconds:
            try:
                await asyncio.wait_for(got_frame.wait(), 1.5)
                got_frame.clear()
                await send(bytes([0x02, 0]))       # next frame please
            except asyncio.TimeoutError:
                await send(bytes([0x02, 1]))       # keyframe
            if not fed and args.feed is not None and time.time() - t0 > args.seconds / 3:
                fed = True
                print(f"feeding food #{args.feed}")
                await send(bytes([0x03, args.feed]))
        dt = time.time() - t0
        states = [j for j in stats["json"] if j.get("t") == "state"]
        print(f"{stats['msgs']} messages, {stats['frames']} frames ({stats['frames'] / dt:.1f} fps), "
              f"{stats['bytes'] / dt / 1024:.1f} KB/s over {dt:.1f}s")
        if states:
            s = states[-1]
            print(f"pet: {s['name']} stage {s['stage']} mood {s['mood']} needs {s['n']} mtu {s['mtu']} heap {s['heap']}")
        Path(args.out).write_bytes(usb_bridge.frame_to_png(frame))
        print("saved", args.out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="ble.png")
    ap.add_argument("--seconds", type=float, default=8)
    ap.add_argument("--feed", type=int, default=None)
    asyncio.run(main(ap.parse_args()))
