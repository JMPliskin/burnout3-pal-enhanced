"""Inspect and execute only argument assembly, never launch the game."""
import ast
from pathlib import Path
import re
from types import SimpleNamespace
import unittest

ROOT=Path(__file__).resolve().parents[3]


def code(name):
    text=(ROOT/name).read_text(encoding='ascii')
    return re.search(r'py -3.12 -B -c "(.*)" %\*',text)[1]


class LauncherTests(unittest.TestCase):
    def command(self,measure=False,pace=None):
        source=code('Jugar Burnout 3 PAL FMV.bat')
        fragment='cmd='+source.split('cmd=',1)[1].split('settings_file=',1)[0]
        args=['launcher']+(['--medir'] if measure else [])
        ns=dict(s={'executable':Path('synthetic.exe')},elf=Path('synthetic.elf'),
                iso=Path('synthetic.iso'),card=Path('synthetic-card'),sys=SimpleNamespace(argv=args),
                pace=[] if pace is None else [pace],fixed={'vsync':'off','resolution':'original','fullscreen':'off','aspect':'4:3','antialiasing':'off'})
        exec(compile(fragment,'launcher-arguments','exec'),ns)
        return ns['cmd']

    def test_all_launchers_parse_and_frame_times_have_interval(self):
        for path in ROOT.glob('*.bat'):
            if not path.name.startswith(('Jugar Burnout','Capturar VU')):continue
            tree=ast.parse(code(path.name))
            for node in ast.walk(tree):
                if isinstance(node,ast.List):
                    values=[x.value if isinstance(x,ast.Constant) else None for x in node.elts]
                    if '--frame-times' in values:
                        interval=values[values.index('--frame-times')+1]
                        self.assertTrue(isinstance(interval,str) and interval.isdigit())

    def test_measure_preserves_graphics_and_enables_pacing_only(self):
        args=self.command(measure=True)
        self.assertEqual(args[args.index('--frame-times')+1],'120')
        self.assertIn('frame_pacing=on',args)
        self.assertFalse(any(x.startswith(('resolution=','vsync=','aspect=','fullscreen=')) for x in args))
        for flag in ('--window','--gpu','--audio','--memcard'):self.assertIn(flag,args)
        for flag in ('--press','--virtual-time','--vu-interpret','--vu-fast'):self.assertNotIn(flag,args)

    def test_default_does_not_change_settings(self):
        args=self.command();self.assertNotIn('--setting',args);self.assertNotIn('--frame-times',args)

    def test_old_comparison_mode_keeps_valid_interval(self):
        for pace in ('on','off'):
            args=self.command(pace=pace)
            self.assertIn('frame_pacing='+pace,args)
            self.assertEqual(args[args.index('--frame-times')+1],'120')


if __name__=='__main__':unittest.main()
