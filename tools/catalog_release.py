#!/usr/bin/env python3
"""Mechanics of a Flipper Apps Catalog release. No dependencies, no SDK, no build.

  catalog_release.py status              what is still missing before submitting
  catalog_release.py intake SRC NAME     install one qFlipper capture as screenshots/NAME.png
  catalog_release.py pin [--tag vX.Y]    write the tag's full 40-char SHA into the manifest

The judgment calls — what the version should be, what the changelog says, whether a
screenshot shows the right thing — are not in here. This only does the parts that are
mechanical and easy to get subtly wrong: the 40-character SHA, the screenshot geometry,
the tag pointing at the commit you actually pinned.

Validation lives in catalog_preflight.py; every subcommand here ends by running it.
"""

import argparse
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "catalog" / "manifest.yml"
SCREENSHOTS = ROOT / "screenshots"
SCREEN = (128, 64)
DOWNSCALE_FACTORS = (4, 8)


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def git(*args, check=True):
    r = subprocess.run(
        ["git", "-C", str(ROOT), *args], capture_output=True, text=True
    )
    if check and r.returncode:
        die(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def png_size(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        die(f"{path} is not a PNG")
    return struct.unpack(">II", data[16:24])


def expected_screenshots():
    """The screenshot names the manifest commits us to, in manifest order."""
    return re.findall(r'^\s*-\s*"\./screenshots/([^"]+)"', MANIFEST.read_text(), re.M)


def fam_version():
    m = re.search(r'fap_version\s*=\s*"([^"]*)"', (ROOT / "application.fam").read_text())
    return m.group(1) if m else None


def preflight():
    print("\n--- preflight ---")
    sys.stdout.flush()  # the child writes to the fd directly; without this it lands first
    return subprocess.run(
        [sys.executable, str(ROOT / "tools" / "catalog_preflight.py")]
    ).returncode


def cmd_status(args):
    version = fam_version()
    print(f"fap_version:  {version}")
    print(f"HEAD:         {git('rev-parse', 'HEAD')}")

    tag = f"v{version}"
    if git("tag", "--list", tag):
        at = git("rev-list", "-n", "1", tag)
        head = git("rev-parse", "HEAD")
        print(f"tag {tag}:     {at} {'(= HEAD)' if at == head else '(NOT at HEAD)'}")
        remote = git("ls-remote", "--tags", "origin", tag, check=False)
        print(f"              {'pushed' if tag in remote else 'NOT pushed to origin'}")
    else:
        print(f"tag {tag}:     does not exist yet")

    sha = re.search(r"commit_sha:\s*(\S+)", MANIFEST.read_text())
    pinned = sha.group(1) if sha else "(absent)"
    print(f"commit_sha:   {pinned}")

    print("\nscreenshots the manifest requires:")
    for name in expected_screenshots():
        path = SCREENSHOTS / name
        if path.exists():
            w, h = png_size(path)
            print(f"  [x] {name:16} {w}x{h}")
        else:
            print(f"  [ ] {name:16} missing")

    dirty = git("status", "--porcelain")
    print(f"\nworking tree: {'dirty' if dirty else 'clean'}")
    if dirty:
        print("  " + "\n  ".join(dirty.splitlines()))
    return preflight()


def cmd_intake(args):
    src = Path(args.src).expanduser().resolve()
    if not src.exists():
        die(f"{src} not found")

    name = args.name if args.name.endswith(".png") else f"{args.name}.png"
    wanted = expected_screenshots()
    if name not in wanted:
        die(f"{name} is not a name the manifest expects: {', '.join(wanted)}")

    w, h = png_size(src)
    if w < h:
        die(
            f"{src.name} is portrait ({w}x{h}). The bundler rejects portrait outright — "
            "capture the Flipper's landscape framebuffer, do not rotate it yourself."
        )
    fx, fy = w // SCREEN[0], h // SCREEN[1]
    if fx != fy or fx not in DOWNSCALE_FACTORS or (w, h) != (SCREEN[0] * fx, SCREEN[1] * fy):
        ok = " or ".join(f"{SCREEN[0]*f}x{SCREEN[1]*f}" for f in DOWNSCALE_FACTORS)
        die(
            f"{src.name} is {w}x{h}; the bundler needs {ok}. Commit qFlipper's output "
            "unmodified — a raw 128x64 is rejected too."
        )

    SCREENSHOTS.mkdir(exist_ok=True)
    shutil.copy2(src, SCREENSHOTS / name)
    print(f"installed screenshots/{name} ({w}x{h}, {fx}x)")

    missing = [n for n in wanted if not (SCREENSHOTS / n).exists()]
    print(f"still missing: {', '.join(missing) if missing else 'nothing'}")
    return preflight()


def cmd_pin(args):
    tag = args.tag or f"v{fam_version()}"

    if git("status", "--porcelain"):
        die("working tree is dirty — commit first, so the pinned SHA is the whole app")

    if not git("tag", "--list", tag):
        die(f"tag {tag} does not exist. Create it on the commit you want published:\n"
            f"  git tag {tag} && git push origin {tag}")

    sha = git("rev-list", "-n", "1", tag)
    head = git("rev-parse", "HEAD")
    if sha != head and not args.allow_detached:
        die(f"tag {tag} is at {sha[:8]} but HEAD is {head[:8]}. Pin what you tagged, or "
            f"pass --allow-detached if you meant to submit an older commit.")

    if len(sha) != 40:
        die(f"refusing to pin {sha!r}: the bundler checks the length is exactly 40")

    text = MANIFEST.read_text()
    new = re.sub(r"(commit_sha:\s*)\S+", rf"\g<1>{sha}", text)
    if new == text:
        print(f"commit_sha already {sha}")
    else:
        MANIFEST.write_text(new)
        print(f"pinned commit_sha: {sha}")

    if tag not in git("ls-remote", "--tags", "origin", tag, check=False):
        print(f"\n::warning:: tag {tag} is not on origin. The catalog clones from GitHub, "
              f"so it must be pushed: git push origin {tag}")

    rc = preflight()
    print(
        f"\nNext: commit catalog/manifest.yml, then in a fork of "
        f"flipperdevices/flipper-application-catalog on branch georgegach/jokeri_"
        f"{fam_version()}, copy it to applications/Games/jokeri/manifest.yml.\n"
        f"The 'Catalog: real bundler' workflow validates it against the actual bundler "
        f"first — run that before opening the PR."
    )
    return rc


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="what is still missing before submitting")

    intake = sub.add_parser("intake", help="install one qFlipper capture")
    intake.add_argument("src", help="path to the PNG qFlipper wrote")
    intake.add_argument("name", help="manifest name, e.g. menu or menu.png")

    pin = sub.add_parser("pin", help="write the tag's full SHA into the manifest")
    pin.add_argument("--tag", help="default: v<fap_version>")
    pin.add_argument("--allow-detached", action="store_true",
                     help="allow pinning a tag that is not at HEAD")

    args = p.parse_args()
    return {"status": cmd_status, "intake": cmd_intake, "pin": cmd_pin}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
