# Keel

A calm command center for an [Omarchy](https://omarchy.org) laptop. It tells you how the machine is
doing in plain words, puts the switches you reach for in one place, shows which apps are busiest,
and finds what's filling your disk. It's a small, fast native app, made for people who find btop
too much.

It has three pages; **Tab** moves between them.

- **Overview.** One headline ("all calm.", "running hot.", "disk nearly full."), how long the
  battery lasts as a clock time, four meters (battery, workload, memory, heat), four modes
  (everyday, focus, travel, night) that set several switches at once, six switches (Wi-Fi,
  Bluetooth, do not disturb, night light, battery saver, stay awake), brightness and volume,
  screens (both, monitor only, mirror), and the three busiest apps with a quit button.
- **Details.** Processor history and each core, memory and swap, disk space and activity, network
  speed and history, battery health and cycles, temperatures and fan, and every process: sort by
  CPU or memory, press `/` to find one by name, `x` to end it (it asks, and offers force).
- **Storage.** Disk used and free, the usual space hogs (trash, app caches, downloads, the package
  cache) with their sizes, and the biggest folders in your home folder. Press Enter to go into a
  folder and Backspace to come back out.

Keel follows your Omarchy theme: its accent colour, its light or dark mode, and its window corners.
It runs only while its window is open; press Esc and it's gone.

## Keys

| Key | Does |
| --- | --- |
| `Tab` | overview → details → storage |
| `1` `2` `3` `4` | everyday · focus · travel · night |
| `w` `b` `d` `n` `s` `a` | Wi-Fi · Bluetooth · do not disturb · night light · battery saver · stay awake |
| `e` | screens: both · monitor only · mirror |
| `,` `.` | brightness down / up |
| `-` `=` | volume down / up |
| `m` | mute (overview) · sort by memory (details) |
| `c` | sort processes by CPU |
| `/` | find a process |
| `↑` `↓` / `j` `k` | pick a process or folder |
| `x` / `Delete` | end the picked process |
| `Enter` / `Backspace` | into a folder / back out (storage) |
| `o` | open the picked folder in the file manager |
| `r` | rescan the disk |
| `t` | light / dark until Keel closes |
| `?` | every key |
| `Esc` / `q` | close |

## Run it

You need Omarchy (or Arch with Hyprland) and the Qt 6 build tools, which Omarchy already includes.

```sh
bin/build          # compiles to build/keel
build/keel         # run it
bin/install        # builds an Arch package and installs it, so Keel shows up in the app launcher
```

`bin/install` asks for your password (it runs `makepkg -si`). To remove Keel: `sudo pacman -R keel`.

## Check it

```sh
bin/verify         # formatting, QML lint, a warnings-as-errors build, and the tests
```

The tests use temporary folders and never change the machine. Flipping a switch, ending a process
or emptying the trash is never exercised by them.

## How it works

- `src/vitals.*` turns kernel numbers into words and the headline. It's pure logic, fully tested.
- `src/apps.*` reads processes, groups them into apps, ranks them, and ends them.
- `src/controls.*` holds the switches, levels, modes and screens. Every change goes through the same
  Omarchy command its own menus use (`omarchy-bluetooth-power`, `omarchy-toggle-idle`,
  `omarchy-hyprland-monitor-internal` …), so Keel and the desktop always agree.
- `src/storage.*` is the disk scanner and the storage page's state.
- `src/backend.*` reads the machine every two seconds and hands the window ready-made text.
- `src/theme.*` follows the Omarchy theme. `src/hyprland.*` talks to Hyprland's socket directly.
- `src/Main.qml` is the window; `Meter`, `Graph` and `Panel` are its small parts.

Why things are the way they are is in [DECISIONS.md](DECISIONS.md).

## Licence

MIT. See [LICENSE](LICENSE).
