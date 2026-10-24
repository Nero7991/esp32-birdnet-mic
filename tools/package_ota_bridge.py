#!/usr/bin/env python3
"""Keep the small v1.35 OTA bridge immutable after its release tag exists."""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]
BRIDGE_VERSION = "1.35"
BOARDS = ("c3", "s3", "c5", "c6")


def package(web, version, locked):
    refresh = version == BRIDGE_VERSION and not locked
    if refresh:
        boards = {}
        for board in BOARDS:
            app = (web / f"firmware-app-{board}.bin").read_bytes()
            limit = 1310720 if board in ("c3", "c6") else 3342336
            if not app or len(app) > limit:
                raise ValueError(f"{board} bridge does not fit its original OTA slot")
            (web / f"firmware-app-{board}-bridge.bin").write_bytes(app)
            boards[board] = {"size": len(app), "sha256": hashlib.sha256(app).hexdigest()}
        (web / "ota-bridge-version.txt").write_text(BRIDGE_VERSION + "\n")
        (web / "ota-bridge.json").write_text(json.dumps({"version": BRIDGE_VERSION, "boards": boards}, indent=2) + "\n")
    metadata = json.loads((web / "ota-bridge.json").read_text())
    if metadata["version"] != BRIDGE_VERSION or (web / "ota-bridge-version.txt").read_text().strip() != BRIDGE_VERSION:
        raise ValueError("Wrong permanent OTA bridge version")
    for board in BOARDS:
        app = (web / f"firmware-app-{board}-bridge.bin").read_bytes()
        limit = 1310720 if board in ("c3", "c6") else 3342336
        if not app or len(app) > limit or metadata["boards"][board] != {
                "size": len(app), "sha256": hashlib.sha256(app).hexdigest()}:
            raise ValueError(f"Missing or changed {board} OTA bridge")
    print("Verified permanent v1.35 bridge for all four boards")


if __name__ == "__main__":
    web = ROOT / "web-flasher"
    version = json.loads((web / "manifest.json").read_text())["version"]
    if shutil.which("git") and (ROOT / ".git").exists():
        locked = subprocess.run(["git", "-C", str(ROOT), "show-ref", "--verify", "--quiet", "refs/tags/v1.35"],
                                check=False).returncode == 0
    else:
        # Published source ZIPs already contain their verified bridge. Building
        # from a ZIP must neither require Git nor replace that frozen image.
        locked = (web / "ota-bridge.json").exists()
    package(web, version, locked)
