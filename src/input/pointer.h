/* Pointer handling: devices, buttons, motion, constraints, cursor and interactive move/resize. */
#ifndef SOLUX_INPUT_POINTER_H
#define SOLUX_INPUT_POINTER_H

void
buttonpress(struct wl_listener *listener, void *data)
{
	unsigned int click;
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	click = ClkRoot;
	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	if (c)
		click = ClkClient;

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		/* Canvas panning is configured through the normal Python buttons table.
		 * Handle this special drag action before focusing/raising the client. */
		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? CLEANMASK(wlr_keyboard_get_modifiers(keyboard)) : 0;
		for (b = buttons; b < buttons + buttons_len; b++) {
			uint32_t want = b->mod;
			if (want & MODKEY)
				want = (want & ~MODKEY) | runtime_modkey;
			if (b->func == canvasdrag && CLEANMASK(mods) == CLEANMASK(want) &&
					event->button == b->button && click == b->click) {
				b->func(&b->arg);
				return;
			}
		}

		/* Change focus if the button was pressed over a client. */
		if (click == ClkClient && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < buttons + buttons_len; b++) {
			uint32_t want = b->mod;
			if (want & MODKEY)
				want = (want & ~MODKEY) | runtime_modkey;

			if (CLEANMASK(mods) == CLEANMASK(want) && event->button == b->button &&
					click == b->click && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (cursor_mode == CurCanvasMove) {
			cursor_mode = CurNormal;
			canvas_drag_mon = NULL;
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			return;
		}
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_axis_event *event = data;
	Client *c = NULL;
	unsigned int click = ClkRoot;
	uint32_t mods;
	struct wlr_keyboard *keyboard;
	Button *b;
	double delta;
	int direction;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL && !locked) {
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c)
			click = ClkClient;

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

		delta = event->delta;
		if (event->delta_discrete)
			delta = event->delta_discrete;

		if (event->delta_discrete) {
			direction = delta < 0 ? 1 : -1;
		} else {
			scroll_accum += delta;
			if (scroll_accum <= -10.0) {
				direction = 1;
				scroll_accum = 0;
			} else if (scroll_accum >= 10.0) {
				direction = -1;
				scroll_accum = 0;
			} else {
				goto forward_axis;
			}
		}

		/* Prefer an exact click target first. If a scroll binding is declared
		 * for ClkClient and the pointer is over empty space, allow it as a
		 * fallback so tag cycling does not stop on an empty tag. */
		for (b = buttons; b < buttons + buttons_len; b++) {
			uint32_t want = b->mod;

			if (!b->scroll || b->click != click)
				continue;

			if (want & MODKEY)
				want = (want & ~MODKEY) | runtime_modkey;

			if (CLEANMASK(mods) == CLEANMASK(want) &&
				b->scroll == direction && b->func) {
				b->func(&b->arg);
				return;
			}
		}

		if (click == ClkRoot) {
			for (b = buttons; b < buttons + buttons_len; b++) {
				uint32_t want = b->mod;

				if (!b->scroll || b->click != ClkClient)
					continue;

				if (want & MODKEY)
					want = (want & ~MODKEY) | runtime_modkey;

				if (CLEANMASK(mods) == CLEANMASK(want) &&
					b->scroll == direction && b->func) {
					b->func(&b->arg);
					return;
				}
			}
		}
	}

forward_axis:
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

static void
apply_pointer_config(struct wlr_pointer *pointer)
{
	struct libinput_device *device;

	if (!pointer)
		return;

	if (!wlr_input_device_is_libinput(&pointer->base)
			|| !(device = wlr_libinput_get_device_handle(&pointer->base)))
		return;

	if (libinput_device_config_tap_get_finger_count(device)) {
		libinput_device_config_tap_set_enabled(device, tap_to_click);
		libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
		libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
		libinput_device_config_tap_set_button_map(device, button_map);
	}

	if (libinput_device_config_scroll_has_natural_scroll(device))
		libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

	if (libinput_device_config_dwt_is_available(device))
		libinput_device_config_dwt_set_enabled(device, disable_while_typing);

	if (libinput_device_config_left_handed_is_available(device))
		libinput_device_config_left_handed_set(device, left_handed);

	if (libinput_device_config_middle_emulation_is_available(device))
		libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

	if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
		libinput_device_config_scroll_set_method(device, scroll_method);

	if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
		libinput_device_config_click_set_method(device, click_method);

	if (libinput_device_config_send_events_get_modes(device))
		libinput_device_config_send_events_set_mode(device, send_events_mode);

	if (libinput_device_config_accel_is_available(device)) {
		libinput_device_config_accel_set_profile(device, accel_profile);
		libinput_device_config_accel_set_speed(device, accel_speed);
	}
}

static void
reapply_pointer_config(void)
{
	PointerDevice *pd;

	wl_list_for_each(pd, &pointer_devices, link)
		apply_pointer_config(pd->pointer);
}

static void
destroy_pointer_config(struct wl_listener *listener, void *data)
{
	PointerDevice *pd = wl_container_of(listener, pd, destroy);

	(void)data;
	wl_list_remove(&pd->link);
	wl_list_remove(&pd->destroy.link);
	free(pd);
}

void
createpointer(struct wlr_pointer *pointer)
{
	PointerDevice *pd;

	apply_pointer_config(pointer);

	pd = ecalloc(1, sizeof(*pd));
	pd->pointer = pointer;
	LISTEN(&pointer->base.events.destroy, &pd->destroy, destroy_pointer_config);
	wl_list_insert(&pointer_devices, &pd->link);

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	

	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);
	if (c && surface == client_surface(c) && solux_canvas_active(c->mon))
		solux_canvas_surface_coords(c, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove &&
				cursor_mode != CurCanvasMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are panning the canvas, update camera coordinates directly so
	 * the canvas tracks the pointer without accumulating frame lag. */
	if (cursor_mode == CurCanvasMove && canvas_drag_mon && solux_canvas_active(canvas_drag_mon)) {
		double z = MAX(0.25, canvas_drag_mon->canvas_zoom);
		canvas_drag_mon->canvas_x = canvas_drag_mon->canvas_target_x =
			canvas_drag_start_x - (cursor->x - canvas_drag_cursor_x) / z;
		canvas_drag_mon->canvas_y = canvas_drag_mon->canvas_target_y =
			canvas_drag_start_y - (cursor->y - canvas_drag_cursor_y) / z;
		canvas_drag_mon->canvas_animating = false;
		solux_canvas_apply(canvas_drag_mon);
		wlr_output_schedule_frame(canvas_drag_mon->wlr_output);
		return;
	}

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		double wx, wy;
		solux_canvas_screen_to_world(grabc ? grabc->mon : NULL, cursor->x, cursor->y, &wx, &wy);
		resize(grabc, (struct wlr_box){.x = (int)lround(wx) - grabcx, .y = (int)lround(wy) - grabcy,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		return;
	} else if (cursor_mode == CurResize) {
		double wx, wy;
		solux_canvas_screen_to_world(grabc ? grabc->mon : NULL, cursor->x, cursor->y, &wx, &wy);
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)lround(wx) - grabc->geom.x, .height = (int)lround(wy) - grabc->geom.y}, 1);
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	

	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	/* Allow moving/resizing in a floating layout, and also allow explicitly
	 * floating clients inside tile/dwindle. Tiled clients in tile/dwindle
	 * remain protected. */
	if (!grabc->mon || grabc->isfullscreen ||
		(!grabc->isfloating && grabc->mon->lt[grabc->mon->sellt]->arrange))
		return;
	setfloating(grabc, 1);
	/* Keep the grabbed window above overlapping windows throughout the drag. */
	wlr_scene_node_raise_to_top(&grabc->scene->node);
	switch (cursor_mode = arg->ui) {
	case CurMove: {
		double wx, wy;
		solux_canvas_screen_to_world(grabc->mon, cursor->x, cursor->y, &wx, &wy);
		grabcx = (int)lround(wx) - grabc->geom.x;
		grabcy = (int)lround(wy) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	}
	case CurResize: {
		double sx, sy;
		if (solux_canvas_active(grabc->mon)) {
			struct wlr_box vg = solux_canvas_world_to_screen(grabc->mon, grabc->geom);
			sx = vg.x + vg.width; sy = vg.y + vg.height;
		} else {
			sx = grabc->geom.x + grabc->geom.width; sy = grabc->geom.y + grabc->geom.height;
		}
		wlr_cursor_warp_closest(cursor, NULL, sx, sy);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
	}
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	

	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	struct wlr_scene_surface *scene_surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			scene_surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (!scene_surface) continue;
			surface = scene_surface->surface;
		}
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
		if (c && c->taganim_role == TAGANIM_OUT) {
			c = NULL;
			surface = NULL;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

#endif
