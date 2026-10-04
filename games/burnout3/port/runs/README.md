# Scripted runs

Each `.args` file is a set of port options that drives the game to a known
point. With `--virtual-time` the run is the same on any machine, under any
load: the same presses land on the same frames and the pictures come out
byte-identical, so a run is a regression test and a way to get somewhere in
the game without playing.

```sh
burnout3.exe <SLUS_210.50> --disc <iso> $(cat games/burnout3/port/runs/car_select.args) \
    --frames out/ --frames-every 200
```

| file | reaches | frames |
| --- | --- | --- |
| `race_training.args` | the same, then a car chosen: RACE TRAINING, the race's loading screen ("HAVE YOU TRIED... WINNING CRASH $?"), the event briefing over the Silver Lake track, and the training's in-engine crash demo ("IMPACT TIME", "33ft Nose Grind + 4.1s Air") | loading ~9,250, briefing ~9,500, the demo ~9,750-10,000; slow - the race's 3D is drawn in software (about an hour on 16 threads). Seen on 2026-10-04 with the build before the switch-only-leaf analysis fix, which translated 0x002A6B80 from the discovered list instead |
| `car_select.args` | no memory card -> start anyway; title (Start); new profile (BURNOUT); don't save, autosave off; main menu -> World Tour -> USA -> Silver Lake -> Race -> the car select ("SELECT A COMPACT"), its 3D garage and cars | the main menu at ~5,100, the car select at ~7,800 |

The frame numbers are vertical blanks counted by the host, so they hold only
with `--virtual-time`; in real time the presses drift with the machine's speed.
