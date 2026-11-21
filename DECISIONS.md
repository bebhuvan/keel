# Decisions

Numbered and append-only. Each says what was decided, why, and what was turned down.

## 1. A native Qt Quick app, built like Omarchy's own

C++ and Qt Quick, built with qmake into one binary and installed as an Arch package, the way Hype,
Omacut and Monologue are built. Qt is already on every Omarchy machine, so Keel adds no
dependencies. Rejected: an Electron or web app (hundreds of MB of memory for a status window), and a
terminal UI (the point was something friendlier than btop).

## 2. No Qt Quick Controls, Material or other styles

The window is plain Qt Quick: Text, Rectangle, a TextInput for search. Controls and styles load
more code and memory, and this design needs none of their widgets.

## 3. Software rendering, no GTK platform theme, no input-method module

Measured on this laptop: the GPU renderer added about 40 MB, Qt's GTK platform theme about 250 ms
of startup and more memory, and the Fcitx input-method module about 50 ms. A flat window of text and
rectangles draws just as fast in software, and Keel has no native dialogs and only one plain text
field. Result: about 50 MB and a window in roughly 0.2–0.4 s, down from 95 MB and 1 s. Rejected:
keeping the defaults "for correctness"; nothing Keel shows depends on them.

## 4. Read the kernel directly; spend processes only where there's no other way

Battery, temperature, fans, radios, brightness, processes and network all come from `/proc` and
`/sys`; the power profile from D-Bus; windows and monitors from Hyprland's socket. Processes are
started only for things with no file or socket (do not disturb, night light, volume, the Wi-Fi
name) and those are read every 10 seconds, not every 2. The process scan uses plain system calls
and remembers each process's program, so a steady machine costs one small read per process.

## 5. Every change goes through Omarchy's own commands

Switches and screens call the same scripts Omarchy's menus use, so state is remembered the same way
(rfkill for Bluetooth, Omarchy's flag files for stay-awake and monitor layout) and the bar and
notifications stay in step. Rejected: calling `bluetoothctl`, `hyprctl keyword` and so on directly,
which would drift from what Omarchy expects.

## 6. Plain words first, numbers second

The headline names the single most urgent problem (battery low, then heat, memory, disk, load) or
says the mode's calm line. It judges heat and load over the last six seconds so one spike doesn't
flash a warning. Battery is shown as the clock time it lasts until. A restart nudge appears after
seven days up. The disk warning fires below 5 GB or 5% free, not at a fixed percentage, because 92%
of a large disk can still be plenty.

## 7. Four fixed modes, opinionated on purpose

Everyday, focus, travel and night, each a fixed set of the six switches; anything else shows as
"custom". Travel keeps Wi-Fi on: being cut off is worse than the minutes it saves. Rejected:
user-editable modes for now; that's a settings screen this app deliberately doesn't have yet.

## 8. Ending things: polite first, force only when asked

Apps with a window are closed through Hyprland, like their own close button, so they can ask to
save. Background programs and single processes get SIGTERM after a confirmation, with SIGKILL
("force") offered in the same question. Only the user's own processes can be ended, never Keel
itself or PID 1. Window addresses must match `0x` plus hex before they reach Hyprland's Lua
dispatcher, so nothing else can be injected into that command.

## 9. The storage scan: native, parallel, saved

Disk use is measured by walking the home folder with `openat`/`fstatat` across all cores (folders
up to five levels down become separate tasks), never following links, never crossing into another
disk, and counting a hard-linked file once. Detail is kept four levels deep for anything over
64 MB, and the result is saved to `~/.cache/keel/storage.json`, so the page opens instantly and a
rescan is the user's choice. On this laptop (5.3 million files) a scan takes about 42 s against
69 s for `du -sx`. A "100× faster than disktree" scan isn't possible: every file must be looked at,
and the disk sets that pace. Sizes are before compression, as `du` reports; the page says so when
the disk compresses (btrfs `compress=`). Getting compressed sizes needs root (`compsize`).
Rejected: shelling out to `du` or `gdu` (slower, or another dependency) and scanning the whole
system (needs root, and the user's files are what they can act on).

## 10. Emptying the trash is the only deletion, and it asks

Keel deletes nothing except the trash, only when asked, after a confirmation that says it can't be
undone. Everything else on the storage page opens the folder in the file manager instead.
Rejected: one-click cache cleaning, since apps differ in what's safe to remove.

## 11. Light and dark, with Keel's own surfaces

Keel takes the accent and the light/dark mode from the Omarchy theme, but its surfaces are its own
pure white or neutral near-black, so a cream or tinted theme still gives a clean window. `t` flips
light and dark until Keel closes. Rejected: copying the theme's background colour.

## 12. English only, for now

The Omarchy app template supports 50 languages; Keel's text is English only because its one user
reads English. Every string is in one place per file, so translation can be added later.

## 13. The name and mark

"Keel": the part of a ship that keeps it steady and upright, fitting a calm command center. The
mark is a waterline over a hull and its keel. No Arch package or command uses the name.
