/* Keyboard handling: keymaps, key bindings, key repeat and layout switching. */
#ifndef SOLUX_INPUT_KEYBOARD_H
#define SOLUX_INPUT_KEYBOARD_H

void
assignkeymap(struct wlr_keyboard *keyboard) {
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &xkb_rules,
					XKB_KEYMAP_COMPILE_NO_FLAGS);

	

	if (!keymap) {
		struct xkb_rule_names fallback = xkb_rules;
		fallback.layout = "us";
		keymap = xkb_keymap_new_from_names(context, &fallback,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	if (!keymap)
		die("failed to compile fallback keymap");

	wlr_keyboard_set_keymap(keyboard, keymap);

	/* Restore selected layout group after rebuilding keymap */
	if (keyboard->xkb_state && current_kblayout < xkb_keymap_num_layouts(keyboard->keymap))
		xkb_state_update_mask(keyboard->xkb_state,
			keyboard->modifiers.depressed,
			keyboard->modifiers.latched,
			keyboard->modifiers.locked,
			0, 0, current_kblayout);

	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
}

static void
reload_runtime_keymap(void)
{
	

	struct wlr_keyboard *keyboard;
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	xkb_layout_index_t group;
	uint32_t depressed, latched, locked;

	

	struct keyboard_group_device {
		struct wlr_keyboard *keyboard;
		struct wl_listener key;
		struct wl_listener modifiers;
		struct wl_listener keymap;
		struct wl_listener repeat_info;
		struct wl_listener destroy;
		struct wl_list link;
	} *device;

	if (!kb_group || !kb_group->wlr_group)
		return;

	keyboard = &kb_group->wlr_group->keyboard;
	if (!keyboard)
		return;

	

	group = 0;
	if (keyboard->xkb_state)
		group = xkb_state_serialize_layout(
			keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
	if (group == XKB_LAYOUT_INVALID)
		group = 0;

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context)
		die("failed to create xkb context");

	keymap = xkb_keymap_new_from_names(context, &xkb_rules,
			XKB_KEYMAP_COMPILE_NO_FLAGS);

	

	if (!keymap) {
		struct xkb_rule_names fallback = xkb_rules;
		fallback.layout = "us";
		keymap = xkb_keymap_new_from_names(context, &fallback,
				XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	if (!keymap)
		die("failed to compile fallback keymap");

	if (xkb_keymap_num_layouts(keymap) <= 0) {
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
		die("new keymap has no layouts");
	}

	if (group >= xkb_keymap_num_layouts(keymap))
		group = 0;

	/*
	 * Preserve only the ordinary modifier bits.  Do NOT copy the old XKB
	 * state object and do NOT use the old keymap anywhere.
	 */
	depressed = keyboard->modifiers.depressed;
	latched = keyboard->modifiers.latched;
	locked = keyboard->modifiers.locked;

	/*
	 * 1. Install the new keymap on the synthetic/group keyboard.
	 */
	wlr_keyboard_set_keymap(keyboard, keymap);

	

	wlr_keyboard_notify_modifiers(keyboard, depressed, latched, locked, 0);

	if (keyboard->xkb_state)
		xkb_state_update_mask(keyboard->xkb_state,
			depressed, latched, locked, 0, 0, group);

	keyboard->modifiers.group = group;

	wlr_keyboard_set_repeat_info(keyboard, repeat_rate, repeat_delay);

	

	wl_list_for_each(device, &kb_group->wlr_group->devices, link) {
		struct wlr_keyboard *tkb = device->keyboard;

		if (!tkb)
			continue;

		wlr_keyboard_set_keymap(tkb, keyboard->keymap);
		wlr_keyboard_notify_modifiers(tkb,
			depressed,
			latched,
			locked,
			0);
	}

	/*
	 * 4. The group keyboard is the keyboard exposed by the seat.
	 */
	wlr_seat_set_keyboard(seat, keyboard);
	wlr_seat_keyboard_notify_modifiers(seat, &keyboard->modifiers);

	current_kblayout = group;
	kblayout_idx = (unsigned int)-1;
	kblayout(kb_group);

	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);

	kblayout(kb_group);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	assignkeymap(&group->wlr_group->keyboard);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

void
incxkbrules(const Arg *arg)
{
	{
		unsigned int nlayouts = 1;
		char *p = xkb_rules.layout;
		if (p) {
			for (; *p; p++)
				if (*p == ',')
					nlayouts++;
		}
		if (nlayouts)
			current_kblayout = (current_kblayout + arg->i + nlayouts) % nlayouts;
	}
	assignkeymap(&kb_group->wlr_group->keyboard);

	/* This path changes the XKB group without a keyboard event. Publish it
	 * immediately so status consumers see the new layout even on the root. */
	kblayout(kb_group);
}

static void
kblayout_idle(void *data)
{
	KeyboardGroup *kb = data;
	if (kb && kb->wlr_group)
		kblayout(kb);
}

void
kblayout(KeyboardGroup *kb)
{
	unsigned int idx;
	const char *layout_name;
	char tmp[PATH_MAX];
	FILE *f;

	if (!kb || !kb->wlr_group || !kb->wlr_group->keyboard.keymap)
		return;

	/* Always use XKB's effective group. current_kblayout is only the group
	 * selected by dwl itself; Alt+Shift/Win+Space can change XKB directly. */
	idx = kb->wlr_group->keyboard.modifiers.group;
	if (idx >= xkb_keymap_num_layouts(kb->wlr_group->keyboard.keymap))
		idx = 0;

	kblayout_idx = idx;
	layout_name = xkb_keymap_layout_get_name(
		kb->wlr_group->keyboard.keymap, idx);

	/* Publish atomically so soluxctl never reads a truncated/empty file. */
	if (*kblayout_file &&
		snprintf(tmp, sizeof(tmp), "%s.tmp", kblayout_file) < (int)sizeof(tmp) &&
		(f = fopen(tmp, "w"))) {
		fprintf(f, "%s\n", layout_name ? layout_name : "unknown");
		if (fclose(f) == 0)
			rename(tmp, kblayout_file);
		else
			remove(tmp);
	}
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	

	const Key *k;
	for (k = keys; k < keys + keys_len; k++) {
		uint32_t want = k->mod;
		if (want & MODKEY)
			want = (want & ~MODKEY) | runtime_modkey;
		if (CLEANMASK(mods) == CLEANMASK(want)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	size_t i;
	int handled = 0;
	const xkb_keysym_t *syms;
	const xkb_keysym_t *base_syms;
	int nsyms, nbase_syms;
	xkb_layout_index_t layout;

	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	 
	uint32_t keycode = event->keycode + 8;

	 
	struct xkb_keymap *keymap = xkb_state_get_keymap(group->wlr_group->keyboard.xkb_state);

	 
	xkb_level_index_t level = xkb_state_key_get_level(group->wlr_group->keyboard.xkb_state, keycode, 0);

	 
	nsyms = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, level, &syms);
	nbase_syms = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, 0, &base_syms);

	uint32_t mods_pressed = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	 
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (locked) {
			 
		} else {
			 
			for (i = 0; i < keys_len; i++) {
				int match = 0;
				for (int s = 0; s < nsyms; s++) {
					if (syms[s] == keys[i].keysym) {
						match = 1;
						break;
					}
				}
				/* A shifted number key produces !, @, etc. XKB bindings
				 * commonly use the physical/base keysym (1, 2, ...).
				 * Keep SHIFT in the modifier comparison, but also accept
				 * the unshifted keysym so MODKEY|SHIFT,1 works. */
				if (!match && (mods_pressed & WLR_MODIFIER_SHIFT)) {
					for (int s = 0; s < nbase_syms; s++) {
						if (base_syms[s] == keys[i].keysym) {
							match = 1;
							break;
						}
					}
				}

				uint32_t want = keys[i].mod;
				if (want & MODKEY)
					want = (want & ~MODKEY) | runtime_modkey;

				if (match && CLEANMASK(want) == CLEANMASK(mods_pressed) && keys[i].func) {
					keys[i].func(&keys[i].arg);
					handled = 1;
					break; /* one physical key combination = one action */
				}
			}
		}
	}

	if (!handled) {
		 
		wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}

	

	wl_event_loop_add_idle(event_loop, kblayout_idle, group);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);

	/* XKB may update the effective group after this listener. Defer the
	 * read until the current event has completed. */
	wl_event_loop_add_idle(event_loop, kblayout_idle, group);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
setxkbrules(const Arg *arg)
{
	current_kblayout = arg->i;
	assignkeymap(&kb_group->wlr_group->keyboard);
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

#endif
