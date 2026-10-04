# Default Solux configuration in Python.
# Edit ~/.config/solux/config.py to override these values.

class Appearance:
    sloppyfocus = True
    bypass_surface_visibility = False
    borderpx = 0
    rootcolor = "0x222222ff"
    fullscreen_bg = "0.0, 0.0, 0.0, 1.0"
    default_opacity_unfocus = 1.0
    default_opacity_focus = 1.0
    scenefx_opacity_inactive = 0.95
    scenefx_opacity_active = 0.98
    scenefx_shadow = True
    scenefx_shadow_only_floating = False
    scenefx_shadow_color = "0x00000080"
    scenefx_shadow_color_focus = "0x000000b0"
    scenefx_shadow_blur_sigma = 20
    scenefx_shadow_blur_sigma_focus = 30
    scenefx_corner_radius = 0
    scenefx_corner_radius_only_floating = False

class Blur:
    blur = True
    blur_xray = False
    blur_ignore_transparent = True
    blur_radius = 5
    blur_num_passes = 3
    blur_noise = 0.02
    blur_brightness = 0.90
    blur_contrast = 0.90
    blur_saturation = 1.10

class Animations:
    animations = True
    animation_type_open = "slide"
    animation_type_close = "slide"
    animation_fade_in = True
    animation_fade_out = True
    zoom_initial_ratio = 0.30
    zoom_end_ratio = 0.80
    fadein_begin_opacity = 0.50
    fadeout_begin_opacity = 0.50
    animation_duration_move = 500
    animation_duration_open = 400
    animation_duration_close = 300
    animation_duration_focus = 0
    animation_duration_tag = 300
    tag_animation_direction = "horizontal"
    animation_curve_move = "0.46, 1.0, 0.29, 0.99"
    animation_curve_open = "0.46, 1.0, 0.29, 0.99"
    animation_curve_close = "0.46, 1.0, 0.29, 0.99"
    animation_curve_focus = "0.46, 1.0, 0.29, 0.99"
    animation_curve_opafadein = "0.46, 1.0, 0.29, 0.99"
    animation_curve_opafadeout = "0.5, 0.5, 0.5, 0.5"
    animation_curve_tag = "0.46, 1.0, 0.29, 0.99"

class Gaps:
    gappx = 10
    smartgaps = False
    gaps = True

class Border_Details:
    urgentcolor = "0xffffffff"
    bordercolor = "0xAFA9A5ff"
    focuscolor = "0xAFA9A5ff"
    borderspx = 3
    borderepx = 2
    borderspx_offset = 2
    borderepx_negative_offset = 0
    borderscolor = "0x65727Aff"
    borderecolor = "0x20272Aff"
    borders_focuscolor = "0x8E9EA7ff"
    borders_urgentcolor = "0xE8E9E8ff"
    bordere_focuscolor = "0x20272Aff"
    bordere_urgentcolor = "0x20272Aff"
    border_color_type = "BrdStartEnd"
    borders_only_floating = False

class Input:
    modkey = "super"
    keyboard_layout = "us,ru"
    keyboard_options = "grp:win_space_toggle"
    repeat_rate = 25
    repeat_delay = 600
    tap_to_click = True
    tap_and_drag = True
    drag_lock = True
    natural_scrolling = False
    disable_while_typing = True
    left_handed = False
    middle_button_emulation = False
    scroll_method = "LIBINPUT_CONFIG_SCROLL_2FG"
    click_method = "LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS"
    send_events_mode = "LIBINPUT_CONFIG_SEND_EVENTS_ENABLED"
    accel_profile = "LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE"
    accel_speed = 0.0
    button_map = "LIBINPUT_CONFIG_TAP_MAP_LRM"
    swipe_min_threshold = 20

class Dwindle_Settings:
    dwindle_type = "dwindle"
    dwindle_attach = False
    dwindle_mirror = False
    dwindle_mouse_client = True

tags = ["1", "2", "3", "4", "5", "6", "7", "8", "9"]

autostart = [
    "echo test"
]

rules = [
    {"appid": "Gimp_EXAMPLE", "workspace": 0, "floating": True, "fullscreen": False, "monitor": -1},
    {"appid": "firefox_EXAMPLE", "workspace": 0, "floating": False, "fullscreen": False, "monitor": -1},
]

monrules = [
    [None, 0.5, 1, 1.0, "dwindle", "WL_OUTPUT_TRANSFORM_NORMAL", -1, -1],
]

layouts = {
    "dwindle": "dwindle",
    "tile": "tile",
    "float": "float",
    "monocle": "monocle",
}

gestures = [
    ["SWIPE", "left", 3, "cycletag", -1, "int"],
    ["SWIPE", "right", 3, "cycletag", 1, "int"],
    ["TZOOM", "out", 3, "spawn", "foot"],
]

keys = [
    ["MODKEY", "r", "spawn", "rofi -show window -window-thumbnail -show-icons -theme ~/.config/rofi/we.rasi.d"],
    ["MODKEY", "e", "spawn", "nemo"],
    ["MODKEY", "t", "spawn", "foot"],
    ["MODKEY", "b", "spawn", "qutebrowser"],
    ["MODKEY|SHIFT", "b", "spawn", "chromium"],
    ["MODKEY", "w", "reload_config"],
    ["MODKEY|CTRL", "Left", "swapdir", 0, "ui"],
    ["MODKEY|CTRL", "Right", "swapdir", 1, "ui"],
    ["MODKEY|CTRL", "Up", "swapdir", 2, "ui"],
    ["MODKEY|CTRL", "Down", "swapdir", 3, "ui"],
    ["MODKEY", "Left", "focusdir", 0, "ui"],
    ["MODKEY", "Right", "focusdir", 1, "ui"],
    ["MODKEY", "Up", "focusdir", 2, "ui"],
    ["MODKEY", "Down", "focusdir", 3, "ui"],
    ["MODKEY", "j", "focusstack", 1, "int"],
    ["MODKEY", "k", "focusstack", -1, "int"],
    ["MODKEY", "i", "incnmaster", 1, "int"],
    ["MODKEY", "d", "incnmaster", -1, "int"],
    ["MODKEY", "g", "togglegaps", "NONE"],
    ["MODKEY", "h", "setmfact", -0.05, "float"],
    ["MODKEY", "l", "setmfact", 0.05, "float"],
    ["MODKEY", "Return", "zoom", "NONE"],
    ["MODKEY", "Tab", "view", 0, "ui"],
    ["MODKEY", "q", "killclient", "NONE"],
    ["MODKEY", "a", "setlayout", 0],
    ["MODKEY", "f", "setlayout", 1],
    ["MODKEY", "m", "setlayout", 3],
    ["MODKEY", "z", "cyclelayout", 1, "int"],
    ["MODKEY", "x", "spawn", "soluxctl set-layout '[ monocle ]'"],
    ["MODKEY|SHIFT", "z", "cyclelayout", -1, "int"],
    ["MODKEY|CTRL", "k", "setopacityunfocus", 0.1, "float"],
    ["MODKEY|CTRL", "j", "setopacityunfocus", -0.1, "float"],
    ["MODKEY|CTRL|SHIFT", "K", "setopacityfocus", 0.1, "float"],
    ["MODKEY|CTRL|SHIFT", "J", "setopacityfocus", -0.1, "float"],
    ["MODKEY|CTRL", "f", "togglefullscreen", "NONE"],
    ["MODKEY", "0", "view", "ALL", "ui"],
    ["MODKEY|SHIFT", "parenright", "tag", "ALL", "ui"],
    ["MODKEY", "comma", "focusmon", -1, "int"],
    ["MODKEY", "period", "focusmon", 1, "int"],
    ["MODKEY|SHIFT", "less", "tagmon", -1, "int"],
    ["MODKEY|SHIFT", "greater", "tagmon", 1, "int"],
    ["MODKEY|CTRL", "q", "quit", "NONE"],
    ["CTRL|ALT", "Terminate_Server", "quit", "NONE"],
]

for vt in range(1, 13):
    keys.append(["CTRL|ALT", f"XF86Switch_VT_{vt}", "chvt", vt, "ui"])

# Tag bindings: the same semantics as the original TAGKEYS definitions.
for i in range(1, 10):
    mask = 1 << (i - 1)
    keys.extend([
        ["MODKEY", str(i), "view", mask, "ui"],
        ["MODKEY|CTRL", str(i), "toggleview", mask, "ui"],
        ["MODKEY|SHIFT", ["exclam", "at", "numbersign", "dollar", "percent", "asciicircum", "ampersand", "asterisk", "parenleft"][i-1], "tag", mask, "ui"],
        ["MODKEY|CTRL|SHIFT", ["exclam", "at", "numbersign", "dollar", "percent", "asciicircum", "ampersand", "asterisk", "parenleft"][i-1], "toggletag", mask, "ui"],
    ])

# Float-layout canvas controls. These actions are ignored outside Float layout.
keys.extend([
    ["MODKEY|SHIFT", "Left", "movecanvas", 0, "int"],
    ["MODKEY|SHIFT", "Right", "movecanvas", 1, "int"],
    ["MODKEY|SHIFT", "Up", "movecanvas", 2, "int"],
    ["MODKEY|SHIFT", "Down", "movecanvas", 3, "int"],
    ["MODKEY|SHIFT", "equal", "zoomcanvas", 1, "int"],
    ["MODKEY|SHIFT", "minus", "zoomcanvas", -1, "int"],
])

buttons = [
    # Pan the Float canvas by dragging with the configured modifier + mouse button.
    ["ClkClient", "MODKEY|SHIFT", "BTN_LEFT", "canvasdrag", 0],
    ["ClkRoot", "MODKEY|SHIFT", "BTN_LEFT", "canvasdrag", 0],
    ["ClkClient", "MODKEY", "BTN_LEFT", "moveresize", 2],
    ["ClkClient", "MODKEY", "BTN_RIGHT", "moveresize", 3],
    ["ClkClient", "MODKEY", "SCROLL_UP", "cycletag", -1, "int"],
    ["ClkClient", "MODKEY", "SCROLL_DOWN", "cycletag", 1, "int"],
]
