#!/usr/bin/env python3
"""check_settings.py against a settings dump in Mednafen's real DumpDef format."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_settings as C  # noqa: E402


def record(name, typ, default, lo="", hi="", enums=(), flags="", aliases=()):
    out = [name, flags, "description", "", typ, default, lo, hi, str(len(enums))]
    for e in enums:
        out += [e, "", ""]
    out.append(str(len(aliases)))
    out += list(aliases)
    return "\n".join(out) + "\n"


DUMP = (record("cheats", "MDFNST_BOOL", "1")
        + record("ffspeed", "MDFNST_FLOAT", "4", "0.25", "15")
        + record("nothrottle", "MDFNST_BOOL", "0", aliases=("nothrottle_alias",))
        + record("ss.dbg_cem", "MDFNST_ENUM", "auto", enums=("data_cb", "data", "full", "auto"),
                 flags="MDFNSF_SUPPRESS_DOC MDFNSF_NONPERSISTENT")
        + record("video.driver", "MDFNST_ENUM", "default", enums=("default", "opengl", "softfb")))


class Check(unittest.TestCase):
    def run_check(self, cfg):
        with tempfile.TemporaryDirectory() as d:
            dp, cp = os.path.join(d, "def.txt"), os.path.join(d, "pin.cfg")
            open(dp, "w").write(DUMP)
            open(cp, "w").write(cfg)
            return C.check(C.parse_def(dp), cp)

    def test_parse(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "def.txt")
            open(p, "w").write(DUMP)
            defs = C.parse_def(p)
        self.assertEqual(sorted(defs)[:3], ["cheats", "ffspeed", "nothrottle"])
        self.assertEqual(defs["ss.dbg_cem"]["enums"], ["data_cb", "data", "full", "auto"])
        self.assertIn("MDFNSF_NONPERSISTENT", defs["ss.dbg_cem"]["flags"])
        self.assertIn("nothrottle_alias", defs)

    def test_good(self):
        self.assertEqual(self.run_check("cheats 0\nnothrottle 1\nss.dbg_cem full\nffspeed 4\n; c\n\n"), [])

    def test_unknown_name(self):
        e = self.run_check("ss.cpucache_emumode full\n")
        self.assertEqual(len(e), 1)
        self.assertIn("no setting 'ss.cpucache_emumode'", e[0])

    def test_bool_must_be_0_or_1(self):
        self.assertEqual(len(self.run_check("cheats yes\n")), 1)
        self.assertEqual(len(self.run_check("cheats 2\n")), 1)

    def test_enum_values(self):
        e = self.run_check("ss.dbg_cem bogus\n")
        self.assertEqual(len(e), 1)
        self.assertIn("data_cb|data|full|auto", e[0])

    def test_numeric_range(self):
        self.assertEqual(len(self.run_check("ffspeed 99\n")), 1)
        self.assertEqual(len(self.run_check("ffspeed x\n")), 1)


if __name__ == "__main__":
    unittest.main()
