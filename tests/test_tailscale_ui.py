"""Tailscale fork: web UI card and the disabled upstream update pull."""
import os
import pathlib
import re
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH = (ROOT / 'esp32-birdnet-mic/esp32-birdnet-mic.ino').read_text()
WEB = (ROOT / 'esp32-birdnet-mic/WebUI.cpp').read_text()
FORK_OTA_TEXT = 'This firmware is a fork with Tailscale. Install updates by file upload.'


def function(source, signature):
    start = source.index(signature + ' {')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class TailscaleUiTests(unittest.TestCase):
    def test_official_ota_pull_disabled(self):
        self.assertRegex(SKETCH, r'#ifdef BIRDNET_TS_FORK\s*\nconst char\* FW_OTA_ARTIFACT_STR = "";\s*\n#else\s*\n'
                                 r'const char\* FW_OTA_ARTIFACT_STR = XIAO_OTA_ARTIFACT;\s*\n#endif')
        install = function(WEB, 'static void httpOtaInstall()')
        self.assertRegex(install, r'#ifdef BIRDNET_TS_FORK\s*\n\s*sendOtaPage\(F\("' + re.escape(FORK_OTA_TEXT) +
                         r'"\), false, 403\);\s*\n\s*return;\s*\n\s*#endif')
        self.assertLess(install.index('BIRDNET_TS_FORK'), install.index('fetchLatestOtaVersion'))
        page = function(WEB, 'static void sendOtaPage(const String &message, bool ok, int httpCode)') \
            if 'static void sendOtaPage(const String &message, bool ok, int httpCode) {' in WEB else \
            function(WEB, 'static void sendOtaPage(const String &message = String(), bool ok = true, int httpCode = 200)')
        self.assertIn(FORK_OTA_TEXT, page)
        # File upload stays.
        self.assertIn('web.on("/ota/upload", HTTP_POST, httpOtaUploadDone, httpOtaUploadChunk);', WEB)

    def test_tailscale_card_in_browser(self):
        chromium = os.environ.get('CHROMIUM') or shutil.which('google-chrome') or shutil.which('chromium')
        node_path = os.environ.get('NODE_PATH', '')
        probe = subprocess.run(['node', '-e', "require('playwright-core')"], env={**os.environ, 'NODE_PATH': node_path},
                               capture_output=True)
        if not chromium or probe.returncode != 0:
            self.skipTest('playwright-core and a Chromium browser are required')
        subprocess.run(['node', str(ROOT / 'tests/tailscale_ui_browser_test.js')], cwd=ROOT, check=True, timeout=90,
                       env={**os.environ, 'CHROMIUM': chromium, 'NODE_PATH': node_path})


if __name__ == '__main__':
    unittest.main()
