"""Renders the Peekabyte artwork in design/ (SVG) into the PNGs the apps use, with Microsoft
Edge in headless mode (no image libraries needed).

    python tools/render_art.py            # everything
    python tools/render_art.py preview    # just design/preview-*.png, for a look
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN = ROOT / "design"
ICONSET = ROOT / "ios" / "Peekabyte" / "Assets.xcassets" / "AppIcon.appiconset"
LAUNCH = ROOT / "ios" / "Peekabyte" / "Assets.xcassets" / "LaunchLogo.imageset"
APP = ROOT / "app"

EDGE = next((p for p in [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
] if os.path.exists(p)), None)

# (source svg, output png, size in px, transparent background)
JOBS = [
    ("icon.svg", ICONSET / "icon-1024.png", 1024, False),
    ("mark.svg", LAUNCH / "launch-logo@2x.png", 280, True),
    ("mark.svg", LAUNCH / "launch-logo@3x.png", 420, True),
    ("icon.svg", APP / "icon.png", 512, False),
    ("icon.svg", APP / "icon-192.png", 192, False),
    ("icon.svg", APP / "apple-touch-icon.png", 180, False),
]


def render(svg: Path, out: Path, size: int, transparent: bool):
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        page = Path(tmp) / "page.html"
        shutil.copy(svg, Path(tmp) / "art.svg")
        page.write_text(
            "<!doctype html><html><head><style>html,body{margin:0;padding:0;overflow:hidden;"
            f"background:{'transparent' if transparent else '#fff'}}}img{{display:block;width:{size}px;height:{size}px}}"
            "</style></head><body><img src=\"art.svg\"></body></html>", encoding="utf-8")
        shot = Path(tmp) / "shot.png"
        args = [EDGE, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                f"--window-size={size},{size}", f"--screenshot={shot}", f"--user-data-dir={Path(tmp) / 'profile'}"]
        if transparent:
            args.append("--default-background-color=00000000")
        subprocess.run(args + [page.as_uri()], check=True, capture_output=True, timeout=60)
        shutil.copy(shot, out)
    print("wrote", out.relative_to(ROOT), size)


def main():
    if not EDGE:
        sys.exit("Microsoft Edge not found")
    jobs = JOBS
    if sys.argv[1:] == ["preview"]:
        jobs = [(src, DESIGN / f"preview-{Path(src).stem}.png", 512, tr) for src, _, _, tr in JOBS[:2]]
    for src, out, size, transparent in jobs:
        if (DESIGN / src).exists():
            render(DESIGN / src, out, size, transparent)


if __name__ == "__main__":
    main()
