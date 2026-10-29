"""Tailscale settings (fork build): key validation, status JSON and key secrecy."""
import json
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH_DIR = ROOT / 'esp32-birdnet-mic'
STUBS = ROOT / 'tests' / 'wifi_ap_stubs'


def function(source, signature):
    start = source.index(signature + ' {')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class TailscaleConfigTests(unittest.TestCase):
    def run_native(self, body):
        code = '#include "Arduino.h"\n#include "TailscaleConfig.h"\n#include <cassert>\n#include <iostream>\n' \
               'unsigned long testNow = 0;\nint main() {\n' + body + '\nreturn 0;\n}\n'
        with tempfile.TemporaryDirectory() as temp:
            cpp, binary = pathlib.Path(temp) / 't.cpp', pathlib.Path(temp) / 't'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(STUBS),
                            '-I', str(SKETCH_DIR), str(cpp), '-o', str(binary)], check=True)
            return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout

    def test_key_validation(self):
        self.run_native(r'''
assert(tailscaleKeyLooksValid("tskey-auth-kAbC123CNTRL-0123456789abcdef"));
assert(!tailscaleKeyLooksValid(""));
assert(!tailscaleKeyLooksValid("tskey-api-kAbC123CNTRL-0123456789abcdef"));
assert(!tailscaleKeyLooksValid("tskey-auth-kAbC123 CNTRL-0123456789abcdef"));
assert(!tailscaleKeyLooksValid("tskey-auth-kAbC123CNTRL-0123456789abcdef\n"));
assert(!tailscaleKeyLooksValid("tskey-auth-"));
std::string long_key = "tskey-auth-" + std::string(190, 'a');
assert(!tailscaleKeyLooksValid(long_key.c_str()));
std::string max_key = "tskey-auth-" + std::string(189, 'a');
assert(tailscaleKeyLooksValid(max_key.c_str()));
''')

    def test_state_names(self):
        out = self.run_native(r'''
for (int s : {0, 1, 2, 3, 4, 5, 6, TS_STATE_NO_PSRAM, TS_STATE_NO_KEY, 42})
    std::cout << tailscaleStateName(s) << "\n";
''')
        self.assertEqual(out.split(), ['off', 'waiting_wifi', 'connecting', 'registering', 'connected',
                                       'reconnecting', 'error', 'no_psram', 'no_key', 'error'])

    def test_status_json_shape(self):
        out = self.run_native(r'''
TailscaleStatus s;
s.available = true; s.hasKey = true; s.state = "connected";
s.ip = "100.64.1.2"; s.hostname = "esp32mic-\"a84c84"; s.peerPath = "direct"; s.lastError = "";
std::cout << tailscaleStatusJson(s);
''')
        data = json.loads(out)
        self.assertEqual(set(data), {'available', 'has_key', 'state', 'ip', 'hostname', 'peer_path', 'last_error'})
        self.assertIs(data['has_key'], True)
        self.assertEqual(data['hostname'], 'esp32mic-"a84c84')
        self.assertEqual(data['state'], 'connected')

    def test_tailscale_key_never_exposed(self):
        link = (SKETCH_DIR / 'TailscaleLink.cpp').read_text()
        web = (SKETCH_DIR / 'WebUI.cpp').read_text()
        sketch = (SKETCH_DIR / 'esp32-birdnet-mic.ino').read_text()
        sinks = re.compile(r'simplePrintln|Serial\.print|webui_pushLog|publish\(|printf|ESP_LOG|log_[ewidv]\(|send\(|sendContent|\+')
        for name, text in (('TailscaleLink.cpp', link), ('WebUI.cpp', web), ('ino', sketch)):
            for line in text.splitlines():
                if 'tsAuthKey' in line or 'auth_key' in line:
                    if 'web.arg("auth_key")' in line or 'web.hasArg("auth_key")' in line:
                        continue
                    self.assertIsNone(sinks.search(line), f'{name}: key may reach an output: {line.strip()}')
        handler = function(web, 'static void httpTailscaleSet()')
        self.assertIn('web.arg("auth_key")', handler)
        for line in handler.splitlines():
            if 'apiSendJSON' in line or 'web.send' in line:
                self.assertNotIn('key', line.replace('has_key', '').replace('invalid_key', ''), line)
        # The key is read into a local and handed to the store only.
        self.assertRegex(handler, r'String\s+(\w+)\s*=\s*web\.arg\("auth_key"\)')
        var = re.search(r'String\s+(\w+)\s*=\s*web\.arg\("auth_key"\)', handler).group(1)
        code_lines = [re.sub(r'"(?:[^"\\]|\\.)*"', '""', l) for l in handler.splitlines()]
        uses = [l.strip() for l in code_lines if re.search(r'\b' + var + r'\b', l)]
        self.assertTrue(uses[0].startswith('String ' + var), uses[0])
        for use in uses[1:]:
            self.assertRegex(use, r'tailscaleKeyLooksValid\(' + var + r'\.c_str\(\)\)|tailscaleSaveKey\(' + var +
                             r'\)|' + var + r'\.length\(\)|' + var + r'\.trim\(\)', use)

    def test_routes_and_status_are_fork_only(self):
        web = (SKETCH_DIR / 'WebUI.cpp').read_text()
        sketch = (SKETCH_DIR / 'esp32-birdnet-mic.ino').read_text()
        self.assertRegex(web, r'#ifdef BIRDNET_TS_FORK\s*\n\s*web\.on\("/api/tailscale", HTTP_GET, httpTailscaleGet\);\s*\n'
                              r'\s*web\.on\("/api/tailscale", HTTP_POST, httpTailscaleSet\);\s*\n\s*#endif')
        status = function(web, 'static void httpStatus()')
        self.assertRegex(status, r'#ifdef BIRDNET_TS_FORK\s*\n\s*json \+= "\\"tailscale\\":" \+ tailscaleStatusJson\(tailscaleStatus\(\)\) \+ ",";\s*\n\s*#endif')
        # Tailscale state is not published over MQTT.
        self.assertNotIn('tailscale', function(sketch, 'static String mqttBuildStateJson()'))
        link = (SKETCH_DIR / 'TailscaleLink.cpp').read_text()
        self.assertTrue(link.lstrip().startswith('#ifdef BIRDNET_TS_FORK') or '\n#ifdef BIRDNET_TS_FORK' in link[:400])
        self.assertIn('"tsnet"', link)


if __name__ == '__main__':
    unittest.main()
