/* Configuration scalars: defaults, animation curves, border widths and key/value loading. */
#ifndef SOLUX_CONFIG_CONFIG_SCALAR_H
#define SOLUX_CONFIG_CONFIG_SCALAR_H

/*
 * Border widths are unsigned throughout Solux.  Never use strtoul() directly
 * for them: "-1" would otherwise wrap to a very large unsigned value and can
 * make wlroots geometry invalid.  Parse as a signed value first and clamp all
 * negative (and malformed/out-of-range) values to zero.
 */
static unsigned int
dnx_nonnegative_uint(const char *s)
{
	char *end;
	unsigned long long value;

	if (!s)
		return 0;

	while (isspace((unsigned char)*s))
		s++;

	/* A leading minus is always invalid for a size/width. */
	if (*s == '-')
		return 0;

	errno = 0;
	value = strtoull(s, &end, 0);

	/* Reject empty/malformed values and clamp overflow. */
	if (s == end || errno == ERANGE)
		return 0;
	while (isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return 0;
	if (value > UINT_MAX)
		return UINT_MAX;

	return (unsigned int)value;
}

/* Last line of defence: these values must never be negative/wrapped. */
static void
dnx_sanitize_border_widths(void)
{
	/* They are unsigned, so a wrapped negative value is always larger than
	 * INT_MAX.  Reset it instead of letting it reach geometry calculations. */
	if (borderpx > INT_MAX)
		borderpx = 0;
	if (borderspx > INT_MAX)
		borderspx = 0;
	if (borderepx > INT_MAX)
		borderepx = 0;
}

static void
dnx_set_animation_curve(double dst[4], const char *value)
{
	char *tmp, *p, *end;
	int i;
	if (!value) return;
	tmp = dnx_unquote_value(value);
	if (!tmp) return;
	p = tmp;
	for (i = 0; i < 4; i++) {
		dst[i] = strtod(p, &end);
		if (end == p) { free(tmp); return; }
		p = end;
		if (i != 3) {
			while (*p && isspace((unsigned char)*p)) p++;
			if (*p != ',') { free(tmp); return; }
			p++;
		}
	}
	free(tmp);
}

static void
dnx_reset_scalar_config(void)
{
	sloppyfocus = 1;
	bypass_surface_visibility = 0;
	borderpx = 0;
	rootcolor[0] = 0.0f; rootcolor[1] = 0.0f; rootcolor[2] = 0.0f; rootcolor[3] = 1.0f;
	fullscreen_bg[0] = 0.0f; fullscreen_bg[1] = 0.0f; fullscreen_bg[2] = 0.0f; fullscreen_bg[3] = 1.0f;
	log_level = WLR_ERROR;
	gappx = 10;
	smartgaps = 0;
	gaps = 1;

	scenefx_opacity_inactive = 1.0f;
	scenefx_opacity_active = 1.0f;
	scenefx_shadow = 1;
	scenefx_shadow_only_floating = 0;
	scenefx_shadow_color[0] = 0.0f; scenefx_shadow_color[1] = 0.0f;
	scenefx_shadow_color[2] = 0.0f; scenefx_shadow_color[3] = 0.50f;
	scenefx_shadow_color_focus[0] = 0.0f; scenefx_shadow_color_focus[1] = 0.0f;
	scenefx_shadow_color_focus[2] = 0.0f; scenefx_shadow_color_focus[3] = 0.69f;
	scenefx_shadow_blur_sigma = 20;
	scenefx_shadow_blur_sigma_focus = 30;
	scenefx_corner_radius = 10;
	scenefx_corner_radius_only_floating = 0;
	scenefx_blur = 1;
	scenefx_blur_xray = 0;
	scenefx_blur_ignore_transparent = 1;
	scenefx_blur_radius = 5;
	scenefx_blur_num_passes = 3;
	scenefx_blur_noise = 0.02f;
	scenefx_blur_brightness = 0.90f;
	scenefx_blur_contrast = 0.90f;
	scenefx_blur_saturation = 1.10f;

	urgentcolor[0] = 1.0f; urgentcolor[1] = 0.0f; urgentcolor[2] = 0.0f; urgentcolor[3] = 1.0f;
	bordercolor[0] = 1.0f; bordercolor[1] = 0.0f; bordercolor[2] = 0.0f; bordercolor[3] = 1.0f;
	focuscolor[0] = 1.0f; focuscolor[1] = 0.0f; focuscolor[2] = 0.0f; focuscolor[3] = 1.0f;
	borders_focuscolor[0] = 1.0f; borders_focuscolor[1] = 0.0f; borders_focuscolor[2] = 0.0f; borders_focuscolor[3] = 1.0f;
	borders_urgentcolor[0] = 1.0f; borders_urgentcolor[1] = 0.0f; borders_urgentcolor[2] = 0.0f; borders_urgentcolor[3] = 1.0f;
	bordere_focuscolor[0] = 1.0f; bordere_focuscolor[1] = 0.0f; bordere_focuscolor[2] = 0.0f; bordere_focuscolor[3] = 1.0f;
	bordere_urgentcolor[0] = 1.0f; bordere_urgentcolor[1] = 0.0f; bordere_urgentcolor[2] = 0.0f; bordere_urgentcolor[3] = 1.0f;
	borderspx = 2;
	borderepx = 2;
	borderspx_offset = 1;
	borderepx_negative_offset = 0;
	borderscolor[0] = 1.0f; borderscolor[1] = 1.0f; borderscolor[2] = 1.0f; borderscolor[3] = 1.0f;
	borderecolor[0] = 0.0f; borderecolor[1] = 0.0f; borderecolor[2] = 0.0f; borderecolor[3] = 1.0f;
	border_color_type = BrdOriginal;
	borders_only_floating = 0;

	repeat_rate = 25;
	repeat_delay = 600;
	tap_to_click = 1;
	tap_and_drag = 1;
	drag_lock = 1;
	natural_scrolling = 0;
	disable_while_typing = 1;
	left_handed = 0;
	middle_button_emulation = 0;
	swipe_min_threshold = 0;
	scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	accel_speed = 0.0;
	button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
	runtime_modkey = WLR_MODIFIER_LOGO;

	xkb_rules = (struct xkb_rule_names) {
		.layout = KEYBOARD_LAYOUT,
		.options = KEYBOARD_OPTIONS,
	};
}

static void
dnx_animation_defaults(void)
{
	free(animation_type_open);
	free(animation_type_close);
	free(dwindle_type);
	dwindle_type = dnx_strdup("dwindle");
	dwindle_attach = 0;
	dwindle_mirror = 0;
	dwindle_mouse_client = 0;
	animation_type_open = dnx_strdup("slide");
	animation_type_close = dnx_strdup("slide");
	if (!animation_type_open || !animation_type_close ||
		0)
		dnx_oom();
	animations = 1;
		animation_fade_in = 1;
	animation_fade_out = 1;
	zoom_initial_ratio = 0.30f;
	zoom_end_ratio = 0.80f;
	fadein_begin_opacity = 0.50f;
	fadeout_begin_opacity = 0.50f;
	animation_duration_move = 500;
	animation_duration_open = 400;
	animation_duration_close = 300;
	animation_duration_focus = 0;
	animation_duration_tag = 300;
	tag_animation_vertical = 0;
	animation_curve_move[0]=0.46; animation_curve_move[1]=1.0; animation_curve_move[2]=0.29; animation_curve_move[3]=0.99;
	animation_curve_open[0]=0.46; animation_curve_open[1]=1.0; animation_curve_open[2]=0.29; animation_curve_open[3]=0.99;
	animation_curve_close[0]=0.46; animation_curve_close[1]=1.0; animation_curve_close[2]=0.29; animation_curve_close[3]=0.99;
	animation_curve_focus[0]=0.46; animation_curve_focus[1]=1.0; animation_curve_focus[2]=0.29; animation_curve_focus[3]=0.99;
	animation_curve_opafadein[0]=0.46; animation_curve_opafadein[1]=1.0; animation_curve_opafadein[2]=0.29; animation_curve_opafadein[3]=0.99;
	animation_curve_opafadeout[0]=0.5; animation_curve_opafadeout[1]=0.5; animation_curve_opafadeout[2]=0.5; animation_curve_opafadeout[3]=0.5;
	animation_curve_tag[0]=0.46; animation_curve_tag[1]=1.0; animation_curve_tag[2]=0.29; animation_curve_tag[3]=0.99;
}

static void
dnx_load_scalar(const DnxEntry *e)
{
	const char *k = e->key;
	const char *v = e->value;

	if (!k || !v) return;

	/* animations */
	if (!strcasecmp(k, "animations")) animations = dnx_bool_value(v, animations);
		else if (!strcasecmp(k, "animation_fade_in")) animation_fade_in = dnx_bool_value(v, animation_fade_in);
	else if (!strcasecmp(k, "animation_fade_out")) animation_fade_out = dnx_bool_value(v, animation_fade_out);
	else if (!strcasecmp(k, "animation_type_open")) { char *x = dnx_unquote_value(v); if (x) { free(animation_type_open); animation_type_open = x; } }
	else if (!strcasecmp(k, "animation_type_close")) { char *x = dnx_unquote_value(v); if (x) { free(animation_type_close); animation_type_close = x; } }
	else if (!strcasecmp(k, "zoom_initial_ratio")) zoom_initial_ratio = fminf(1.0f, fmaxf(0.01f, strtof(v, NULL)));
	else if (!strcasecmp(k, "zoom_end_ratio")) zoom_end_ratio = fminf(1.0f, fmaxf(0.01f, strtof(v, NULL)));
	else if (!strcasecmp(k, "fadein_begin_opacity")) fadein_begin_opacity = fminf(1.0f, fmaxf(0.0f, strtof(v, NULL)));
	else if (!strcasecmp(k, "fadeout_begin_opacity")) fadeout_begin_opacity = fminf(1.0f, fmaxf(0.0f, strtof(v, NULL)));
	else if (!strcasecmp(k, "animation_duration_move")) animation_duration_move = (uint32_t)MAX(1, MIN(50000, atoi(v)));
	else if (!strcasecmp(k, "animation_duration_open")) animation_duration_open = (uint32_t)MAX(1, MIN(50000, atoi(v)));
	else if (!strcasecmp(k, "animation_duration_close")) animation_duration_close = (uint32_t)MAX(1, MIN(50000, atoi(v)));
	else if (!strcasecmp(k, "animation_duration_focus")) animation_duration_focus = (uint32_t)MAX(0, MIN(50000, atoi(v)));
	else if (!strcasecmp(k, "animation_curve_move")) dnx_set_animation_curve(animation_curve_move, v);
	else if (!strcasecmp(k, "animation_curve_open")) dnx_set_animation_curve(animation_curve_open, v);
	else if (!strcasecmp(k, "animation_curve_close")) dnx_set_animation_curve(animation_curve_close, v);
	else if (!strcasecmp(k, "animation_curve_focus")) dnx_set_animation_curve(animation_curve_focus, v);
	else if (!strcasecmp(k, "animation_curve_opafadein")) dnx_set_animation_curve(animation_curve_opafadein, v);
	else if (!strcasecmp(k, "animation_curve_opafadeout")) dnx_set_animation_curve(animation_curve_opafadeout, v);
	else if (!strcasecmp(k, "animation_duration_tag")) animation_duration_tag = (uint32_t)MAX(0, MIN(50000, atoi(v)));
	else if (!strcasecmp(k, "animation_curve_tag")) dnx_set_animation_curve(animation_curve_tag, v);
	else if (!strcasecmp(k, "tag_animation_direction") || !strcasecmp(k, "tag_animation_duration")) {
		char *x = dnx_unquote_value(v);
		if (x) {
			if (!strcasecmp(x, "vertical")) tag_animation_vertical = 1;
			else if (!strcasecmp(x, "horizontal")) tag_animation_vertical = 0;
			free(x);
		}
	}

	/* dwindle */
	if (!strcasecmp(k, "dwindle_type")) {
		char *x = dnx_unquote_value(v);
		if (x) {
			if (!strcasecmp(x, "dwindle") || !strcasecmp(x, "spiral")) {
				free(dwindle_type);
				dwindle_type = x;
			} else {
				free(x);
			}
		}
	}
	else if (!strcasecmp(k, "dwindle_attach")) dwindle_attach = dnx_bool_value(v, dwindle_attach);
	else if (!strcasecmp(k, "dwindle_mirror")) dwindle_mirror = dnx_bool_value(v, dwindle_mirror);
	else if (!strcasecmp(k, "dwindle_mouse_client")) dwindle_mouse_client = dnx_bool_value(v, dwindle_mouse_client);

	/* appearance */
	if (!strcasecmp(k, "sloppyfocus")) sloppyfocus = atoi(v);
	else if (!strcasecmp(k, "bypass_surface_visibility")) bypass_surface_visibility = atoi(v);
	else if (!strcasecmp(k, "borderpx")) borderpx = dnx_nonnegative_uint(v);
	else if (!strcasecmp(k, "rootcolor")) dnx_set_hex_rgba(rootcolor, v);
	else if (!strcasecmp(k, "fullscreen_bg")) dnx_set_rgba(fullscreen_bg, v);
	else if (!strcasecmp(k, "scenefx_opacity_inactive") ||
			!strcasecmp(k, "scenefx_opacity_unfocus"))
		scenefx_opacity_inactive = fminf(1.0f, fmaxf(0.0f, strtof(v, NULL)));
	else if (!strcasecmp(k, "scenefx_opacity_active") ||
			!strcasecmp(k, "scenefx_opacity_focus"))
		scenefx_opacity_active = fminf(1.0f, fmaxf(0.0f, strtof(v, NULL)));
	else if (!strcasecmp(k, "scenefx_shadow")) scenefx_shadow = dnx_bool_value(v, scenefx_shadow);
	else if (!strcasecmp(k, "scenefx_shadow_only_floating")) scenefx_shadow_only_floating = dnx_bool_value(v, scenefx_shadow_only_floating);
	else if (!strcasecmp(k, "scenefx_shadow_color")) dnx_set_hex_rgba(scenefx_shadow_color, v);
	else if (!strcasecmp(k, "scenefx_shadow_color_focus")) dnx_set_hex_rgba(scenefx_shadow_color_focus, v);
	else if (!strcasecmp(k, "scenefx_shadow_blur_sigma")) scenefx_shadow_blur_sigma = MAX(0, atoi(v));
	else if (!strcasecmp(k, "scenefx_shadow_blur_sigma_focus")) scenefx_shadow_blur_sigma_focus = MAX(0, atoi(v));
	else if (!strcasecmp(k, "scenefx_corner_radius")) scenefx_corner_radius = MAX(0, atoi(v));
	else if (!strcasecmp(k, "scenefx_corner_radius_only_floating")) scenefx_corner_radius_only_floating = dnx_bool_value(v, scenefx_corner_radius_only_floating);
	else if (!strcasecmp(k, "log_level")) {
		if (!strcasecmp(v, "WLR_ERROR")) log_level = WLR_ERROR;
		else if (!strcasecmp(v, "WLR_DEBUG")) log_level = WLR_DEBUG;
		else if (!strcasecmp(v, "WLR_INFO")) log_level = WLR_INFO;
		else if (!strcasecmp(v, "WLR_SILENT")) log_level = WLR_SILENT;
		else log_level = atoi(v);
	}

	/* modifier key */
	else if (!strcasecmp(k, "modkey")) {
		if (!strcasecmp(v, "alt"))
			runtime_modkey = WLR_MODIFIER_ALT;
		else
			runtime_modkey = WLR_MODIFIER_LOGO;
	}

	/* gaps */
	else if (!strcasecmp(k, "gappx")) gappx = atoi(v);
	else if (!strcasecmp(k, "smartgaps")) smartgaps = atoi(v);
	else if (!strcasecmp(k, "gaps")) gaps = atoi(v);

	/* blur */
	else if (!strcasecmp(k, "blur")) scenefx_blur = dnx_bool_value(v, scenefx_blur);
	else if (!strcasecmp(k, "blur_xray")) scenefx_blur_xray = dnx_bool_value(v, scenefx_blur_xray);
	else if (!strcasecmp(k, "blur_ignore_transparent")) scenefx_blur_ignore_transparent = dnx_bool_value(v, scenefx_blur_ignore_transparent);
	else if (!strcasecmp(k, "blur_radius")) scenefx_blur_radius = MAX(0, atoi(v));
	else if (!strcasecmp(k, "blur_num_passes")) scenefx_blur_num_passes = MAX(1, atoi(v));
	else if (!strcasecmp(k, "blur_noise")) scenefx_blur_noise = strtof(v, NULL);
	else if (!strcasecmp(k, "blur_brightness")) scenefx_blur_brightness = strtof(v, NULL);
	else if (!strcasecmp(k, "blur_contrast")) scenefx_blur_contrast = strtof(v, NULL);
	else if (!strcasecmp(k, "blur_saturation")) scenefx_blur_saturation = strtof(v, NULL);

	/* borders */
	else if (!strcasecmp(k, "urgentcolor")) dnx_set_hex_rgba(urgentcolor, v);
	else if (!strcasecmp(k, "bordercolor")) dnx_set_hex_rgba(bordercolor, v);
	else if (!strcasecmp(k, "focuscolor")) dnx_set_hex_rgba(focuscolor, v);
	else if (!strcasecmp(k, "borders_focuscolor")) dnx_set_hex_rgba(borders_focuscolor, v);
	else if (!strcasecmp(k, "borders_urgentcolor")) dnx_set_hex_rgba(borders_urgentcolor, v);
	else if (!strcasecmp(k, "bordere_focuscolor")) dnx_set_hex_rgba(bordere_focuscolor, v);
	else if (!strcasecmp(k, "bordere_urgentcolor")) dnx_set_hex_rgba(bordere_urgentcolor, v);
	else if (!strcasecmp(k, "borderspx")) borderspx = dnx_nonnegative_uint(v);
	else if (!strcasecmp(k, "borderepx")) borderepx = dnx_nonnegative_uint(v);
	else if (!strcasecmp(k, "borderspx_offset")) borderspx_offset = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderepx_negative_offset")) borderepx_negative_offset = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderscolor")) dnx_set_hex_rgba(borderscolor, v);
	else if (!strcasecmp(k, "borderecolor")) dnx_set_hex_rgba(borderecolor, v);
	else if (!strcasecmp(k, "border_color_type")) {
		if (!strcasecmp(v, "BrdOriginal")) border_color_type = BrdOriginal;
		else if (!strcasecmp(v, "BrdStart")) border_color_type = BrdStart;
		else if (!strcasecmp(v, "BrdEnd")) border_color_type = BrdEnd;
		else if (!strcasecmp(v, "BrdStartEnd")) border_color_type = BrdStartEnd;
		else border_color_type = atoi(v);
	}
	else if (!strcasecmp(k, "borders_only_floating")) borders_only_floating = atoi(v);

	/* input */
	else if (!strcasecmp(k, "keyboard_rules")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.rules=x; }
	else if (!strcasecmp(k, "keyboard_model")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.model=x; }
	else if (!strcasecmp(k, "keyboard_layout")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.layout=x; }
	else if (!strcasecmp(k, "keyboard_variant")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.variant=x; }
	else if (!strcasecmp(k, "keyboard_options")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.options=x; }
	else if (!strcasecmp(k, "repeat_rate")) repeat_rate = atoi(v);
	else if (!strcasecmp(k, "repeat_delay")) repeat_delay = atoi(v);
	else if (!strcasecmp(k, "tap_to_click")) tap_to_click = dnx_bool_value(v, tap_to_click);
	else if (!strcasecmp(k, "tap_and_drag")) tap_and_drag = dnx_bool_value(v, tap_and_drag);
	else if (!strcasecmp(k, "drag_lock")) drag_lock = dnx_bool_value(v, drag_lock);
	else if (!strcasecmp(k, "natural_scrolling")) natural_scrolling = dnx_bool_value(v, natural_scrolling);
	else if (!strcasecmp(k, "disable_while_typing")) disable_while_typing = dnx_bool_value(v, disable_while_typing);
	else if (!strcasecmp(k, "left_handed")) left_handed = dnx_bool_value(v, left_handed);
	else if (!strcasecmp(k, "middle_button_emulation")) middle_button_emulation = dnx_bool_value(v, middle_button_emulation);
	else if (!strcasecmp(k, "swipe_min_threshold")) swipe_min_threshold = (unsigned int)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "scroll_method")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_NO_SCROLL")) scroll_method=LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_2FG")) scroll_method=LIBINPUT_CONFIG_SCROLL_2FG;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_EDGE")) scroll_method=LIBINPUT_CONFIG_SCROLL_EDGE;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN")) scroll_method=LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
		else scroll_method=(enum libinput_config_scroll_method)atoi(v);
	}
	else if (!strcasecmp(k, "click_method")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_NONE")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_NONE;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
		else click_method=(enum libinput_config_click_method)atoi(v);
	}
	else if (!strcasecmp(k, "send_events_mode")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_ENABLED")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_DISABLED")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
		else send_events_mode=(uint32_t)strtoul(v,NULL,0);
	}
	else if (!strcasecmp(k, "accel_profile")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT")) accel_profile=LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE")) accel_profile=LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		else accel_profile=(enum libinput_config_accel_profile)atoi(v);
	}
	else if (!strcasecmp(k, "accel_speed")) accel_speed = strtod(v, NULL);
	else if (!strcasecmp(k, "button_map")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_TAP_MAP_LRM")) button_map=LIBINPUT_CONFIG_TAP_MAP_LRM;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_TAP_MAP_LMR")) button_map=LIBINPUT_CONFIG_TAP_MAP_LMR;
		else button_map=(enum libinput_config_tap_button_map)atoi(v);
	}
	else if (!strcasecmp(k, "monitor_name")) monrules[0].name = dnx_is_null(v) ? NULL : dnx_unquote_value(v);
	else if (!strcasecmp(k, "mfact")) monrules[0].mfact = strtof(v,NULL);
	else if (!strcasecmp(k, "nmaster")) monrules[0].nmaster = atoi(v);
	else if (!strcasecmp(k, "scale")) monrules[0].scale = strtof(v,NULL);
	else if (!strcasecmp(k, "layout")) {
		int li = dnx_layout_index(v);
		if (li >= 0) monrules[0].lt = &layouts[li];
	}
	else if (!strcasecmp(k, "transform")) monrules[0].rr = dnx_transform(v);
	else if (!strcasecmp(k, "x")) monrules[0].x = atoi(v);
	else if (!strcasecmp(k, "y")) monrules[0].y = atoi(v);

#undef I
#undef B
#undef F
}

static void
dnx_list_append(char **dst, size_t cap, size_t *n, const DnxEntry *e)
{
	char *value;

	if (!e || !e->count || *n + 1 >= cap)
		return;
	value = dnx_strdup(e->items[0]);
	if (!value)
		return;
	dst[*n] = value;
	(*n)++;
	dst[*n] = NULL;
}

static void
dnx_autostart_append(const DnxEntry *e)
{
	DnxAutostartCommand *cmd;

	if (!e || e->type != DNX_TABLE || e->count != 1 || !e->items[0] || !*e->items[0])
		return;

	if (autostart_cmds_len >= autostart_cmds_cap)
		autostart_cmds = dnx_realloc_array(autostart_cmds, &autostart_cmds_cap,
				autostart_cmds_len + 1, sizeof(*autostart_cmds));

	cmd = &autostart_cmds[autostart_cmds_len++];
	memset(cmd, 0, sizeof(*cmd));
	cmd->command = dnx_strdup(e->items[0]);
	if (!cmd->command)
		dnx_oom();
}

static void
dnx_autostart_clear(void)
{
	size_t i;

	for (i = 0; i < autostart_cmds_len; i++)
		free(autostart_cmds[i].command);
	free(autostart_cmds);
	autostart_cmds = NULL;
	autostart_cmds_len = autostart_cmds_cap = 0;
}

#endif
