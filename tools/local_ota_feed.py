#!/usr/bin/env python3
"""Restricted, temporary C6 OTA feed; never changes routing or starts an update."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OFFICIAL_HOST = "esp32mic.msmeteo.cz"


def version_pair(value):
    if not re.fullmatch(r"[0-9]+\.[0-9]+", value):
        raise ValueError("Version must have two numeric components")
    return tuple(map(int, value.split(".")))


def load_candidate(web_dir, installed_version):
    version = (web_dir / "ota-version.txt").read_text().strip()
    if version_pair(version) <= version_pair(installed_version):
        raise ValueError("Candidate must be newer than the installed firmware")
    if json.loads((web_dir / "manifest.json").read_text())["version"] != version:
        raise ValueError("Manifest/feed version mismatch")
    app = (web_dir / "firmware-app-c6.bin").read_bytes()
    if not 24 <= len(app) <= 1310720 or app[0] != 0xE9 or struct.unpack_from("<H", app, 12)[0] != 13:
        raise ValueError("Need a C6 app-only image that fits the original OTA slot")
    if (web_dir / f"firmware-app-c6-{version}.bin").read_bytes() != app:
        raise ValueError("Stable/versioned C6 images differ")
    # Freeze the verified bytes; a rebuild during the test cannot change them.
    payloads = {"/ota-version.txt": ("text/plain; charset=utf-8", (version + "\n").encode()),
                "/firmware-app-c6.bin": ("application/octet-stream", app)}
    payloads.update({"/updates" + path: value for path, value in list(payloads.items())})
    return version, hashlib.sha256(app).hexdigest(), payloads


def make_server(address, allowed_clients, payloads):
    allowed = {ipaddress.ip_address(value) for value in allowed_clients}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass  # Do not dump arbitrary request headers or URLs.

        def reply(self, head=False):
            if ipaddress.ip_address(self.client_address[0]) not in allowed:
                return self.reject(403)
            if self.headers.get("Host", "").split(":", 1)[0].lower() != OFFICIAL_HOST:
                return self.reject(421)
            if self.path not in payloads:
                return self.reject(404)
            mime, body = payloads[self.path]
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if not head:
                self.wfile.write(body)

        def reject(self, code):
            self.send_response(code)
            self.send_header("Content-Length", "0")
            self.end_headers()

        def do_GET(self):
            self.reply()

        def do_HEAD(self):
            self.reply(head=True)

        def do_POST(self):
            self.reject(405)

    return ThreadingHTTPServer(address, Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web-dir", type=Path, default=ROOT / "web-flasher")
    parser.add_argument("--installed-version", required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8089)
    parser.add_argument("--allow-client", action="append", default=[])
    args = parser.parse_args()
    if not ipaddress.ip_address(args.bind).is_loopback and not args.allow_client:
        parser.error("A LAN bind requires an explicit --allow-client address")
    version, digest, payloads = load_candidate(args.web_dir, args.installed_version)
    server = make_server((args.bind, args.port), args.allow_client or ["127.0.0.1"], payloads)
    print(json.dumps({"candidate": version, "sha256": digest, "listen": server.server_address,
                      "allowed_clients": args.allow_client or ["127.0.0.1"]}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
