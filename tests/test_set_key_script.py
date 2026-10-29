"""The key-entry helper must never put the key in argv, output or history."""
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'idf' / 'tools' / 'set-tailscale-key.sh'


class SetKeyScriptTests(unittest.TestCase):
    def test_reads_hidden_and_posts_from_stdin(self):
        text = SCRIPT.read_text()
        self.assertIn('read -rs', text)
        self.assertIn('--data-urlencode auth_key@-', text)
        self.assertIn('X-ESP32MIC-CSRF: 1', text)
        for bad in ('echo "$key"', 'echo $key', 'set -x', 'auth_key=$key', 'auth_key="$key"'):
            self.assertNotIn(bad, text)


if __name__ == '__main__':
    unittest.main()
