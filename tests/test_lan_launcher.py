"""LAN launch sessions preserve user saves/config and keep local graphics independent."""
import configparser
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('lan_launcher',ROOT/'tools/lan_sparring.py')
launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)

class LanLauncherTests(unittest.TestCase):
    def fixture(self,folder,role):
        root=Path(folder);binary=root/'sf2';source=root/'custom-save';source.mkdir()
        (source/'profile.xml').write_text('original profile')
        ini=root/'original.ini';ini.write_text('[storage]\nsave_directory = custom-save\n[audio]\nvolume = 0.7\n[video]\ninternal_resolution = 1x\n')
        args=launcher.parser().parse_args([role,*(['127.0.0.1'] if role=='join' else []),'--binary',str(binary),'--config',str(ini),'--fps','120','--aspect','32:9','--resolution','0.50x','--upscaler','fsr1','--aa','smaa'])
        out=root/'session';out.mkdir()
        return args,out,source,ini
    def test_private_save_config_and_graphics_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            args,out,source,ini=self.fixture(folder,'host');before=ini.read_bytes()
            config=launcher.configure(args,out);parsed=configparser.ConfigParser();parsed.read(config)
            self.assertEqual(parsed['video']['target_fps'],'120');self.assertEqual(parsed['video']['internal_resolution'],'0.50x')
            self.assertEqual(parsed['video']['aspect_ratio'],'32:9');self.assertEqual(parsed['video']['antialiasing'],'smaa')
            self.assertEqual(parsed['audio']['volume'],'0.7');self.assertEqual(parsed['network']['enabled'],'false')
            self.assertEqual(parsed['input']['player_setup'],'solo');self.assertEqual(parsed['storage']['save_directory'],'save')
            (out/'save/profile.xml').write_text('session changes')
            self.assertEqual((source/'profile.xml').read_text(),'original profile');self.assertEqual(ini.read_bytes(),before)
    def test_join_does_not_inherit_host_or_replay_options(self):
        with tempfile.TemporaryDirectory() as folder:
            args,out,_,_=self.fixture(folder,'join');config=launcher.configure(args,out)
            with patch.dict(os.environ,{'SF2_LAN_HOST':'bad','SF2_LAN_SEED':'999','SF2_REPLAY_INPUT':'old','SF2RECOMP_NO_SCALE':'1'}):
                env=launcher.environment(args,out,config)
            self.assertEqual(env['SF2_LAN_JOIN'],'127.0.0.1')
            for key in ['SF2_LAN_HOST','SF2_LAN_SEED','SF2_LAN_MATCH','SF2_REPLAY_INPUT','SF2RECOMP_NO_SCALE','SF2_REPLAY_TRACE']:
                self.assertNotIn(key,env)
            self.assertIn('1200:DOWN:',env['SF2RECOMP_INPUT'])
    def test_missing_save_fails_before_starting_game(self):
        with tempfile.TemporaryDirectory() as folder:
            args,out,source,_=self.fixture(folder,'host');(source/'profile.xml').unlink();source.rmdir()
            with self.assertRaisesRegex(ValueError,'No existing save'):launcher.configure(args,out)
