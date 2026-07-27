# Solux

> **A minimalist Wayland compositor focused on simplicity, speed, and efficiency.**

Solux is my minimalist Wayland compositor stripped of all non-essential features. There are **no animations, blur effects, or rounded corners**—only the functionality required for a clean and responsive desktop experience.

Solux is configured through `config.h` and must be compiled from source. It is a fork of **dwl**, built on top of **wlroots 0.20**.

## Features

- Minimal and lightweight
- Built on **wlroots 0.20**
- Fork of **dwl**
- Configuration via `config.h`
- Three window layouts:
  - Tile
  - Dwindle
  - Floating
- Built-in minimalist status bar
- No animations, blur, or rounded corners

## Build Dependencies

Before compiling, install the required development packages:

- GCC
- GNU Make
- pkg-config
- wayland
- wayland-protocols
- wlroots 0.20
- libinput
- libxkbcommon
- pixman
- libdrm
- mesa (EGL/OpenGL)
- xcb (XWayland support, optional)

Make sure the **development** versions of these libraries are installed on your distribution.

## Building

Solux is compiled with **GCC**.

Clone the repository:

```bash
git clone https://github.com/nszmak35/solux.git
cd solux
```

Compile:

```bash
make
```

## Installation

Install the compiled binary:

```bash
sudo make install
```

If you want to remove Solux later:

```bash
sudo make uninstall
```

## Configuration

Before building, edit `config.h` to customize keybindings, appearance, layouts, and other settings. Rebuild Solux after making changes.

## Version

Current version:

**0.44-nszmak**

---

## Good luck!

Thank you for trying **Solux**.

Have fun customizing your desktop, and enjoy a fast, minimal, distraction-free Wayland experience.
