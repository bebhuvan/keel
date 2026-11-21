# Keel

A calm command center for an [Omarchy](https://omarchy.org) laptop. It tells you how the machine is
doing in plain words, puts the switches you reach for in one place, shows which apps are busiest,
and finds what's filling your disk. It's a small, fast native app, made for people who find btop
too much.

It has three pages; **Tab** moves between them.

- **Overview.** One headline ("All calm.", "Running hot.", "Disk nearly full."), how long the
  battery lasts as a clock time, the battery level, three gauges (processor, memory, heat), four modes
  (everyday, focus, travel, night) that set several switches at once, six switches (Wi-Fi,
  Bluetooth, do not disturb, night light, battery saver, stay awake), brightness and volume,
  screens (both, monitor only), battery care (stop charging at 80%), the batteries of
  connected Bluetooth devices, and the three busiest apps with quit or end. Press `v` to see
  instead which apps have used the most processor time since they opened: the battery hogs.
- **Details.** Processor history and each core, memory and swap, disk space and activity, network
  speed and history, battery health and cycles, temperatures and fan, and every process: sort by
  CPU or memory, press `/` to find one by name, `x` to end it (it asks, and offers force).
- **Storage.** Disk used and free, the usual space hogs (trash, app caches, downloads, the package
  cache), what grew since the last scan, big files untouched for six months, and the biggest
  folders in your home folder (Enter goes into one, Backspace comes back out). Keel shows where
  the space went; deciding what to delete stays with you, in your file manager (`o` opens it).

Programs your desktop runs on (uwsm, Hyprland, PipeWire, the Omarchy shell and the like) are marked
"system" and can't be ended from Keel, even by force.

Keel follows your Omarchy theme: its accent colour, its light or dark mode, and its window corners.
It's drawn plainly on purpose: white or near-black, thin rules instead of boxes, one typeface
(Inter), and orange only for something that needs looking at, such as heat or a full disk.
It runs only while its window is open; press Esc and it's gone.

## Keys

| Key | Does |
| --- | --- |
| `Tab` | overview → details → storage |
| `1` `2` `3` `4` | everyday · focus · travel · night |
| `w` `b` `d` `n` `s` `a` | Wi-Fi · Bluetooth · do not disturb · night light · battery saver · stay awake |
| `e` | screens: both · monitor only |
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
| `v` | busiest now / heaviest since opened |
| `p` | battery care on / off (asks for your password) |
| `t` | light / dark until Keel closes |
| `?` | every key |
| `Esc` / `q` | close |

## Run it

You need Omarchy (or Arch with Hyprland) and the Qt 6 build tools, which Omarchy already includes,
plus the Inter typeface (`sudo pacman -S inter-font`; `bin/install` pulls it in by itself).

```sh
bin/build          # compiles to build/keel
build/keel         # run it
bin/install        # builds an Arch package and installs it, so Keel shows up in the app launcher
```

`bin/install` asks for your password (it runs `makepkg -si`). It packages what's in the folder, so
commit first. To remove Keel: `sudo pacman -R keel`.

Battery care needs the installed package, not `build/keel`. It runs through
`/usr/lib/keel/keel-helper`, a short script that accepts only `battery-care on|off`, and the
system's own password prompt asks for it and says what it's for.

## Check it

```sh
bin/verify         # formatting, QML lint, a warnings-as-errors build, and the tests
```

The tests use temporary folders and never change the machine: the only process a test ends is a
`sleep` it started itself, and the admin helper is only fed arguments it must refuse. Flipping real switches is never exercised by them. `bin/verify` also
runs gitleaks when it's installed, and `.github/workflows/verify.yml` runs it all on every push
once the project has a GitHub remote.

## How it works

- `src/vitals.*` turns kernel numbers into words and the headline. It's pure logic, fully tested.
- `src/apps.*` reads processes, groups them into apps, ranks them, and ends them.
- `src/controls.*` holds the switches, levels, modes and screens. Every change goes through the same
  Omarchy command its own menus use (`omarchy-bluetooth-power`, `omarchy-toggle-idle`,
  `omarchy-hyprland-monitor-internal` …), so Keel and the desktop always agree.
- `src/storage.*` is the disk scanner and the storage page's state.
- `pkgbuild/keel-helper` and `dev.keel.helper.policy` are battery care's password-protected half.
- `src/backend.*` reads the machine every two seconds and hands the window ready-made text.
- `src/theme.*` follows the Omarchy theme. `src/hyprland.*` talks to Hyprland's socket directly.
- `src/Main.qml` is the window. Its small parts: `Section` (one row of the page grid), `Lamp` (an
  on/off indicator and label), `Gauge` (a level with tick marks), `Fader` (a slider), `Trace` (a
  two-minute line chart), `Figure` (a large number with its unit), `Scroller` (a page that scrolls
  when the window is short) and `PlainText`.

Why things are the way they are is in [DECISIONS.md](DECISIONS.md).

## Licence

MIT. See [LICENSE](LICENSE).
