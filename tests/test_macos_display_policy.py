"""Display changes follow real-frame cap policy; no game dump is required."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch
from types import SimpleNamespace
from paths import ROOT
sys.path.insert(0,str(ROOT/'packaging/macos/launcher'))
import session

class DisplayPolicyTest(unittest.TestCase):
    def test_live_render_size_is_independent_of_logical_grid(self):
        for resolution in session.RESOLUTIONS:
            for upscaling in ['native','quality','balanced','performance']:
                with self.subTest(resolution=resolution,upscaling=upscaling):
                    expected=list(session.dimensions(resolution,upscaling))
                    selection=session.plan(resolution,upscaling,'60',live=True)
                    self.assertEqual(selection['input'],[3840,2160])
                    self.assertEqual(selection['render'],expected)
                    self.assertEqual(selection['profile'],'3840x2160-240')
                    self.assertEqual(selection['output'],list(session.RESOLUTIONS[resolution]))

    def test_hud_switch_controls_both_overlays_and_ignores_inherited_experiments(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);resources=root/'App/Contents/Resources'
            resources.mkdir(parents=True);support=root/'Support';support.mkdir()
            out=root/'Profile';out.mkdir();source=root/'Source';source.mkdir()
            modules=SimpleNamespace(module=lambda _: {'elf':b'synthetic ELF'})
            for upscaling in ['native','quality']:
                for hud in [False,True]:
                    with self.subTest(upscaling=upscaling,hud=hud),patch.dict(sys.modules,{'link_modules':modules}),patch.dict(session.os.environ,{'GOW3_FRAMEGEN':'1','GOW3_HUD_DISPLAY_INFO':'1','MTL_HUD_ENABLED':'1','GOW3_SHADER_POLICY':'skip'}):
                        _,env=session.environment(resources,support,root/'Dump',session.plan('2160',upscaling,'60'),upscaling,'60',hud,
                            prepared=(out,source,{'resolution_patch':'Resolution Patch - 4K'}))
                    self.assertEqual(env['MTL_HUD_ENABLED'],str(int(hud)))
                    self.assertEqual(env['GOW3_HUD_DISPLAY_INFO'],str(int(hud)))
                    self.assertEqual(env['GOW3_FRAMEGEN'],'0')
                    self.assertEqual(env['GOW3_SHADER_POLICY'],'complete')
                    self.assertEqual(env['GOW3_LOGICAL_RENDER_SIZE'],
                        'x'.join(map(str,session.dimensions('2160',upscaling))))
                    self.assertIn('show_fps='+str(int(hud))+'\n',(out/'display.ini').read_text())

    def test_legacy_generation_request_is_disabled_without_changing_cpu_profile(self):
        plain=session.plan('2160','native','60')
        generated=session.plan('2160','native','60',True)
        self.assertEqual(plain['profile'],generated['profile'])
        self.assertFalse(plain['frame_generation'])
        self.assertFalse(generated['frame_generation'])
        self.assertEqual(generated['engine'],60)

    def test_unlimited_does_not_request_60hz(self):
        with tempfile.TemporaryDirectory() as temp:
            resources=Path(temp);(resources/'DisplayMode').touch()
            for cap,generated in [('0',False),('0',True)]:
                with self.subTest(cap=cap,framegen=generated),patch.object(session.subprocess,'Popen') as start:
                    with session.display_refresh(resources,cap,generated):pass
                    start.assert_not_called()

    def test_cap_60_releases_lease_even_on_interruption(self):
        with tempfile.TemporaryDirectory() as temp:
            resources=Path(temp);(resources/'DisplayMode').touch()
            child=Mock();child.stdin=io.BytesIO();child.stdout=io.BytesIO(b'{"state":"applied"}\n{"state":"restored"}\n')
            with patch.object(session.subprocess,'Popen',return_value=child) as start,patch.object(session.select,'select',return_value=([child.stdout],[],[])),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(KeyboardInterrupt):
                    with session.display_refresh(resources,'60',True):raise KeyboardInterrupt
            start.assert_called_once()
            self.assertTrue(child.stdin.closed)
            child.wait.assert_called_once_with(timeout=5)

if __name__=='__main__':unittest.main()
