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
| `car_select.args` | no memory card -> start anyway; title (Start); new profile (BURNOUT); don't save, autosave off; main menu -> World Tour -> USA -> Silver Lake -> Race -> the car select ("SELECT A COMPACT"), its 3D garage and cars | the main menu at ~5,100, the car select at ~7,800 |

The frame numbers are vertical blanks counted by the host, so they hold only
with `--virtual-time`; in real time the presses drift with the machine's speed.
