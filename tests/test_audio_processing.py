"""Numerical DSP reference and memory/rate regressions for the PDM integration."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AudioProcessingTests(unittest.TestCase):
    def native(self, source):
        with tempfile.TemporaryDirectory() as temp:
            cpp = pathlib.Path(temp) / 'test.cpp'
            binary = pathlib.Path(temp) / 'test'
            cpp.write_text(source)
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-fno-sanitize-recover=all',
                            '-I', str(ROOT / 'esp32-birdnet-mic'), str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_integer_audio_against_reference(self):
        self.native((ROOT / 'tests/fixed_audio_test.cpp').read_text())

    def test_reserve_retains_wifi_and_task_headroom(self):
        self.native(r'''
#include "AudioReserve.h"
#include <cassert>
int main() {
 using namespace audioreserve;
 assert(fit(65536,1024,215*1024,150*1024)==65536); // C6/S3 keep full reserve
 auto capacity=fit(65536,1024,120*1024,100*1024); // constrained C3/C5
 assert(capacity>=16384 && capacity<65536);
 assert(120*1024-capacity>=kHeapMargin+kProducerStackBytes+kAllocationOverhead);
 assert(fit(65536,1024,215*1024,33000)==28672); // fragmented heap
 assert(fit(65536,1024,90*1024,80*1024)==0); // cannot keep the minimum safely
 assert(fit(65536,1024,215*1024,16384)==0);
 assert(fit(65536,16384,150*1024,150*1024)==0); // four large chunks cannot fit
 assert(fit(65536,1024,0,0)==0);
}
''')

    def test_word_rate_startup_overruns_retry_and_reset(self):
        self.native(r'''
#include "PdmCapture.h"
#include <cassert>
int main() {
 for(int stride:{1,2}) {
  pdmsw::WordRateDetector rate;
  assert(rate.observe(999999,0,192000,0)==0); // discard startup burst
  assert(rate.observe(999999,100000,192000,0)==0); // begin steady window
  for(int n=1;n<=4;++n)
   assert(rate.observe(19200*stride,100000+n*100000,192000,0)==(n==4?stride:0));
  assert(rate.observe(0,2000000,192000,9)==stride); // confirmed state stays stable
  rate.reset(); assert(!rate.started && rate.stride==0);
 }
 pdmsw::WordRateDetector rate;
 rate.observe(0,0,192000,0);rate.observe(0,100000,192000,0);
 assert(rate.observe(76800,500000,192000,1)==0); // lost pairs must not become plain
 rate.observe(0,600000,192000,1);
 assert(rate.observe(153600,1000000,192000,1)==2);
 rate.reset();rate.observe(0,0,192000,0);rate.observe(0,100000,192000,0);
 assert(rate.observe(115200,500000,192000,0)==0); // ambiguous rate, retry
 assert(rate.observe(153600,900000,192000,0)==2);
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>'))


if __name__ == '__main__':
    unittest.main()
