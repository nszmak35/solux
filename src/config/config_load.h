/* Configuration loading: Python entries, built-in keys, load and reload. */
#ifndef SOLUX_CONFIG_CONFIG_LOAD_H
#define SOLUX_CONFIG_CONFIG_LOAD_H

static size_t dnx_tag_n;
static int dnx_user_list_started_tags;
static int dnx_user_table_started[6];
static int dnx_user_autostart_started;
static size_t dnx_rules_n, dnx_layouts_n, dnx_monrules_n, dnx_keys_n, dnx_buttons_n, dnx_gestures_n;

static void
dnx_reset_parser_state(void)
{
	dnx_tag_n = 0;
	dnx_user_list_started_tags = 0;
	memset(dnx_user_table_started, 0, sizeof(dnx_user_table_started));
	dnx_user_autostart_started = 0;
	dnx_rules_n = dnx_layouts_n = dnx_monrules_n = dnx_keys_n = dnx_buttons_n = dnx_gestures_n = 0;
}

static int
dnx_count_user_entries_cb(const DnxEntry *e, void *userdata)
{
	size_t *count = userdata;

	if (e->source == 1)
		(*count)++;
	return 0;
}

static int
dnx_entry_cb(const DnxEntry *e, void *userdata)
{
	(void)userdata;

	if (e->type == DNX_SCALAR) {
		dnx_load_scalar(e);
		/* Keep invalid negative/wrapped border sizes out of rendering code. */
		dnx_sanitize_border_widths();
		return 0;
	}

	if (e->type == DNX_LIST) {
		if (!strcasecmp(e->key, "tags")) {
			if (e->source == 1 && !dnx_user_list_started_tags) {
				size_t ti;
				for (ti = 0; ti < dnx_tags_owned; ti++)
					free(tags[ti]);
				for (ti = 0; ti <= MAX_TAGS; ti++)
					tags[ti] = NULL;
				dnx_tags_owned = 0;
				dnx_tag_n = 0;
				dnx_user_list_started_tags = 1;
			}
			dnx_list_append(tags, MAX_TAGS + 1, &dnx_tag_n, e);
			if (dnx_tag_n > dnx_tags_owned)
				dnx_tags_owned = dnx_tag_n;
		}
		return 0;
	}

	if (e->type != DNX_TABLE)
		return 0;

	if (!strcasecmp(e->key, "autostart") && e->count >= 1) {
		if (e->source == 1 && !dnx_user_autostart_started) {
			dnx_autostart_clear();
			dnx_user_autostart_started = 1;
		}
		dnx_autostart_append(e);
	} else if (!strcasecmp(e->key, "rules") && e->count >= 5) {
		if (e->source == 1 && !dnx_user_table_started[0]) {
			dnx_rules_n = 0; rules_len = 0; dnx_user_table_started[0] = 1;
		}
		if (dnx_rules_n >= rules_cap) rules = dnx_realloc_array(rules, &rules_cap, dnx_rules_n + 1, sizeof(*rules));
		{
			Rule *rr = &rules[dnx_rules_n];
			memset(rr, 0, sizeof(*rr));
			rr->appid = dnx_is_null(e->items[0]) ? NULL : dnx_strdup(e->items[0]);
			rr->workspace = MAX(0, atoi(e->items[1]));
			rr->isfloating = dnx_bool_value(e->items[2], 0);
			rr->isfullscreen = dnx_bool_value(e->items[3], 0);
			rr->monitor = dnx_is_null(e->items[4]) ? NULL : dnx_strdup(e->items[4]);
			rules_len = ++dnx_rules_n;
		}
	} else if (!strcasecmp(e->key, "layouts") && e->count >= 2) {
		/* dnx_layouts_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[1]) {
			dnx_layouts_n = 0;
			layouts_len = 0;
			dnx_user_table_started[1] = 1;
		}
		if (dnx_layouts_n < LENGTH(layouts)) {
			if (layout_symbol_owned[dnx_layouts_n])
				free((char *)layouts[dnx_layouts_n].symbol);
			layouts[dnx_layouts_n].symbol = dnx_strdup(e->items[0]);
			layouts[dnx_layouts_n].arrange = dnx_arrange_for_name(e->items[1]);
			layout_symbol_owned[dnx_layouts_n] = 1;
			dnx_layouts_n++;
			layouts_len = dnx_layouts_n;
		}
	} else if ((!strcasecmp(e->key, "monrules") || !strcasecmp(e->key, "monitors")) && e->count >= 8) {
		/* dnx_monrules_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[2]) {
			dnx_monrules_n = 0; monrules_len = 0; dnx_user_table_started[2] = 1;
		}
		if (dnx_monrules_n >= monrules_cap) monrules = dnx_realloc_array(monrules, &monrules_cap, dnx_monrules_n + 1, sizeof(*monrules));
		{
			memset(&monrules[dnx_monrules_n], 0, sizeof(monrules[dnx_monrules_n]));
			monrules[dnx_monrules_n].name = dnx_is_null(e->items[0]) ? NULL : dnx_strdup(e->items[0]);
			monrules[dnx_monrules_n].mfact = strtof(e->items[1], NULL);
			monrules[dnx_monrules_n].nmaster = atoi(e->items[2]);
			monrules[dnx_monrules_n].scale = strtof(e->items[3], NULL);
			/* Keep the exact value from %monrules.  %layouts may appear later
			 * in config.py, so resolve the layout only after the whole config
			 * has been parsed. */
			monrules[dnx_monrules_n].ltname = dnx_is_null(e->items[4]) ? NULL : dnx_strdup(e->items[4]);
			{
				int li = dnx_layout_index(e->items[4]);
				monrules[dnx_monrules_n].lt = (li >= 0) ? &layouts[li] : &layouts[0];
			}
			monrules[dnx_monrules_n].rr = dnx_transform(e->items[5]);
			monrules[dnx_monrules_n].x = atoi(e->items[6]);
			monrules[dnx_monrules_n].y = atoi(e->items[7]);
			monrules_len = ++dnx_monrules_n;
		}
	} else if (!strcasecmp(e->key, "keys") && e->count >= 3) {
		/* dnx_keys_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[3]) {
			dnx_keys_n = 0; keys_len = 0; dnx_user_table_started[3] = 1;
		}
		if (dnx_keys_n >= keys_cap) keys = dnx_realloc_array(keys, &keys_cap, dnx_keys_n + 1, sizeof(*keys));
		{
			const char *arg = e->count >= 4 ? e->items[3] : NULL;
			const char *argtype = e->count >= 5 ? e->items[4] : NULL;
			memset(&keys[dnx_keys_n], 0, sizeof(keys[dnx_keys_n]));
			keys[dnx_keys_n].mod = dnx_mods(e->items[0]);
			keys[dnx_keys_n].keysym = dnx_keysym(e->items[1]);

			if (!strcasecmp(e->items[2], "spawn")) keys[dnx_keys_n].func = spawn;
			else if (!strcasecmp(e->items[2], "view")) keys[dnx_keys_n].func = view;
			else if (!strcasecmp(e->items[2], "toggleview")) keys[dnx_keys_n].func = toggleview;
			else if (!strcasecmp(e->items[2], "tag")) keys[dnx_keys_n].func = tag;
			else if (!strcasecmp(e->items[2], "toggletag")) keys[dnx_keys_n].func = toggletag;
			else if (!strcasecmp(e->items[2], "setlayout")) keys[dnx_keys_n].func = setlayout;
			else if (!strcasecmp(e->items[2], "setmfact")) keys[dnx_keys_n].func = setmfact;
			else if (!strcasecmp(e->items[2], "incnmaster")) keys[dnx_keys_n].func = incnmaster;
			else if (!strcasecmp(e->items[2], "focusstack")) keys[dnx_keys_n].func = focusstack;
			else if (!strcasecmp(e->items[2], "focusdir")) keys[dnx_keys_n].func = focusdir;
			else if (!strcasecmp(e->items[2], "swapdir")) keys[dnx_keys_n].func = swapdir;
			else if (!strcasecmp(e->items[2], "focusmon")) keys[dnx_keys_n].func = focusmon;
			else if (!strcasecmp(e->items[2], "tagmon")) keys[dnx_keys_n].func = tagmon;
			else if (!strcasecmp(e->items[2], "cyclelayout")) keys[dnx_keys_n].func = cyclelayout;
			else if (!strcasecmp(e->items[2], "cycletag")) keys[dnx_keys_n].func = cycletag;
			else if (!strcasecmp(e->items[2], "togglefloating")) keys[dnx_keys_n].func = togglefloating;
			else if (!strcasecmp(e->items[2], "togglefullscreen")) keys[dnx_keys_n].func = togglefullscreen;
			else if (!strcasecmp(e->items[2], "togglegaps")) keys[dnx_keys_n].func = togglegaps;
			else if (!strcasecmp(e->items[2], "setopacityunfocus")) keys[dnx_keys_n].func = setopacityunfocus;
			else if (!strcasecmp(e->items[2], "setopacityfocus")) keys[dnx_keys_n].func = setopacityfocus;
			else if (!strcasecmp(e->items[2], "killclient")) keys[dnx_keys_n].func = killclient;
			else if (!strcasecmp(e->items[2], "zoom")) keys[dnx_keys_n].func = zoom;
			else if (!strcasecmp(e->items[2], "quit")) keys[dnx_keys_n].func = quit;
			else if (!strcasecmp(e->items[2], "reload_config")) keys[dnx_keys_n].func = reload_config;
			else if (!strcasecmp(e->items[2], "chvt")) keys[dnx_keys_n].func = chvt;
			else if (!strcasecmp(e->items[2], "moveresize")) keys[dnx_keys_n].func = moveresize;
			else if (!strcasecmp(e->items[2], "zoomcanvas")) keys[dnx_keys_n].func = zoomcanvas;
			else if (!strcasecmp(e->items[2], "movecanvas")) keys[dnx_keys_n].func = movecanvas;
			else return 0;

			if (!arg || !*arg || !strcasecmp(arg, "NONE")) {
				keys[dnx_keys_n].arg.i = 0;
			} else if (!strcasecmp(e->items[2], "spawn")) {
				keys[dnx_keys_n].arg.v = dnx_strdup(arg);
				if (!keys[dnx_keys_n].arg.v) dnx_oom();
			} else if (!strcasecmp(e->items[2], "setlayout")) {
				int li = dnx_layout_index(arg);
				keys[dnx_keys_n].arg.v = (li >= 0) ? &layouts[li] : &layouts[0];
			} else if (argtype && !strcasecmp(argtype, "float")) {
				keys[dnx_keys_n].arg.f = strtof(arg, NULL);
			} else if (argtype && (!strcasecmp(argtype, "ui") || !strcasecmp(argtype, "uint"))) {
				keys[dnx_keys_n].arg.ui = (!strcasecmp(arg, "~0") || !strcasecmp(arg, "ALL"))
					? ~0u : (uint32_t)strtoul(arg, NULL, 0);
			} else if (argtype && !strcasecmp(argtype, "int")) {
				keys[dnx_keys_n].arg.i = atoi(arg);
			} else if (!strcasecmp(e->items[2], "focusdir") ||
					!strcasecmp(e->items[2], "swapdir") ||
					!strcasecmp(e->items[2], "view") ||
					!strcasecmp(e->items[2], "toggleview") ||
					!strcasecmp(e->items[2], "tag") ||
					!strcasecmp(e->items[2], "toggletag") ||
					!strcasecmp(e->items[2], "chvt")) {
				keys[dnx_keys_n].arg.ui = (!strcasecmp(arg, "~0") || !strcasecmp(arg, "ALL"))
					? ~0u : (uint32_t)strtoul(arg, NULL, 0);
			} else if (!strcasecmp(e->items[2], "focusmon") ||
					!strcasecmp(e->items[2], "tagmon") ||
					!strcasecmp(e->items[2], "incnmaster") ||
					!strcasecmp(e->items[2], "focusstack") ||
					!strcasecmp(e->items[2], "cyclelayout") ||
					!strcasecmp(e->items[2], "cycletag") ||
					!strcasecmp(e->items[2], "moveresize") ||
					!strcasecmp(e->items[2], "zoomcanvas") ||
					!strcasecmp(e->items[2], "movecanvas")) {
				keys[dnx_keys_n].arg.i = atoi(arg);
			} else {
				char *end;
				float f = strtof(arg, &end);
				if (*end == 'f' || *end == 'F')
					keys[dnx_keys_n].arg.f = f;
				else
					keys[dnx_keys_n].arg.i = atoi(arg);
			}
			keys_len = ++dnx_keys_n;
		}
	} else if (!strcasecmp(e->key, "buttons") && e->count >= 4) {
		/* dnx_buttons_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[4]) {
			dnx_buttons_n = 0; buttons_len = 0; dnx_user_table_started[4] = 1;
		}
		if (dnx_buttons_n >= buttons_cap) buttons = dnx_realloc_array(buttons, &buttons_cap, dnx_buttons_n + 1, sizeof(*buttons));
		{
			memset(&buttons[dnx_buttons_n], 0, sizeof(buttons[dnx_buttons_n]));
			if (!strcasecmp(e->items[0], "ClkClient")) buttons[dnx_buttons_n].click = ClkClient;
			else if (!strcasecmp(e->items[0], "ClkRoot")) buttons[dnx_buttons_n].click = ClkRoot;
			else return 0;

			buttons[dnx_buttons_n].mod = dnx_mods(e->items[1]);
			if (!strcasecmp(e->items[2], "BTN_LEFT")) buttons[dnx_buttons_n].button = BTN_LEFT;
			else if (!strcasecmp(e->items[2], "BTN_RIGHT")) buttons[dnx_buttons_n].button = BTN_RIGHT;
			else if (!strcasecmp(e->items[2], "BTN_MIDDLE")) buttons[dnx_buttons_n].button = BTN_MIDDLE;
			else if (!strcasecmp(e->items[2], "SCROLL_UP")) buttons[dnx_buttons_n].scroll = 1;
			else if (!strcasecmp(e->items[2], "SCROLL_DOWN")) buttons[dnx_buttons_n].scroll = -1;
			else buttons[dnx_buttons_n].button = (unsigned)strtoul(e->items[2], NULL, 0);

			if (!strcasecmp(e->items[3], "spawn")) buttons[dnx_buttons_n].func = spawn;
			else if (!strcasecmp(e->items[3], "view")) buttons[dnx_buttons_n].func = view;
			else if (!strcasecmp(e->items[3], "toggleview")) buttons[dnx_buttons_n].func = toggleview;
			else if (!strcasecmp(e->items[3], "tag")) buttons[dnx_buttons_n].func = tag;
			else if (!strcasecmp(e->items[3], "toggletag")) buttons[dnx_buttons_n].func = toggletag;
			else if (!strcasecmp(e->items[3], "setlayout")) buttons[dnx_buttons_n].func = setlayout;
			else if (!strcasecmp(e->items[3], "setmfact")) buttons[dnx_buttons_n].func = setmfact;
			else if (!strcasecmp(e->items[3], "incnmaster")) buttons[dnx_buttons_n].func = incnmaster;
			else if (!strcasecmp(e->items[3], "focusstack")) buttons[dnx_buttons_n].func = focusstack;
			else if (!strcasecmp(e->items[3], "focusdir")) buttons[dnx_buttons_n].func = focusdir;
			else if (!strcasecmp(e->items[3], "swapdir")) buttons[dnx_buttons_n].func = swapdir;
			else if (!strcasecmp(e->items[3], "focusmon")) buttons[dnx_buttons_n].func = focusmon;
			else if (!strcasecmp(e->items[3], "tagmon")) buttons[dnx_buttons_n].func = tagmon;
			else if (!strcasecmp(e->items[3], "cyclelayout")) buttons[dnx_buttons_n].func = cyclelayout;
			else if (!strcasecmp(e->items[3], "cycletag")) buttons[dnx_buttons_n].func = cycletag;
			else if (!strcasecmp(e->items[3], "togglefloating")) buttons[dnx_buttons_n].func = togglefloating;
			else if (!strcasecmp(e->items[3], "togglefullscreen")) buttons[dnx_buttons_n].func = togglefullscreen;
			else if (!strcasecmp(e->items[3], "togglegaps")) buttons[dnx_buttons_n].func = togglegaps;
			else if (!strcasecmp(e->items[3], "setopacityunfocus")) buttons[dnx_buttons_n].func = setopacityunfocus;
			else if (!strcasecmp(e->items[3], "setopacityfocus")) buttons[dnx_buttons_n].func = setopacityfocus;
			else if (!strcasecmp(e->items[3], "killclient")) buttons[dnx_buttons_n].func = killclient;
			else if (!strcasecmp(e->items[3], "zoom")) buttons[dnx_buttons_n].func = zoom;
			else if (!strcasecmp(e->items[3], "quit")) buttons[dnx_buttons_n].func = quit;
			else if (!strcasecmp(e->items[3], "reload_config")) buttons[dnx_buttons_n].func = reload_config;
			else if (!strcasecmp(e->items[3], "chvt")) buttons[dnx_buttons_n].func = chvt;
			else if (!strcasecmp(e->items[3], "moveresize")) buttons[dnx_buttons_n].func = moveresize;
			else if (!strcasecmp(e->items[3], "zoomcanvas")) buttons[dnx_buttons_n].func = zoomcanvas;
			else if (!strcasecmp(e->items[3], "movecanvas")) buttons[dnx_buttons_n].func = movecanvas;
			else if (!strcasecmp(e->items[3], "canvasdrag")) buttons[dnx_buttons_n].func = canvasdrag;
			else return 0;

			if (!e->count || e->count < 5 || !e->items[4] || !*e->items[4] || !strcasecmp(e->items[4], "NONE")) {
				buttons[dnx_buttons_n].arg.i = 0;
			} else if (!strcasecmp(e->items[3], "spawn")) {
				buttons[dnx_buttons_n].arg.v = dnx_strdup(e->items[4]);
				if (!buttons[dnx_buttons_n].arg.v) dnx_oom();
			} else if (!strcasecmp(e->items[3], "setlayout")) {
				int li = dnx_layout_index(e->items[4]);
				buttons[dnx_buttons_n].arg.v = (li >= 0) ? &layouts[li] : &layouts[0];
			} else if (!strcasecmp(e->items[3], "setmfact") ||
					!strcasecmp(e->items[3], "setopacityunfocus") ||
					!strcasecmp(e->items[3], "setopacityfocus")) {
				buttons[dnx_buttons_n].arg.f = strtof(e->items[4], NULL);
			} else if (!strcasecmp(e->items[3], "focusdir") ||
					!strcasecmp(e->items[3], "swapdir") ||
					!strcasecmp(e->items[3], "view") ||
					!strcasecmp(e->items[3], "toggleview") ||
					!strcasecmp(e->items[3], "tag") ||
					!strcasecmp(e->items[3], "toggletag") ||
					!strcasecmp(e->items[3], "chvt")) {
				buttons[dnx_buttons_n].arg.ui = (!strcasecmp(e->items[4], "~0") || !strcasecmp(e->items[4], "ALL")) ? ~0u : (uint32_t)strtoul(e->items[4], NULL, 0);
			} else {
				buttons[dnx_buttons_n].arg.i = atoi(e->items[4]);
			}
			buttons_len = ++dnx_buttons_n;
		}
	} else if (!strcasecmp(e->key, "gestures") && e->count >= 4) {
		/* Gesture entries: type, direction, fingers, action, [argument], [modifier]. */
		if (e->source == 1 && !dnx_user_table_started[5]) {
			dnx_gestures_n = 0;
			gestures_len = 0;
			dnx_user_table_started[5] = 1;
		}
		if (dnx_gestures_n >= gestures_cap)
			gestures = dnx_realloc_array(gestures, &gestures_cap, dnx_gestures_n + 1, sizeof(*gestures));
		{
			Gesture *g = &gestures[dnx_gestures_n];
			const char *action = e->items[3];
			const char *arg = e->count >= 5 ? e->items[4] : NULL;
			const char *mod = e->count >= 6 ? e->items[5] : NULL;
			memset(g, 0, sizeof(*g));

			if (!strcasecmp(e->items[0], "SWIPE")) {
				if (!strcasecmp(e->items[1], "left"))
					g->motion = SWIPE_LEFT;
				else if (!strcasecmp(e->items[1], "right"))
					g->motion = SWIPE_RIGHT;
				else if (!strcasecmp(e->items[1], "up"))
					g->motion = SWIPE_UP;
				else if (!strcasecmp(e->items[1], "down"))
					g->motion = SWIPE_DOWN;
				else
					return 0;
			} else if (!strcasecmp(e->items[0], "TZOOM")) {
				if (!strcasecmp(e->items[1], "in"))
					g->motion = TZOOM_IN;
				else if (!strcasecmp(e->items[1], "out"))
					g->motion = TZOOM_OUT;
				else
					return 0;
			} else {
				return 0;
			}

			g->fingers_count = (unsigned int)strtoul(e->items[2], NULL, 0);
			if (g->motion == TZOOM_IN || g->motion == TZOOM_OUT) {
				if (g->fingers_count < 2 || g->fingers_count > 5)
					return 0;
			}
			g->mod = mod ? dnx_mods(mod) : 0;

			if (!strcasecmp(action, "spawn"))
				g->func = spawn;
			else if (!strcasecmp(action, "view"))
				g->func = view;
			else if (!strcasecmp(action, "toggleview"))
				g->func = toggleview;
			else if (!strcasecmp(action, "tag"))
				g->func = tag;
			else if (!strcasecmp(action, "toggletag"))
				g->func = toggletag;
			else if (!strcasecmp(action, "setlayout"))
				g->func = setlayout;
			else if (!strcasecmp(action, "setmfact"))
				g->func = setmfact;
			else if (!strcasecmp(action, "incnmaster"))
				g->func = incnmaster;
			else if (!strcasecmp(action, "focusstack"))
				g->func = focusstack;
			else if (!strcasecmp(action, "focusdir"))
				g->func = focusdir;
			else if (!strcasecmp(action, "swapdir"))
				g->func = swapdir;
			else if (!strcasecmp(action, "focusmon"))
				g->func = focusmon;
			else if (!strcasecmp(action, "tagmon"))
				g->func = tagmon;
			else if (!strcasecmp(action, "cyclelayout"))
				g->func = cyclelayout;
			else if (!strcasecmp(action, "cycletag"))
				g->func = cycletag;
			else if (!strcasecmp(action, "togglefloating"))
				g->func = togglefloating;
			else if (!strcasecmp(action, "togglefullscreen"))
				g->func = togglefullscreen;
			else if (!strcasecmp(action, "togglegaps"))
				g->func = togglegaps;
			else if (!strcasecmp(action, "setopacityunfocus"))
				g->func = setopacityunfocus;
			else if (!strcasecmp(action, "setopacityfocus"))
				g->func = setopacityfocus;
			else if (!strcasecmp(action, "killclient"))
				g->func = killclient;
			else if (!strcasecmp(action, "zoom"))
				g->func = zoom;
			else if (!strcasecmp(action, "quit"))
				g->func = quit;
			else if (!strcasecmp(action, "reload_config"))
				g->func = reload_config;
			else if (!strcasecmp(action, "chvt"))
				g->func = chvt;
			else if (!strcasecmp(action, "moveresize"))
				g->func = moveresize;
			else if (!strcasecmp(action, "zoomcanvas"))
				g->func = zoomcanvas;
			else if (!strcasecmp(action, "movecanvas"))
				g->func = movecanvas;
			else
				return 0;

			if (!arg || !*arg || !strcasecmp(arg, "NONE")) {
				g->arg.i = 0;
			} else if (!strcasecmp(action, "spawn")) {
				g->arg.v = dnx_strdup(arg);
				if (!g->arg.v) dnx_oom();
			} else if (!strcasecmp(action, "setlayout")) {
				int li = dnx_layout_index(arg);
				g->arg.v = (li >= 0) ? &layouts[li] : &layouts[0];
			} else if (!strcasecmp(action, "focusdir") ||
					!strcasecmp(action, "swapdir") ||
					!strcasecmp(action, "view") ||
					!strcasecmp(action, "toggleview") ||
					!strcasecmp(action, "tag") ||
					!strcasecmp(action, "toggletag") ||
					!strcasecmp(action, "chvt")) {
				g->arg.ui = (!strcasecmp(arg, "~0") || !strcasecmp(arg, "ALL"))
					? ~0u : (uint32_t)strtoul(arg, NULL, 0);
			} else if (!strcasecmp(action, "focusmon") ||
					!strcasecmp(action, "tagmon") ||
					!strcasecmp(action, "incnmaster") ||
					!strcasecmp(action, "focusstack") ||
					!strcasecmp(action, "cyclelayout") ||
					!strcasecmp(action, "cycletag") ||
					!strcasecmp(action, "moveresize") ||
					!strcasecmp(action, "zoomcanvas") ||
					!strcasecmp(action, "movecanvas")) {
				g->arg.i = atoi(arg);
			} else if (!strcasecmp(action, "setmfact") ||
					!strcasecmp(action, "setopacityunfocus") ||
					!strcasecmp(action, "setopacityfocus")) {
				g->arg.f = strtof(arg, NULL);
			} else {
				g->arg.i = atoi(arg);
			}
			gestures_len = ++dnx_gestures_n;
		}
	}
	return 0;
}
static const char *
dnx_default_config_path(void)
{
	static char path[PATH_MAX];
	const char *env = getenv("SOLUX_PY_DEFAULT");
	char exe[PATH_MAX];
	ssize_t n;

	if (env && *env && access(env, R_OK) == 0)
		return env;
	if (access(DEF_CFG, R_OK) == 0)
		return DEF_CFG;
	if (access("/etc/solux/config.py", R_OK) == 0)
		return "/etc/solux/config.py";
	if (access("/usr/local/share/solux/default/config.py", R_OK) == 0)
		return "/usr/local/share/solux/default/config.py";
	if (access("/usr/share/solux/default/config.py", R_OK) == 0)
		return "/usr/share/solux/default/config.py";

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		char *slash;
		exe[n] = '\0';
		slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			snprintf(path, sizeof(path), "%s/default/config.py", exe);
			if (access(path, R_OK) == 0)
				return path;
			snprintf(path, sizeof(path), "%s/../default/config.py", exe);
			if (access(path, R_OK) == 0)
				return path;
		}
	}

	/* Keep the canonical path in the diagnostic even when it is absent. */
	return DEF_CFG;
}



static const Key builtin_tty_keys[] = {
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F1,  chvt, { .ui = 1  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F2,  chvt, { .ui = 2  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F3,  chvt, { .ui = 3  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F4,  chvt, { .ui = 4  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F5,  chvt, { .ui = 5  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F6,  chvt, { .ui = 6  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F7,  chvt, { .ui = 7  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F8,  chvt, { .ui = 8  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F9,  chvt, { .ui = 9  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F10, chvt, { .ui = 10 } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F11, chvt, { .ui = 11 } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F12, chvt, { .ui = 12 } },
};

static void
dnx_add_builtin_tty_keys(void)
{
	size_t i, j;

	for (i = 0; i < LENGTH(builtin_tty_keys); i++) {
		int overridden = 0;

		/* A config.py entry for the same combination overrides the
		 * built-in TTY binding, regardless of which command it uses. */
		for (j = 0; j < keys_len; j++) {
			if (keys[j].mod == builtin_tty_keys[i].mod &&
				keys[j].keysym == builtin_tty_keys[i].keysym) {
				overridden = 1;
				break;
			}
		}

		if (overridden)
			continue;

		if (keys_len >= keys_cap)
			keys = dnx_realloc_array(keys, &keys_cap, keys_len + 1, sizeof(*keys));
		keys[keys_len++] = builtin_tty_keys[i];
	}
}

static void
dnx_resolve_monrule_layouts(void)
{
	size_t i;

	for (i = 0; i < monrules_len; i++) {
		if (!monrules[i].ltname)
			continue;

		{
			int li = dnx_layout_index(monrules[i].ltname);
			monrules[i].lt = (li >= 0) ? &layouts[li] : &layouts[0];
		}
	}
}

static void
load_dnx_config(void)
{
	const char *defcfg = dnx_default_config_path();
	const char *usrcfg = dnx_config_override ? dnx_config_override : USR_CFG;
	char usercfg[PATH_MAX];
	const char *home;

	/* If the normal per-user config is missing, use the system default. */
	if (!dnx_config_override) {
		home = getenv("HOME");
		if (home && *home) {
			snprintf(usercfg, sizeof(usercfg), "%s/.config/solux/config.py", home);
			if (access(usercfg, R_OK) == 0)
				usrcfg = usercfg;
			else
				usrcfg = SOLUX_STANDARD_CFG;
		} else {
			usrcfg = SOLUX_STANDARD_CFG;
		}
	}

	dnx_animation_defaults();
	dnx_reset_tags();

	memcpy(layouts, default_layouts, sizeof(layouts));
	memset(layout_symbol_owned, 0, sizeof(layout_symbol_owned));
	layouts_len = LENGTH(default_layouts);
	dnx_reset_parser_state();
	dnx_init_dynamic_config();
	/* An explicitly supplied config that yields no DNX entries is treated as
	 * invalid.  Check it before applying anything, so the fallback does not
	 * leave partially loaded configuration behind. */
	if (dnx_config_override && strcmp(usrcfg, SOLUX_STANDARD_CFG) != 0) {
		size_t user_entries = 0;
		dnx_foreach_ff(defcfg, usrcfg, dnx_count_user_entries_cb, &user_entries);
		if (user_entries == 0) {
			fprintf(stderr,
					"solux: configuration file '%s' could not be parsed; using standard configuration '%s'\n",
					usrcfg, SOLUX_STANDARD_CFG);
			dnx_config_override = SOLUX_STANDARD_CFG;
			usrcfg = SOLUX_STANDARD_CFG;
		}
	}

	


	/* Second pass: apply the actual configuration. */
	int loaded = dnx_foreach_ff(defcfg, usrcfg, dnx_entry_cb, NULL);

	/* %monrules can legally appear before %layouts. Resolve its layout names
	 * now, after every %layouts entry from config.py has been loaded. */
	dnx_resolve_monrule_layouts();

	if (!loaded)
		fprintf(stderr, "solux: no config.py found (tried %s and %s); using built-in defaults\n",
			defcfg, usrcfg);

	tagcount = dnx_tag_n > 0 ? dnx_tag_n : 9;

	/* Keep the built-in TTY shortcuts as a fallback when config.py does
	 * not define those key combinations. */
	dnx_add_builtin_tty_keys();
}

static void
reload_config(const Arg *arg)
{
	Monitor *m;
	size_t i, j;
	const int old_dwindle_attach = dwindle_attach;
	const int old_dwindle_mirror = dwindle_mirror;
	const int old_dwindle_mouse_client = dwindle_mouse_client;
	const int old_gaps = gaps;
	const int old_gappx = gappx;
	const int old_smartgaps = smartgaps;
	const int old_dwindle_spiral = dwindle_type && !strcasecmp(dwindle_type, "spiral");
	bool animate_layout_reload;
	(void)arg;

	/* Validate the complete Python configuration before touching any live state.
	 * If Python reports a syntax/runtime error, the old configuration stays
	 * active and the reload is aborted atomically. */
	{
		const char *defcfg = dnx_default_config_path();
		const char *usrcfg = dnx_config_override ? dnx_config_override : USR_CFG;
		char usercfg[PATH_MAX];
		const char *home;

		if (!dnx_config_override) {
			home = getenv("HOME");
			if (home && *home) {
				snprintf(usercfg, sizeof(usercfg), "%s/.config/solux/config.py", home);
				if (access(usercfg, R_OK) == 0)
					usrcfg = usercfg;
				else
					usrcfg = SOLUX_STANDARD_CFG;
			} else {
				usrcfg = SOLUX_STANDARD_CFG;
			}
		}

		if (!dnx_validate_ff(defcfg, usrcfg)) {
			fprintf(stderr, "solux: config.py contains an error; reload aborted; current configuration kept\n");
			return;
		}
	}

	/* Autostart is configuration state; reloading must NOT execute it again. */
	dnx_autostart_clear();

	/* A configuration reload must apply layout changes immediately.  Do not
	 * let the normal move animation interpolate the new geometry. */
	{
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (!c->scene || !client_surface(c)->mapped || client_is_unmanaged(c))
				continue;
			c->isnoanimation = 1;
			c->animation.running = false;
			c->animation.current = c->animation.target = c->geom;
		}
	}

	/*
	 * Recreate the dynamic tables from their built-in defaults.  The parser
	 * then applies config.py on top of them exactly as it does at startup.
	 */
	if (rules) {
		for (i = 0; i < rules_len; i++) {
			free(rules[i].appid);
			free(rules[i].monitor);
		}
	}
	free(rules);
	if (keys) {
		for (i = 0; i < keys_len; i++)
			if (keys[i].func == spawn)
				free((void *)keys[i].arg.v);
	}
	if (buttons) {
		for (i = 0; i < buttons_len; i++)
			if (buttons[i].func == spawn)
				free((void *)buttons[i].arg.v);
	}
	if (gestures) {
		for (i = 0; i < gestures_len; i++)
			if (gestures[i].func == spawn)
				free((void *)gestures[i].arg.v);
	}
	if (monrules) {
		for (i = 0; i < monrules_len; i++)
			free(monrules[i].ltname);
	}
	free(monrules);
	free(keys);
	free(buttons);
	free(gestures);
	rules = NULL; monrules = NULL; keys = NULL; buttons = NULL; gestures = NULL;
	rules_len = rules_cap = 0;
	monrules_len = monrules_cap = 0;
	keys_len = keys_cap = 0;
	buttons_len = buttons_cap = 0;
	gestures_len = gestures_cap = 0;

	/* Reset XKB rules before parsing the new config. Without this, old
	 * keyboard_options may survive reload and get combined with the new one. */
	xkb_rules = (struct xkb_rule_names) {
		.layout = "us,ru",
		.options = "grp:win_space_toggle",
	};

	for (i = 0; i < LENGTH(layouts); i++) {
		if (layout_symbol_owned[i])
			free((char *)layouts[i].symbol);
	}
	memcpy(layouts, default_layouts, sizeof(layouts));
	memset(layout_symbol_owned, 0, sizeof(layout_symbol_owned));
	layouts_len = LENGTH(default_layouts);

	/* Reset every scalar setting to the compiled-in default so a value removed
	 * from config.py cannot survive a successful reload. */
	dnx_reset_scalar_config();

	load_dnx_config();

	/* The binary tree is meaningful only while mouse-dependent Dwindle is
	 * enabled.  If the mode itself changes, discard the old tree so the next
	 * Dwindle arrange rebuilds it from the currently visible clients instead of
	 * reusing a structure produced under the other layout path. */
	if (old_dwindle_mouse_client != dwindle_mouse_client) {
		wl_list_for_each(m, &mons, link) {
			for (i = 0; i <= TAGCOUNT; i++) {
				solux_dwindle_tree_free(m->pertag->dwindle_roots[i]);
				m->pertag->dwindle_roots[i] = NULL;
			}
		}
	}

	animate_layout_reload = old_dwindle_attach != dwindle_attach ||
		old_dwindle_mirror != dwindle_mirror ||
		old_dwindle_mouse_client != dwindle_mouse_client || old_dwindle_spiral !=
		(dwindle_type && !strcasecmp(dwindle_type, "spiral")) ||
		old_gaps != gaps || old_gappx != gappx || old_smartgaps != smartgaps;

	solux_animation_reload_runtime();
	update_scenefx_runtime();
	if (root_bg)
		wlr_scene_rect_set_color(root_bg, rootcolor);
	wlr_log_init(log_level, NULL);
	{
		Client *c;
		Client *focused;

		wl_list_for_each(c, &clients, link) {
			if (!client_surface(c)->mapped || !c->scene || client_is_unmanaged(c))
				continue;

			c->opacity_unfocus = scenefx_opacity_inactive;
			c->opacity_focus = scenefx_opacity_active;

			focused = c->mon ? focustop(c->mon) : NULL;
			c->opacity = (c == focused) ? c->opacity_focus : c->opacity_unfocus;
			wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				iter_xdg_scene_buffers_opacity, c);
		}
	}

	

	{
		Client *c;
		Client *focused = focustop(selmon);

		wl_list_for_each(c, &clients, link) {
			if (!client_surface(c)->mapped || !c->scene || client_is_unmanaged(c))
				continue;

			c->bw = c->isfullscreen ? 0 : borderpx;
			if (c->isfullscreen) {
				c->bws = 0;
				c->bwe = 0;
			} else if (borders_only_floating && !c->isfloating) {
				c->bws = 0;
				c->bwe = 0;
			} else {
				c->bws = borderspx;
				c->bwe = borderepx;
			}

			if (c->isurgent)
				setclientborderstate(c, BorderUrgent);
			else if (c == focused)
				setclientborderstate(c, BorderFocus);
			else
				setclientborderstate(c, BorderNormal);

			resize(c, c->geom, 0);
		}
	}

	/* Apply modkey and keyboard changes from config.py without restarting dwl. */
	apply_runtime_modkey();
	reload_runtime_keymap();
	reapply_pointer_config();

	/*
	 * A reload may remove or reorder %layouts.  Never leave a monitor or
	 * per-tag layout pointer referring to a layout outside the active list.
	 */
	if (!layouts_len)
		return;

	/*
	 * If the reload changes layout geometry (gaps or dwindle parameters), keep
	 * the geometry captured above alive while updatemons() performs its normal
	 * monitor/output refresh.  updatemons() calls arrange(), so enabling the
	 * animation path here makes that first arrange produce the real MOVE target
	 * instead of replacing the saved origin with the new geometry.
	 */
	/* Apply monitor rules to all existing outputs before recalculating geometry.
	 * A monitor rule can also change the active layout, master count or mfact;
	 * those are layout changes too and must use the same MOVE animation path. */
	wl_list_for_each(m, &mons, link) {
		const Layout *oldlt = m->lt[m->sellt];
		void (*oldarrange)(Monitor *) = oldlt ? oldlt->arrange : NULL;
		float oldmfact = m->mfact;
		int oldnmaster = m->nmaster;
		apply_monitor_rule(m);
		if (oldlt != m->lt[m->sellt] || oldmfact != m->mfact ||
			oldnmaster != m->nmaster ||
			oldarrange != m->lt[m->sellt]->arrange)
			animate_layout_reload = true;

		/* Any reload whose resulting monitor layout is tiled must return existing
		 * floating clients to the layout. Keep their current geometry as the
		 * animation origin so the transition is visible instead of jumping. */
		if (m->lt[m->sellt]->arrange) {
			Client *c;
			wl_list_for_each(c, &clients, link) {
				if (c->mon != m || !c->isfloating || c->isfullscreen)
					continue;
				if (animations) {
					c->animation.current = c->geom;
					animate_layout_reload = true;
				}
				c->isfloating = 0;
				if (borders_only_floating) {
					c->bws = 0;
					c->bwe = 0;
				}
				wlr_scene_node_reparent(&c->scene->node, layers[LyrTile]);
			}
		}
	}

	/* monrules can be the only reason the layout changed. Enable animation
	 * only after all monitor rules and float->tile conversions are complete. */
	if (animate_layout_reload) {
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (!c->scene || !client_surface(c)->mapped || client_is_unmanaged(c))
				continue;
			c->isnoanimation = 0;

		}
	}
	updatemons(NULL, NULL);

	wl_list_for_each(m, &mons, link) {
		Client *c;

		if (m->fullscreen_bg)
			wlr_scene_rect_set_color(m->fullscreen_bg, fullscreen_bg);

		/* Apply the current config.py gaps setting to existing monitors. */
		m->gaps = gaps;

		/* Apply a changed tag count to already-running monitor/client state. */
		m->tagset[0] &= TAGMASK;
		m->tagset[1] &= TAGMASK;
		if (!m->tagset[0])
			m->tagset[0] = 1;
		if (!m->tagset[1])
			m->tagset[1] = 1;

		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			c->tags &= TAGMASK;
			if (!c->tags)
				c->tags = m->tagset[m->seltags];
		}

		if (m->pertag) {
			if (m->pertag->curtag > TAGCOUNT)
				m->pertag->curtag = 1;
			if (m->pertag->prevtag > TAGCOUNT)
				m->pertag->prevtag = 1;
		}

		for (i = 0; i < 2; i++) {
			if (m->lt[i] < layouts || m->lt[i] >= layouts + layouts_len)
				m->lt[i] = &layouts[0];
		}
		if (m->pertag) {
			for (i = 0; i <= TAGCOUNT; i++) {
				for (j = 0; j < 2; j++) {
					if (m->pertag->ltidxs[i][j] < layouts ||
							m->pertag->ltidxs[i][j] >= layouts + layouts_len)
						m->pertag->ltidxs[i][j] = &layouts[0];
				}
				if (m->pertag->sellts[i] > 1)
					m->pertag->sellts[i] = 0;
			}
		}
		strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));
		if (animate_layout_reload) {
			solux_animation_relayout(m);
		} else {
			arrange(m);
		}
		dwl_ipc_output_printstatus(m);
		ext_workspace_printstatus(m);
	}

	/* Layout geometry has been installed as the animation target.  Restore
	 * normal animation behavior without cancelling the MOVE animations that
	 * were just started by solux_animation_relayout(). */
	{
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (!c->scene || !client_surface(c)->mapped || client_is_unmanaged(c))
				continue;
			c->isnoanimation = 0;
		}
	}
	fprintf(stderr, "solux: config.py reloaded\n");
}

static void
apply_runtime_modkey(void)
{
	/* MODKEY is resolved at match time; never rewrite bindings here. */
}

#endif
