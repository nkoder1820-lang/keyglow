# Keyglow

Backlight scenes for RGB keyboards on Linux. An on-screen keyboard shows
exactly what the real one is doing, and fifty-odd scenes range from a quiet
candle to keys that flare as you type.

It comes in two parts that share one repository:

- **The app** (`keyglow`): the window, the background process that drives the
  lights, and a command for key bindings and scripts.
- **The Omarchy bar widget** (Keyglow RGB): a keyboard icon in the bar that
  opens a scene picker with brightness, speed and colour.

## Which keyboards

Keyglow reaches a keyboard in one of two ways and picks by itself:

- **Cosmic Byte CB Artemis** (68 keys, SinoWealth `258a:010c`): driven
  directly, nothing else to install. This is the keyboard Keyglow was written
  on and the only one it has been tried on by hand.
- **Any keyboard OpenRGB supports** (several hundred, from Corsair, Razer,
  Logitech, SteelSeries, Keychron, Ducky, ASUS, MSI and many more; see
  [openrgb.org/devices.html](https://openrgb.org/devices.html)): driven
  through OpenRGB's local server. Install it and leave its server running:

      sudo pacman -S openrgb
      openrgb --server

  To have it there after every login, switch on "Start at login" with "Start
  server" in OpenRGB's settings. Keyglow finds the first keyboard the server
  lists, reads its key layout from it, and draws that layout on screen. A
  keyboard lit in a few zones instead of per key is shown as a row of blocks.

If neither is found, the app and the widget say "No keyboard found".

What is different through OpenRGB: there is no "sync" step (the layout comes
from OpenRGB), white balance starts at neutral, and the scenes that react to
typing play without reacting, because Keyglow only reads key presses from the
Artemis. The OpenRGB path is checked against a simulated server in the tests,
not yet against a shelf of real keyboards: if yours is listed by OpenRGB and
misbehaves here, please open an issue.

## Install on Omarchy

    omarchy plugin add https://github.com/nkoder1820-lang/keyglow.git --enable
    ~/.config/omarchy/plugins/io.github.nkoder1820-lang.keyglow/bin/install

The first line adds the bar widget. The second builds the app and installs it
as an Arch package called `keyglow`; it asks for your password once. The
package holds `/usr/bin/keyglow`, a launcher entry, an icon, and one udev rule
that lets your user reach the CB Artemis's lighting interface without root.
With a CB Artemis, unplug it and plug it back in afterwards, then open Keyglow
from the launcher once to match the lights to your keys. With any other
keyboard, start OpenRGB's server as described above.

**Needs:** `qt6-base`, `qt6-declarative` and `qt6-wayland` (the install step
pulls them in), plus `base-devel` to build, and `openrgb` for keyboards other
than the CB Artemis. The widget needs `keyglow` 1.1 or
newer on your `PATH` and says so in its panel when it is missing.

**Changes outside the plugin folder:** the package above, and Keyglow's own
settings in `~/.config/keyglow/`. "Light up at login" in the app adds
`~/.config/autostart/keyglow-daemon.desktop`, only when you switch it on. The
build happens in `~/.cache/keyglow`. Nothing else is touched.

## Remove

    omarchy plugin remove io.github.nkoder1820-lang.keyglow
    sudo pacman -R keyglow
    rm -rf ~/.config/keyglow ~/.cache/keyglow ~/.config/autostart/keyglow-daemon.desktop

The keyboard goes back to its own built-in effect about a second after the
background process stops.

## The bar widget

| On the icon | Does |
| --- | --- |
| Click | Open the panel |
| Right-click | Lights off, and back on |
| Middle-click | Next scene |
| Scroll | Brightness |

In the panel: arrow keys (or `h j k l`) move through the scenes and Enter
picks one, `P` switches the lights, `+` and `-` change brightness, `[` and `]`
change tab, `O` opens the app, Esc closes. The Favourites tab appears once you
have starred a scene in the app. The icon dims when the lights are off or the
keyboard is unplugged.

Open or close the panel from a key binding with
`omarchy-shell io.github.nkoder1820-lang.keyglow toggle`.

## How it works

This section is about the CB Artemis; other keyboards get the same frames
through OpenRGB's SDK protocol (`src/openrgb.cpp`), about fifteen times a
second.

The Artemis has no Linux backlight driver, and the only known protocol is
"direct" mode: the PC streams colours, and the keyboard falls back to its own
effect about a second after the stream stops. So Keyglow is two halves of one
binary: the window edits `~/.config/keyglow/config.json`, and `keyglow daemon`
keeps streaming it after the window is closed. Nothing is written to the
keyboard's memory. Lighting works over the USB cable only.

**Sync it once.** The controller only knows a 21 × 6 grid of lights, not
which key each one is under. Press `T` (or "Quick sync"): the board glows dim
blue with one key bright white, and you press the white key. After 12 to 16
of those Keyglow has ruled out every wiring but one and fills in the rest,
then spot-checks Space, Enter and an arrow. If the board turns out not to be
wired as a grid, it switches to checking all the lights one by one. The result
is saved to `~/.config/keyglow/layout.json`. Until then Keyglow guesses, and
lights the whole grid so no key stays dark.

**White balance.** This keyboard's green LEDs are much weaker than its red
and blue ones, so equal parts of all three come out pink, not white. Keyglow
corrects what it sends: red and blue are cut to 28% of green by default, and
the colour is then scaled back up so pure red or blue stay at full strength.
The on-screen keyboard shows the colour you asked for; the real one now
matches it. If your board differs, use the White balance sliders in the app
or `keyglow white <red%> <blue%>`.

**Typing-reactive scenes** (Ripple, Typing heat) read which keys are held from
the keyboard's own HID reports. Presses are only turned into light; they are
never stored or sent anywhere, and the background process reads them only
while one of those scenes is selected.

## Scenes

Fifty-three of them, each shown on its card as a miniature of your keyboard
that plays when you hover it. Star the ones you like (`S`) to collect them
under Favourites.

- **Signature**: Terracotta, Reactor, Saber Clash, Digital Rain, The Grid,
  Scanner, Neon City, Synthwave, Kinetic ⚡, Gamma, Swing, Night Watch.
- **Themes**: Omarchy (follows the desktop accent), Tokyo Night, Catppuccin,
  Gruvbox, Nord, Dracula, Rosé Pine, Everforest, Kanagawa, Solarized.
- **Nature**: Aurora, Sunset, Ocean, Ember, Candlelight, Starlight, Fireflies,
  Sakura, Lava, Glacier, Nebula, Thunderstorm, Forest.
- **Reactive** ⚡: Ripple, Typing Heat, Frostbite, Fireworks, Spotlight, Raindrops.
- **Work**: Focus, WASD, Vim, Code.
- **Basics**: Solid, Breathe, Heartbeat, Duotone, Rainbow, Colour Cycle,
  Painted (your own per-key colours), Keyboard's Own.

Most scenes are one of a couple of dozen effect kinds with their own palette,
so adding one is a line in the table in `src/lighting.cpp` plus its name in
`src/backend.cpp`.

The first launch walks through four steps: hello, quick sync, pick a look,
and lights at login.

## Shortcuts

| Key | Does |
| --- | --- |
| Space | Lights off, and back on |
| ← → | Previous and next scene |
| ↑ ↓ | Brightness |
| [ ] | Speed |
| S | Star or unstar the current scene |
| P | Paint over the current look |
| F | Fill every key with the current colour |
| T | Sync the lights to your keys |
| ? | Shortcut overlay |
| Q | Quit (the lights stay on) |

## From the terminal or a key binding

    keyglow list | status | toggle | on | off | next | prev
    keyglow preset ember
    keyglow brightness +10      # or -10, or 60
    keyglow speed 30
    keyglow color '#ff8800'
    keyglow white 28 28         # white balance: red and blue, as % of green
    keyglow status --json       # what the bar widget reads
    keyglow list --json         # every scene, with its tab and colours

For example, in Hyprland: `bind = SUPER, F9, exec, keyglow next`.

## Build

    bin/build     # build/keyglow
    bin/test      # the app's tests, then the bar panel's (needs node)
    bin/install   # package, launcher entry, udev rule

The bar widget is `manifest.json`, `Panel.qml` and `Model.js` in the repository
root, checked with `omarchy plugin validate .` on a clean checkout.

## Licence

MIT, see `LICENSE`. The Themes scenes borrow the names and palettes of open
source colour schemes; Keyglow is not affiliated with those projects.

`tools/probe.py` asks the keyboard to identify itself without changing anything.
