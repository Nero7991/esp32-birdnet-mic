"""Check the .ino to .cpp converter used by the ESP-IDF build."""
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ino_to_cpp', ROOT / 'idf' / 'tools' / 'ino_to_cpp.py')
ino_to_cpp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ino_to_cpp)

SAMPLE = r'''
#include <stdint.h>
struct Point { int x; int y; };
static int counter = 0; // { not a block }
const char *s = "a { b";

int usesLater() { return later(2) + twice(); }

static bool later(int a,
                  int b = 3) {
    if (a) { return b > 0; }
    return false;
}

int twice(int n = 2) { return n * 2; }

int declared(int);
int declared(int v) { return v; }

namespace ns { int inner() { return 1; } }
struct S { int method() { return 2; } };
template <typename T> T ident(T v) { return v; }
int main() { return usesLater() + declared(1) + ns::inner() + ident(1); }
'''


class InoToCppTests(unittest.TestCase):
    def test_prototypes_for_top_level_functions(self):
        protos = ino_to_cpp.prototypes(SAMPLE)
        self.assertIn('int usesLater();', protos)
        self.assertIn('static bool later(int a, int b = 3);', protos)
        self.assertIn('int twice(int n = 2);', protos)

    def test_skips_declared_nested_and_templates(self):
        joined = '\n'.join(ino_to_cpp.prototypes(SAMPLE))
        self.assertNotIn('declared', joined)
        self.assertNotIn('inner', joined)
        self.assertNotIn('method', joined)
        self.assertNotIn('ident', joined)
        self.assertNotIn('main', joined)

    def test_default_arguments_move_to_prototype(self):
        out = ino_to_cpp.convert(SAMPLE, 'sample.ino')
        self.assertIn('int twice(int n = 2);', out)
        self.assertIn('int twice(int n ) {', out.replace('int n /*= 2*/', 'int n '))

    def test_converted_sample_compiles_and_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            cpp = pathlib.Path(temp) / 'sample.cpp'
            cpp.write_text(ino_to_cpp.convert(SAMPLE, 'sample.ino'))
            binary = pathlib.Path(temp) / 'sample'
            subprocess.run(['g++', '-std=gnu++17', '-Wall', str(cpp), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 1 + 4 + 1 + 1 + 1)

    def test_real_sketch_converts(self):
        source = (ROOT / 'esp32-birdnet-mic' / 'esp32-birdnet-mic.ino').read_text()
        protos = ino_to_cpp.prototypes(source)
        self.assertGreater(len(protos), 100)
        self.assertTrue(any('setup_i2s_driver(' in p for p in protos))


if __name__ == '__main__':
    unittest.main()
