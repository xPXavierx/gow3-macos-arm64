"""Display changes follow real-frame cap policy; no game dump is required."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch
from paths import ROOT
sys.path.insert(0,str(ROOT/'packaging/macos/launcher'))
import session

class DisplayPolicyTest(unittest.TestCase):
    def test_generation_keeps_the_same_cpu_profile(self):
        plain=session.plan('2160','native','60')
        generated=session.plan('2160','native','60',True)
        self.assertEqual(plain['profile'],generated['profile'])
        self.assertFalse(plain['frame_generation'])
        self.assertTrue(generated['frame_generation'])
        self.assertEqual(generated['engine'],60)

    def test_unlimited_and_generation_do_not_request_60hz(self):
        with tempfile.TemporaryDirectory() as temp:
            resources=Path(temp);(resources/'DisplayMode').touch()
            for cap,generated in [('0',False),('0',True),('60',True)]:
                with self.subTest(cap=cap,framegen=generated),patch.object(session.subprocess,'Popen') as start:
                    with session.display_refresh(resources,cap,generated):pass
                    start.assert_not_called()

    def test_cap_60_releases_lease_even_on_interruption(self):
        with tempfile.TemporaryDirectory() as temp:
            resources=Path(temp);(resources/'DisplayMode').touch()
            child=Mock();child.stdin=io.BytesIO();child.stdout=io.BytesIO(b'{"state":"applied"}\n{"state":"restored"}\n')
            with patch.object(session.subprocess,'Popen',return_value=child) as start,patch.object(session.select,'select',return_value=([child.stdout],[],[])),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(KeyboardInterrupt):
                    with session.display_refresh(resources,'60',False):raise KeyboardInterrupt
            start.assert_called_once()
            self.assertTrue(child.stdin.closed)
            child.wait.assert_called_once_with(timeout=5)

if __name__=='__main__':unittest.main()
