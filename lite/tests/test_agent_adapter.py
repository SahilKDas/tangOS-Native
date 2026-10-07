import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ADAPTER = pathlib.Path(__file__).resolve().parents[1] / "assets/agent_adapter.py"

class AdapterTests(unittest.TestCase):
    def test_instructions_and_exact_arguments(self):
        with tempfile.TemporaryDirectory(prefix="lite-adapter-") as tmp:
            root = pathlib.Path(tmp)
            (root / "rules.txt").write_text("ROOT_RULE\nNESTED_RULE", encoding="utf-8")
            driver = root / "driver.py"
            driver.write_text("INSTRUCTIONS = 'original'\nimport sys\ndef main():\n assert 'ROOT_RULE' in INSTRUCTIONS and 'NESTED_RULE' in INSTRUCTIONS and 'original' in INSTRUCTIONS\n assert sys.argv[1:] == ['argument with spaces', '--literal']\n print('injected')\n", encoding="utf-8")
            env = dict(os.environ, TANGOS_AGENT_INSTRUCTIONS=str(root / "rules.txt"), PYTHONDONTWRITEBYTECODE="1")
            result = subprocess.run([sys.executable, str(ADAPTER), str(driver), 'argument with spaces', '--literal'], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('injected', result.stdout)
            driver.write_text('def main():\n print("unsafe driver launched")\n', encoding='utf-8')
            result = subprocess.run([sys.executable, str(ADAPTER), str(driver)], env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('unsafe driver launched', result.stdout)

if __name__ == '__main__':
    unittest.main()
