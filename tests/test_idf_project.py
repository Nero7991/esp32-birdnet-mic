"""Check the ESP-IDF build of the Tailscale fork (S3 Sense only)."""
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
IDF = ROOT / 'idf'

EXPECTED_PARTITIONS = [
    ('nvs', 'data', 'nvs', 0x9000, 20 * 1024),
    ('otadata', 'data', 'ota', 0xE000, 8 * 1024),
    ('app0', 'app', 'ota_0', 0x10000, 3264 * 1024),
    ('app1', 'app', 'ota_1', 0x340000, 3264 * 1024),
    ('spiffs', 'data', 'spiffs', 0x670000, 1536 * 1024),
    ('coredump', 'data', 'coredump', 0x7F0000, 64 * 1024),
]


def parse_size(text):
    text = text.strip()
    if text.upper().endswith('K'):
        return int(text[:-1], 0) * 1024
    if text.upper().endswith('M'):
        return int(text[:-1], 0) * 1024 * 1024
    return int(text, 0)


class IdfProjectTests(unittest.TestCase):
    def test_sdkconfig_defaults(self):
        text = (IDF / 'sdkconfig.defaults').read_text()
        lines = set(line.strip() for line in text.splitlines())
        for required in (
            'CONFIG_IDF_TARGET="esp32s3"',
            'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y',
            'CONFIG_SPIRAM=y',
            'CONFIG_SPIRAM_MODE_OCT=y',
            'CONFIG_FREERTOS_HZ=1000',
            'CONFIG_PARTITION_TABLE_CUSTOM=y',
            'CONFIG_LWIP_IP4_FRAG=y',
            'CONFIG_LWIP_IP4_REASSEMBLY=y',
            'CONFIG_ML_TAILSCALE_AUTH_KEY=""',
            'CONFIG_ML_ENABLE_CONFIG_HTTPD=n',
        ):
            self.assertIn(required, lines, required)

    def test_partitions_match_published_s3_table(self):
        rows = []
        for line in (IDF / 'partitions.csv').read_text().splitlines():
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            name, ptype, subtype, offset, size = [f.strip() for f in line.split(',')[:5]]
            rows.append((name, ptype, subtype, int(offset, 0), parse_size(size)))
        self.assertEqual(rows, EXPECTED_PARTITIONS)

    def test_main_builds_sketch_sources_in_place(self):
        cmake = (IDF / 'main' / 'CMakeLists.txt').read_text()
        self.assertIn('esp32-birdnet-mic', cmake)
        self.assertIn('BIRDNET_TS_FORK=1', cmake)
        for flag in ('-fno-exceptions', '-DWM_NODEBUG'):
            self.assertIn(flag, cmake)
        # No copied sketch sources inside idf/.
        copies = [p.name for p in IDF.rglob('*.cpp')
                  if (ROOT / 'esp32-birdnet-mic' / p.name).exists()]
        self.assertEqual(copies, [])

    def test_component_pins(self):
        manifest = (IDF / 'main' / 'idf_component.yml').read_text()
        self.assertRegex(manifest, r'espressif/arduino-esp32:\s*\n?\s*(version:\s*)?"?3\.3\.12"?')
        modules = (ROOT / '.gitmodules').read_text()
        self.assertIn('idf/libs/WiFiManager', modules)
        self.assertIn('idf/libs/PubSubClient', modules)
        status = subprocess.run(['git', 'submodule', 'status'], cwd=ROOT,
                                capture_output=True, text=True, check=True).stdout
        wm = subprocess.run(['git', 'describe', '--tags', '--exact-match'],
                            cwd=IDF / 'libs' / 'WiFiManager',
                            capture_output=True, text=True).stdout.strip()
        ps = subprocess.run(['git', 'describe', '--tags', '--exact-match'],
                            cwd=IDF / 'libs' / 'PubSubClient',
                            capture_output=True, text=True).stdout.strip()
        self.assertIn('2.0.17', wm, status)
        self.assertIn('2.8', ps, status)

    def test_fork_version_only_in_fork_build(self):
        source = (ROOT / 'esp32-birdnet-mic' / 'esp32-birdnet-mic.ino').read_text()
        self.assertRegex(source, re.compile(
            r'#ifdef BIRDNET_TS_FORK\s*\n#define FW_VERSION "1\.36-ts\.1"\s*\n#else\s*\n#define FW_VERSION "1\.36"\s*\n#endif'))


if __name__ == '__main__':
    unittest.main()
