"""Host-side validation of real network configuration and disc boot settings."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkConfigTests(unittest.TestCase):
    def test_native_static_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "config.c"
            source.write_text(r'''
#include <assert.h>
#include "net_config.h"
int main(void) {
 uint32_t value;
 assert(GC_NetParseIPv4("192.168.1.42", &value) && value == 0xc0a8012a);
 const char *bad[] = {"", "1.2.3", "1.2.3.4.5", "256.0.0.1", "1.2.3.-1", "1.2.3.4x", "1.2.3.4 ", "1.2.3.0000", "0x7f000001"};
 for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) assert(!GC_NetParseIPv4(bad[i], &value));
 assert(GC_NetValidateStatic("192.168.1.42", "255.255.255.0", "192.168.1.1"));
 assert(GC_NetValidateStatic("10.1.2.3", "255.255.0.0", "10.1.0.1"));
 assert(!GC_NetValidateStatic("10.1.2.3", "255.255.0.0", "0.0.0.0"));
 assert(!GC_NetValidateStatic("10.1.2.3", "255.0.255.0", "10.1.2.1"));
 assert(!GC_NetValidateStatic("10.1.2.3", "255.255.255.0", "10.1.3.1"));
 assert(!GC_NetValidateStatic("10.1.2.0", "255.255.255.0", "10.1.2.1"));
 assert(!GC_NetValidateStatic("10.1.2.255", "255.255.255.0", "10.1.2.1"));
 assert(!GC_NetValidateStatic("127.0.0.1", "255.0.0.0", "0.0.0.0"));
 assert(!GC_NetValidateStatic("224.0.0.1", "255.255.255.0", "0.0.0.0"));
 assert(!GC_NetValidateStatic("10.1.2.3", "255.255.255.0", "10.1.2.3"));
 return 0;
}
''')
            binary = Path(tmp) / "config"
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-I", str(ROOT / "engine/engine/platform/gamecube"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_native_socket_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            (base / "network.h").write_bytes((ROOT / "tests/network_mock.h").read_bytes())
            binary = base / "sockets"
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-DXASH_GAMECUBE_LIBOGC2=1", "-I", str(base), "-I", str(ROOT / "engine/engine/platform/gamecube"), str(ROOT / "tests/network_socket_shim.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def package(self, *args):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for name in ("engine/scripts", "dod", "valve", "lean", "vmin", "out"):
                (base / name).mkdir(parents=True)
            (base / "dod/liblist.gam").write_text("test fixture")
            (base / "out/dodgc.dol").write_bytes(b"fixture")
            # Intercept the real script's disc-builder boundary, retain exactly
            # what the console would receive in gamecube.cfg.
            (base / "engine/scripts/build-gamecube-disc.py").write_text('''
import pathlib, sys
args = sys.argv
cfg = pathlib.Path(args[args.index("--data")+1]) / "gamecube.cfg"
pathlib.Path(args[args.index("--output")+1]).write_bytes(cfg.read_bytes())
''')
            env = dict(os.environ, DODGC_ENGINE=str(base / "engine"), DODGC_CONTENT_DOD=str(base / "dod"), DODGC_CONTENT_VALVE=str(base / "valve"), DODGC_OUT=str(base / "out"), DODGC_DOL=str(base / "out/dodgc.dol"), DODGC_PLAYTESTS=str(base / "playtests"), XASH3D_GC_ISO_CACHE=str(base / "cache"), DODGC_LEAN_DOD=str(base / "lean"), DODGC_LEAN_VALVE=str(base / "vmin"))
            result = subprocess.run([str(ROOT / "scripts/build-lean-iso.sh"), "--menu-only", "--skip-stage", *args], env=env, capture_output=True, text=True)
            cfg = base / "out/dodgc-menu.iso"
            return result, cfg.read_text() if cfg.exists() else ""

    def test_host_dhcp(self):
        result, cfg = self.package("--listen", "dod_flash", "--net")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("\nnet\n", cfg)
        self.assertNotIn("netstatic", cfg)
        self.assertNotIn("headless", cfg)

    def test_client_dhcp(self):
        result, cfg = self.package("--connect", "203.0.113.10:27015", "--net", "--map", "dod_flash")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("net\nconnect 203.0.113.10:27015\nmap dod_flash", cfg)

    def test_static_subnet(self):
        result, cfg = self.package("--listen", "dod_flash", "--net-ip", "192.168.7.42", "--headless")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("netstatic 192.168.7.42 255.255.255.0 192.168.7.1\nheadless", cfg)

    def test_bad_static_rejected(self):
        for ip in ("999.1.2.3", "10.1.2.0", "10.1.2.255"):
            result, cfg = self.package("--listen", "dod_flash", "--net-ip", ip)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(cfg, "")

    def test_explicit_gateway(self):
        result, cfg = self.package("--listen", "dod_flash", "--net-ip", "10.1.2.3", "--net-mask", "255.255.0.0", "--net-gateway", "10.1.0.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("netstatic 10.1.2.3 255.255.0.0 10.1.0.1", cfg)


if __name__ == "__main__":
    unittest.main()
