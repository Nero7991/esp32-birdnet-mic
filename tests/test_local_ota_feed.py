"""Private feed uses production paths and cannot serve other boards or files."""
from http.client import HTTPConnection
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("local_feed", ROOT / "tools/local_ota_feed.py")
feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(feed)


class LocalOtaFeedTests(unittest.TestCase):
    def test_another_source_cannot_access_the_candidate(self):
        server = feed.make_server(("127.0.0.1", 0), ["192.0.2.80"],
                                  {"/ota-version.txt": ("text/plain", b"1.35\n")})
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            connection = HTTPConnection(*server.server_address, timeout=3)
            connection.request("GET", "/ota-version.txt", headers={
                "Host": feed.OFFICIAL_HOST, "X-Forwarded-For": "192.0.2.80"})
            response = connection.getresponse()
            self.assertEqual(response.status, 403)
            self.assertEqual(response.read(), b"")
            connection.close()
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=3)

    def test_candidate_is_newer_targeted_consistent_and_fits_old_slot(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            image = bytearray(100)
            image[0] = 0xE9
            struct.pack_into("<H", image, 12, 13)
            (path / "firmware-app-c6.bin").write_bytes(image)
            (path / "firmware-app-c6-1.35.bin").write_bytes(image)
            (path / "ota-version.txt").write_text("1.35\n")
            (path / "manifest.json").write_text(json.dumps({"version": "1.35"}))
            version, digest, payloads = feed.load_candidate(path, "1.34")
            self.assertEqual(version, "1.35")
            self.assertEqual(len(digest), 64)
            self.assertEqual(payloads["/firmware-app-c6.bin"][1], image)
            with self.assertRaises(ValueError):
                feed.load_candidate(path, "1.35")
            image[12] = 9  # Another ESP32 target.
            (path / "firmware-app-c6.bin").write_bytes(image)
            with self.assertRaises(ValueError):
                feed.load_candidate(path, "1.34")

    def test_paths_host_acl_lengths_content_types_and_read_only_methods(self):
        payloads = {"/ota-version.txt": ("text/plain; charset=utf-8", b"1.35\n"),
                    "/firmware-app-c6.bin": ("application/octet-stream", b"candidate bytes")}
        server = feed.make_server(("127.0.0.1", 0), ["127.0.0.1"], payloads)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            def request(path, method="GET", host=feed.OFFICIAL_HOST):
                connection = HTTPConnection(*server.server_address, timeout=3)
                connection.request(method, path, headers={"Host": host})
                response = connection.getresponse()
                result = response.status, dict(response.getheaders()), response.read()
                connection.close()
                return result
            for path, (mime, body) in payloads.items():
                code, headers, actual = request(path)
                self.assertEqual(code, 200)
                self.assertEqual(actual, body)
                self.assertEqual(headers["Content-Length"], str(len(body)))
                self.assertEqual(headers["Content-Type"], mime)
                self.assertEqual(request(path, "HEAD")[2], b"")
            self.assertEqual(request("/firmware-app-c3.bin")[0], 404)
            self.assertEqual(request("/../AGENTS.md")[0], 404)
            self.assertEqual(request("/ota-version.txt", host="other.example")[0], 421)
            self.assertEqual(request("/ota-version.txt", "POST")[0], 405)
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=3)
