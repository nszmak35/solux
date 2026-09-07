# Solux

> A simple and configurable Wayland compositor.

Solux is a Wayland compositor based on [dwl](https://codeberg.org/dwl/dwl), focused on being lightweight, configurable, and easy to modify.

Unlike the original dwl configuration approach, Solux uses an external configuration file written in DNX.

The default configuration is located at:

```text
default/config.dnx
```

When installed, Solux can use the system configuration:

```text
/etc/solux/config.dnx
```

and the user's configuration:

```text
~/.config/solux/config.dnx
```

Solux supports reloading its configuration at runtime, so many configuration changes can be applied without restarting the compositor.

## Features

- Wayland compositor based on dwl
- wlroots-based rendering and input handling
- Tiling and floating window management
- Multiple layouts
- Gaps
- Per-monitor configuration
- Monitor rules
- Runtime configuration reload
- Keyboard and pointer configuration
- libinput configuration
- XWayland support (when compiled with `XWAYLAND`)
- DWL IPC / ext-workspaces support
- Layer-shell support
- Output management
- Output power management
- Session locking
- Fractional scaling support
- XDG activation and decoration support
- Configurable window rules

## Dependencies

### Runtime and build dependencies

Solux requires:

- `libinput`
- `wayland`
- `wlroots` 0.20
- `xkbcommon`
- `libdrm`

The following are required at build time:

- `wayland-protocols`
- `pkg-config`

Depending on your distribution, development packages may have a `-devel` suffix.

For example, on distributions using separate development packages, you will generally need:

```text
libinput-devel
wayland-devel
wlroots-devel
xkbcommon-devel
libdrm-devel
wayland-protocols
pkg-config
```

### Optional XWayland support

If Solux is compiled with `XWAYLAND`, the following additional dependencies are required:

- Xlib
- XCB
- XCB ICCCM
- XWayland support from wlroots

Typical development packages are:

```text
libX11-devel
libxcb-devel
xcb-util-wm-devel
```

The exact package names depend on your distribution.

## Building

Clone the repository and enter the source directory:

```sh
git clone <repository-url>
cd solux
```

Make sure all dependencies are installed.

Then build Solux:

```sh
make
```

If the build succeeds, the `solux` executable will be produced.

To install it system-wide:

```sh
doas make install
```

or:

```sh
sudo make install
```

By default, the executable is installed into:

```text
/usr/local/bin/
```

The configuration can be installed to:

```text
/etc/solux/config.dnx
```

depending on the installation rules provided by the project's `Makefile`.

## Configuration

Solux uses DNX configuration files.

The example/default configuration is:

```text
default/config.dnx
```

This file contains the default settings for Solux and can be used as a reference when creating your own configuration.

For a user-specific configuration, use:

```text
~/.config/solux/config.dnx
```

The system-wide configuration path is:

```text
/etc/solux/config.dnx
```

The recommended approach is to copy the default configuration to your user configuration directory:

```sh
mkdir -p ~/.config/solux
cp default/config.dnx ~/.config/solux/config.dnx
```

Then edit:

```sh
$EDITOR ~/.config/solux/config.dnx
```

### Configuration priority

Solux looks for configuration in the following order:

1. `~/.config/solux/config.dnx`
2. `/etc/solux/config.dnx`
3. `default/config.dnx`

The exact fallback behavior depends on how Solux was built and where it is launched from.

The user configuration takes priority over the default configuration.

## DNX

DNX is the configuration format used by Solux.

Instead of recompiling Solux every time a configuration option is changed, configuration is read from `config.dnx`.

The configuration contains sections for things such as:

- appearance
- keybindings
- mouse bindings
- layouts
- monitor rules
- window rules
- input configuration
- commands
- startup commands

For example, the default configuration contains the layout and monitor configuration used by Solux.

See:

```text
default/config.dnx
```

for the complete list of available options and their syntax.

## Runtime configuration reload

Solux supports reloading the configuration without restarting the compositor.

After changing `config.dnx`, use the configured reload keybinding or the corresponding reload command.

This allows configuration changes such as:

- colors
- gaps
- layouts
- monitor rules
- keybindings
- input settings
- window rules

to be applied while Solux is running.

## Layouts

Solux provides several tiling layouts, including:

- tile
- monocle
- spiral
- right spiral
- dwindle
- right dwindle
- right tile

The available layouts and their names are defined in:

```text
default/config.dnx
```

Layouts can be selected and switched through the configured keybindings.

## Monitor configuration

Solux supports monitor rules through `%monrules`.

Monitor rules can define:

- monitor name
- master-factor (`mfact`)
- number of master windows (`nmaster`)
- scale
- layout
- rotation/transform
- X position
- Y position

This allows different monitors to have independent layouts and positioning.

For example, monitor-specific configuration belongs in:

```text
default/config.dnx
```

and can be overridden from:

```text
~/.config/solux/config.dnx
```

## Window rules

Window rules allow Solux to apply settings automatically to applications.

Rules can specify things such as:

- application ID
- title
- tags
- floating state
- focused opacity
- unfocused opacity
- monitor

This makes it possible to automatically place applications on specific tags or monitors and configure whether they should float.

## Input configuration

Solux uses libinput for input devices.

The configuration supports options including:

- natural scrolling
- scroll method
- click method
- acceleration profile
- tap button mapping

These settings can be configured through `config.dnx`.

## XWayland

XWayland support is optional.

When building Solux with:

```text
XWAYLAND
```

defined, Solux includes support for X11 applications through XWayland.

If you do not need X11 application support, Solux can be built without XWayland.

## Wayland protocols

Solux uses a number of Wayland and wlroots protocols, including support for:

- layer-shell
- foreign toplevel management
- fractional scaling
- output management
- output power management
- session locking
- virtual keyboard
- virtual pointer
- pointer constraints
- relative pointer
- screencopy
- primary selection
- data control
- XDG activation
- XDG decorations
- XDG output

The required protocol sources are provided/used during compilation as part of the project's build process.

## Useful software

Solux is a compositor, so you will probably want a few Wayland utilities and applications alongside it.

### Terminal

[foot](https://codeberg.org/dnkl/foot)

### Display and output management

`wlr-randr`

### Status bar

Any Wayland-compatible status bar can be used.

Examples include:

- Waybar
- yambar
- other wlroots-compatible bars

## Starting Solux

Solux can be started from a Wayland-compatible session or directly from a TTY using your preferred launcher/session setup.

A typical setup may look like:

```sh
solux
```

Applications such as a terminal or status bar can then be started using the configured startup commands.

## Configuration example

The main configuration file to study is:

```text
default/config.dnx
```

It is recommended to copy it first:

```sh
mkdir -p ~/.config/solux
cp default/config.dnx ~/.config/solux/config.dnx
```

Then customize the copied file rather than modifying the system/default configuration.

## Known Issues

N/A

## License

GPL-v3
