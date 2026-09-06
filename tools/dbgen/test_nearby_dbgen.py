import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
GEN = HERE / "nearby_dbgen.py"

HA = '''from typing import Final
BLUETOOTH: Final[list[dict]] = [
 {"domain":"demo","manufacturer_id":123},
 {"domain":"demo2","local_name":"NB-*","connectable":False},
]
'''
ZHA = '''MODELS_INFO="models_info"
MFR="NearBy"
class Demo:
 signature={MODELS_INFO:[(MFR,"ZB-1"),("Acme","ZB-2")]}
QuirkBuilder("BuilderCo", "QB-1")
'''
OUI = '''Registry,Assignment,Organization Name,Organization Address
MA-L,001A2B,NearBy Labs,Somewhere
'''


class DbgenTest(unittest.TestCase):
    def test_build_validate_lookup_and_corruption(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            (td / "ha.py").write_text(HA)
            zha = td / "zha"
            zha.mkdir()
            (zha / "demo.py").write_text(ZHA)
            (td / "oui.csv").write_text(OUI)
            db = td / "test.nbdb"
            cmd = [
                sys.executable, str(GEN), "build",
                "--ha-bluetooth", str(td / "ha.py"), "--ha-rev", "ha-test",
                "--zha-root", str(zha), "--zha-rev", "zha-test",
                "--oui-csv", str(td / "oui.csv"), "--oui-rev", "oui-test",
                "--build-epoch", "1", "--out", str(db),
            ]
            subprocess.run(cmd, check=True, capture_output=True)
            valid = subprocess.run([sys.executable, str(GEN), "validate", str(db)], check=True, capture_output=True, text=True)
            self.assertTrue(json.loads(valid.stdout)["ok"])
            release = subprocess.run([sys.executable, str(GEN), "validate", str(db), "--release"], capture_output=True)
            self.assertEqual(release.returncode, 2)
            got = subprocess.run([sys.executable, str(GEN), "lookup", str(db), "oui:001A2B"], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(got.stdout)["organization"], "NearBy Labs")
            data = bytearray(db.read_bytes())
            data[-1] ^= 0x01
            db.write_bytes(data)
            broken = subprocess.run([sys.executable, str(GEN), "validate", str(db)], capture_output=True)
            self.assertEqual(broken.returncode, 2)


if __name__ == "__main__":
    unittest.main()
