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


def c_function(source, signature):
    # C sources contain '{' in character literals, so end at the closing brace in column 0.
    start = source.index(signature + ' {')
    return source[start:source.index('\n}\n', start)]


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

    def test_problem_text_and_peer_path(self):
        out = self.run_native(r'''
std::cout << "[" << tailscaleProblem(4, 999999) << "]\n";
std::cout << "[" << tailscaleProblem(5, 10000) << "]\n";
std::cout << "[" << tailscaleProblem(5, 31000) << "]\n";
std::cout << "[" << tailscaleProblem(1, 31000) << "]\n";
std::cout << "[" << tailscaleProblem(6, 0) << "]\n";
std::cout << "[" << tailscalePeerPath(5, "derp") << "]\n";
std::cout << "[" << tailscalePeerPath(4, "direct") << "]\n";
''')
        self.assertEqual(out.splitlines(), [
            '[]', '[]',
            '[Cannot reach Tailscale. Check that this network has internet access.]',
            '[Cannot reach Tailscale. Check that this network has internet access.]',
            '[The Tailscale control server rejected this device. Check the auth key and that the node was not removed.]',
            '[]', '[direct]'])

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


MICROLINK_SRC = ROOT / 'idf/libs/microlink/components/microlink/src'
CJSON = pathlib.Path.home() / 'esp/v5.5/esp-idf/components/json/cJSON'


@unittest.skipUnless((CJSON / 'cJSON.c').exists(), 'ESP-IDF cJSON not found')
class ControlRejectTests(unittest.TestCase):
    """A revoked key or a removed node must surface as a rejection, not as a network fault."""

    def run_c(self, body):
        code = '#include "ml_ctrl_reject.h"\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n' \
               'int main(void) {\n' + body + '\nreturn 0;\n}\n'
        with tempfile.TemporaryDirectory() as temp:
            c, binary = pathlib.Path(temp) / 't.c', pathlib.Path(temp) / 't'
            c.write_text(code)
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(MICROLINK_SRC), '-I', str(CJSON),
                            str(c), str(MICROLINK_SRC / 'ml_ctrl_reject.c'), str(CJSON / 'cJSON.c'), '-o', str(binary)],
                           check=True)
            return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout

    def test_register_error_field(self):
        out = self.run_c(r'''
char why[48];
cJSON *r = cJSON_Parse("{\"User\":{\"ID\":0},\"Error\":\"machine not authorized by tailnet policy\"}");
assert(ml_ctrl_register_error(r, why, sizeof(why)));
printf("%s\n", why);
cJSON_Delete(r);
r = cJSON_Parse("{\"User\":{\"ID\":601777},\"MachineAuthorized\":true,\"Error\":\"\"}");
assert(!ml_ctrl_register_error(r, why, sizeof(why)));
assert(why[0] == 0);
cJSON_Delete(r);
r = cJSON_Parse("{\"User\":{\"ID\":601777}}");
assert(!ml_ctrl_register_error(r, why, sizeof(why)));
cJSON_Delete(r);
assert(!ml_ctrl_register_error(NULL, why, sizeof(why)));
char tiny[8];
r = cJSON_Parse("{\"Error\":\"invalid key: unable to validate API key\"}");
assert(ml_ctrl_register_error(r, tiny, sizeof(tiny)));
assert(strlen(tiny) == 7);
cJSON_Delete(r);
''')
        self.assertEqual(out, 'machine not authorized by tailnet policy\n')

    def test_register_error_never_echoes_a_key(self):
        out = self.run_c(r'''
char why[96];
cJSON *r = cJSON_Parse("{\"Error\":\"bad key tskey-auth-kAbCdEfGhIjKlMnOp\"}");
assert(ml_ctrl_register_error(r, why, sizeof(why)));
printf("%s\n", why);
cJSON_Delete(r);
''')
        self.assertNotIn('tskey-', out)
        self.assertNotIn('kAbCdEf', out)
        self.assertTrue(out.strip())

    def test_register_error_drops_the_key_id(self):
        # The control server echoes the key ID: "invalid key: API key <id> not valid".
        out = self.run_c(r'''
char why[96];
cJSON *r = cJSON_Parse("{\"Error\":\"invalid key: API key kAbCdEf123CNTRL not valid\"}");
assert(ml_ctrl_register_error(r, why, sizeof(why)));
printf("%s\n", why);
cJSON_Delete(r);
''')
        self.assertEqual(out, 'invalid key\n')

    def test_node_not_found_body(self):
        self.run_c(r'''
const char *nf = "node not found\n";
assert(ml_ctrl_body_node_not_found((const uint8_t *)nf, strlen(nf)));
assert(ml_ctrl_body_node_not_found((const uint8_t *)nf, 14));
assert(!ml_ctrl_body_node_not_found((const uint8_t *)nf, 5));
const char *ok = "{\"Node\":{}}";
assert(!ml_ctrl_body_node_not_found((const uint8_t *)ok, strlen(ok)));
assert(!ml_ctrl_body_node_not_found(NULL, 0));
''')

    def test_coord_reports_rejection(self):
        coord = (MICROLINK_SRC / 'ml_coord.c').read_text().replace('\r\n', '\n')
        main = (MICROLINK_SRC / 'microlink.c').read_text().replace('\r\n', '\n')
        internal = (MICROLINK_SRC.parent / 'include/microlink_internal.h').read_text().replace('\r\n', '\n')
        cmake = (MICROLINK_SRC.parent / 'CMakeLists.txt').read_text()
        self.assertIn('"src/ml_ctrl_reject.c"', cmake)
        self.assertRegex(internal, r'volatile bool ctrl_rejected;')
        self.assertRegex(internal, r'#define ML_CTRL_REJECTED_RETRY_MS\s+60000')
        self.assertIn('ml_ctrl_register_error(resp_json', c_function(coord, 'static int do_register(microlink_t *ml, ml_noise_state_t *noise)'))
        self.assertIn('ml_ctrl_body_node_not_found(', c_function(coord, 'static int do_fetch_peers(microlink_t *ml, ml_noise_state_t *noise)'))
        self.assertIn('ml_ctrl_body_node_not_found(', c_function(coord, 'static int poll_map_update(microlink_t *ml, ml_noise_state_t *noise)'))
        self.assertRegex(coord, r'ml->ctrl_rejected = false;\s*\n\s*ml->state = ML_STATE_CONNECTED;')
        self.assertIn('ml->ctrl_rejected ? ML_CTRL_REJECTED_RETRY_MS', coord)
        get_state = c_function(main, 'microlink_state_t microlink_get_state(const microlink_t *ml)')
        self.assertIn('ctrl_rejected', get_state)
        self.assertIn('ML_STATE_ERROR', get_state)


class TailscaleLinkTests(unittest.TestCase):
    def setUp(self):
        self.link = (SKETCH_DIR / 'TailscaleLink.cpp').read_text()

    def test_begin_guards(self):
        begin = function(self.link, 'void tailscaleBegin()')
        psram = begin.index('ESP.getPsramSize() == 0')
        nokey = begin.index('!tailscaleHasKey()')
        init = begin.index('microlink_init(')
        self.assertLess(psram, init)
        self.assertLess(nokey, init)
        self.assertIn('TS_STATE_NO_PSRAM', begin[psram:nokey])
        self.assertIn('TS_STATE_NO_KEY', begin[nokey:init])
        self.assertIn('.auth_key = tsAuthKey', begin)

    def test_loop_reports_problem_and_live_path(self):
        loop = function(self.link, 'void tailscaleLoop()')
        self.assertIn('tsLastError = tailscaleProblem(', loop)
        self.assertIn('tsPeerPath = tailscalePeerPath(', loop)

    def test_new_key_starts_a_new_node(self):
        # A node key deleted from one tailnet is refused when registering with another
        # tailnet's key (docs/debugging/2026-10-31_node-removal-not-reported.md), so a saved
        # or removed auth key also drops MicroLink's stored keys, as a fresh login would.
        save = function(self.link, 'bool tailscaleSaveKey(const String &key)')
        self.assertLess(save.index('prefs.putString("key", key)'), save.index('microlink_factory_reset()'))
        self.assertIn('microlink_factory_reset()', function(self.link, 'void tailscaleClearKey()'))

    def test_no_restart_on_failure(self):
        self.assertNotIn('ESP.restart', self.link)
        self.assertNotIn('esp_restart', self.link)

    def test_setup_and_loop_call_sites(self):
        sketch = (SKETCH_DIR / 'esp32-birdnet-mic.ino').read_text()
        self.assertRegex(sketch, r'applyMdnsSetting\(\);\s*\n#ifdef BIRDNET_TS_FORK\s*\n\s*tailscaleLoad\(\);\s*\n\s*tailscaleBegin\(\);\s*\n#endif')
        loop = function(sketch, 'void loop()')
        self.assertRegex(loop, r'#ifdef BIRDNET_TS_FORK\s*\n\s*tailscaleLoop\(\);\s*\n\s*#endif')

    def test_tcpip_stack_fits_wireguard(self):
        # WireGuard encrypts in the lwIP tcpip thread (tiT); 3072 overflowed under a stream.
        defaults = (ROOT / 'idf' / 'sdkconfig.defaults').read_text()
        m = re.search(r'^CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=(\d+)$', defaults, re.M)
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1)), 6144)

    def test_tcp_send_buffer_covers_remote_rtt(self):
        # Throughput is capped at send buffer / RTT. The 48 kHz stream needs ~99 KB/s; the
        # default 5760 bytes fell short at ~62 ms to a remote BirdNET-Go and dropped audio.
        defaults = (ROOT / 'idf' / 'sdkconfig.defaults').read_text()
        m = re.search(r'^CONFIG_LWIP_TCP_SND_BUF_DEFAULT=(\d+)$', defaults, re.M)
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1)) / 0.150, 96000)

    def test_fork_audio_ring_in_psram_covers_wan_stalls(self):
        # A remote client's TCP loss recovery stalls longer than the 0.47 s internal ring;
        # the fork keeps 4 s in PSRAM so the backlog survives and drains afterwards.
        sketch = (SKETCH_DIR / 'esp32-birdnet-mic.ino').read_text()
        self.assertRegex(sketch, r'#if \(defined\(CONFIG_IDF_TARGET_ESP32C5\) \|\| defined\(BIRDNET_TS_FORK\)\) && CONFIG_SPIRAM\n#define AUDIO_RING_IN_PSRAM 1')
        cap = function(sketch, 'static size_t computeAudioRingBufferCapacity()')
        self.assertRegex(cap, r'#if AUDIO_RING_IN_PSRAM && defined\(BIRDNET_TS_FORK\)\n(?:\s*//.*\n)*\s*size_t remote = \(size_t\)currentSampleRate \* sizeof\(int16_t\) \* 4;')

    def test_per_packet_logs_quiet(self):
        begin = function(self.link, 'void tailscaleBegin()')
        self.assertIn('esp_log_level_set("ml_wg_mgr", ESP_LOG_WARN)', begin)

    def test_microlink_calls_lwip_under_core_lock(self):
        # ml_wg_mgr_task feeds packets into lwIP outside the tcpip thread; without the core
        # lock this asserted in lwip_netconn_do_writemore (docs/debugging/2026-10-30_tailnet-rtsp-panics.md).
        defaults = (ROOT / 'idf' / 'sdkconfig.defaults').read_text()
        self.assertIn('CONFIG_LWIP_TCPIP_CORE_LOCKING=y', defaults)
        src = (ROOT / 'idf/libs/microlink/components/microlink/src/ml_wg_mgr.c').read_text().replace('\r\n', '\n')
        self.assertIn('#include "ml_lwip_lock.h"', src)
        src = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
        depth, unlocked = 0, []
        lines = src.splitlines()
        for n, line in enumerate(lines, 1):
            code = line.split('//', 1)[0]
            if 'ML_LWIP_LOCK();' in code:
                depth += 1
            if 'ML_LWIP_UNLOCK();' in code:
                # An unlock on an early-return path leaves the function, not the region.
                following = [l.strip() for l in lines[n:n + 3] if l.strip()]
                if not any(l.startswith('return') for l in following[:2]):
                    depth -= 1
            if re.search(r'\bwireguardif_(?!peer_init\b)\w+\s*\(', code) and not code.lstrip().startswith(('extern', 'static')) and depth <= 0:
                unlocked.append(f'{n}: {line.strip()}')
        self.assertEqual(unlocked, [])

    def test_wireguard_rx_not_printed_per_packet(self):
        src = (ROOT / 'idf/libs/microlink/components/wireguard_lwip/src/wireguardif.c').read_text()
        self.assertNotRegex(src, r'(?m)^\s*printf\("\[WG_RX\] type=')

    def test_microlink_component_pinned(self):
        root_cmake = (ROOT / 'idf' / 'CMakeLists.txt').read_text()
        self.assertIn('libs/microlink/components', root_cmake)
        rev = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT / 'idf' / 'libs' / 'microlink',
                             capture_output=True, text=True, check=True).stdout
        self.assertTrue(rev.startswith('1a31126'), rev)  # Nero7991/microlink lwip-core-lock, on 216da33
        defaults = (ROOT / 'idf' / 'sdkconfig.defaults').read_text()
        self.assertIn('CONFIG_ML_ENABLE_CELLULAR=n', defaults)
        self.assertIn('CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y', defaults)


if __name__ == '__main__':
    unittest.main()
