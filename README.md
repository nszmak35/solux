# Solux

Solux is a Wayland compositor built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.20 and
[SceneFX](https://github.com/wlrfx/scenefx) 0.5. It descends from [dwl](https://codeberg.org/dwl/dwl) and
borrows animation ideas from [MangoWM](https://github.com/DreamMaoMao/mangowc). It is configured with a
Python file and controlled at runtime with `soluxctl`.

## Features

- Tiling layouts: dwindle, tile and monocle, plus a free-floating layout with a zoomable canvas (camera per tag).
- SceneFX effects: rounded corners (`scenefx_corner_radius`), window shadows, background blur, window opacity.
- Animations: window open, close, move and resize, and a workspace slide when switching tags.
- Borders: a plain border plus optional outer ("start") and inner ("end") borders with separate colors.
- Touchpad gestures (swipe and pinch) that can be bound to any action.
- Protocols: dwl-ipc-unstable-v2 (status bars), ext-workspace-v1, layer-shell, session lock,
  foreign-toplevel management, output management, XWayland (optional).
- `soluxctl`: reload the configuration, switch layouts and workspaces, and watch compositor state.

## Repository layout

```
.
├── Makefile, config.mk     build configuration
├── default/config.py       default configuration (installed to /etc/solux/config.py)
├── protocols/              protocol XML files, used to generate headers at build time
├── solux.desktop           Wayland session entry
├── licenses/               licenses of the projects Solux is derived from
└── src/
    ├── solux.c             core: types, globals, window management, main()
    ├── client.h            client helper functions
    ├── util.c, util.h      small utilities
    ├── animations/         animation core, per-client animations, tag switch animation
    ├── canvas/             float-layout canvas (camera, zoom, coordinate conversion)
    ├── config/             configuration helpers, scalar settings, loading and reloading
    ├── effects/            borders, corner radius, shadow, blur and opacity
    ├── ext_workspace/      ext-workspace-v1 implementation
    ├── gestures/           touchpad gestures
    ├── input/              keyboard and pointer handling
    ├── ipc/                dwl-ipc protocol and the soluxctl control socket
    ├── layouts/            dwindle, tile and monocle layouts
    ├── normalfloat/        helpers for normal floating windows and the canvas
    ├── output/             monitors, output management and rendering
    ├── parser/             embedded Python configuration parser
    ├── shell/              layer-shell, session lock, foreign-toplevel
    ├── soluxctl/           soluxctl command line tool
    └── xwayland/           XWayland client handling
```

`src/solux.c` includes the modules under `src/` directly, so Solux is built as a single translation unit
together with `util.c`, `parser/parserconf.c`, `ext_workspace/wlr_ext_workspace_v1.c` and the generated
dwl-ipc code.

## Dependencies

- wlroots 0.20
- SceneFX 0.5 (`pkg-config` must find `scenefx-0.5`)
- wayland-server, wayland-client, wayland-protocols, wayland-scanner
- xkbcommon, libinput, pixman
- Python 3 development files (embedded interpreter for the configuration parser)
- xcb and xcb-icccm when XWayland support is enabled in `config.mk`

The Makefile first tries `pkg-config python3-embed` and falls back to `python3-config`.

## Building

```sh
make
sudo make install
```

`make` generates the protocol headers from `protocols/*.xml` (and the stable protocols shipped with
wayland-protocols) into `build/gen/`, compiles everything into `build/`, and produces `./solux` and
`./soluxctl`. `make clean` removes `build/` and both binaries.
`make install` installs the binaries, `solux.desktop` and `/etc/solux/config.py`.

Edit `config.mk` to change the install prefix, the wlroots flags or to disable XWayland.

## Running

```sh
solux [-v] [-d] [-c config.py] [-s startup-command]
```

`-v` prints the version, `-d` enables debug logging, `-c` selects a configuration file and `-s` runs a command at startup.

## Configuration

Configuration is a Python file. Files are looked up in this order:

1. `-c /path/to/config.py`
2. `~/.config/solux/config.py`
3. the default file: `SOLUX_PY_DEFAULT`, `default/config.py`, `/etc/solux/config.py`, or `default/config.py` next to the binary

`default/config.py` lists every supported setting. Scalar settings are grouped in classes (`Appearance`, `Blur`,
`Animations`, `Gaps`, `Border_Details`, `Input`, `Dwindle_Settings`); other settings are module-level values:
`tags`, `autostart`, `rules`, `monrules`, `layouts`, `gestures`, `keys` and `buttons`.

Both `spawn` actions and `autostart` entries are single command strings executed with `/bin/sh -c`:

```python
keys = [
    ["MODKEY", "r", "spawn", "rofi -show drun"],
    ["MODKEY", "t", "spawn", "foot"],
]

autostart = [
    "swaybg -i img.jpg",
    "waybar",
]
```

Reload the configuration with the `reload_config` action or `soluxctl reconfig`.

### Corner radius, borders and shadows

`scenefx_corner_radius` is the single radius for the whole window: the client buffer, the borders, blur and
shadow all use it. `scenefx_corner_radius_only_floating` limits rounding to floating windows.

Shadows need `scenefx_shadow = True` and a blur sigma above zero (`scenefx_shadow_blur_sigma` for unfocused and
`scenefx_shadow_blur_sigma_focus` for the focused window); a sigma of 0 draws no visible shadow.
`scenefx_shadow_color` and `scenefx_shadow_color_focus` are `0xRRGGBBAA` values.

### Tag (workspace) animation

Switching tags slides the old workspace out and the new one in. The offset is purely visual; window geometry,
canvas coordinates, camera and zoom are not modified.

```python
class Animations:
    tag_animation_direction = "horizontal"   # or "vertical"
    animation_duration_tag = 300             # ms, 0 disables the tag animation
    animation_curve_tag = "0.46, 1.0, 0.29, 0.99"
```

It follows the global `animations` switch.

## soluxctl

```
soluxctl help
soluxctl reconfig
soluxctl set-workspace <name_tag>
soluxctl set-layout <layout>
soluxctl watch-layout
soluxctl watch-kblayout
soluxctl watch-monitor
soluxctl watch-workspace <name_tag>
soluxctl watch-focus
soluxctl watch-all
```

## Licenses

Solux contains code derived from dwl, dwm, sway, tinywl and MangoWM. See `LICENSE` and the files in `licenses/`.
