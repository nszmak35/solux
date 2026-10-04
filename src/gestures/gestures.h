/* Touchpad gestures: swipe and pinch. */
#ifndef SOLUX_GESTURES_GESTURES_H
#define SOLUX_GESTURES_GESTURES_H

static int
ongesture(struct wlr_pointer_swipe_end_event *event)
{
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Gesture *g;
	unsigned int motion;
	unsigned int adx, ady;

	if (event->cancelled)
		return 0;

	adx = (unsigned int)round(fabs(swipe_dx));
	ady = (unsigned int)round(fabs(swipe_dy));
	if ((uint64_t)adx * adx + (uint64_t)ady * ady <
			(uint64_t)swipe_min_threshold * swipe_min_threshold)
		return 0;

	if (adx > ady)
		motion = swipe_dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
	else
		motion = swipe_dy < 0 ? SWIPE_UP : SWIPE_DOWN;

	keyboard = wlr_seat_get_keyboard(seat);
	mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

	for (g = gestures; g < gestures + gestures_len; g++) {
		uint32_t want = g->mod;

		/* MODKEY is a symbolic modifier. Resolve it exactly like key and
		 * button bindings, so gestures also work with modkey=alt. */
		if (want & MODKEY)
			want = (want & ~MODKEY) | runtime_modkey;

		if (CLEANMASK(mods) == CLEANMASK(want) &&
			swipe_fingers == g->fingers_count &&
			motion == g->motion && g->func) {
			g->func(&g->arg);
			return 1;
		}
	}

	return 0;
}

static void
swipe_begin(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_swipe_begin_event *event = data;

	swipe_fingers = event->fingers;
	swipe_dx = 0;
	swipe_dy = 0;

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_begin(
			pointer_gestures, seat, event->time_msec, event->fingers);
}

static void
swipe_update(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_swipe_update_event *event = data;

	swipe_fingers = event->fingers;
	swipe_dx += event->dx;
	swipe_dy += event->dy;

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_update(
			pointer_gestures, seat, event->time_msec, event->dx, event->dy);
}

static void
swipe_end(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_swipe_end_event *event = data;

	ongesture(event);

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_end(
			pointer_gestures, seat, event->time_msec, event->cancelled);
}

static void
pinch_begin(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_begin_event *event = data;

	pinch_fingers = event->fingers;
	pinch_scale = 1.0;

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_begin(
			pointer_gestures, seat, event->time_msec, event->fingers);
}

static void
pinch_update(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_update_event *event = data;

	pinch_scale = event->scale;

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_update(
			pointer_gestures, seat, event->time_msec, event->dx,
			event->dy, event->scale, event->rotation);
}

static void
pinch_end(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_end_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Gesture *g;
	unsigned int motion;

	if (!event->cancelled) {
		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

		if (pinch_scale > 1.05)
			motion = TZOOM_IN;
		else if (pinch_scale < 0.95)
			motion = TZOOM_OUT;
		else
			motion = (unsigned int)-1;

		if (motion != (unsigned int)-1) {
			for (g = gestures; g < gestures + gestures_len; g++) {
				uint32_t want = g->mod;

				if (want & MODKEY)
					want = (want & ~MODKEY) | runtime_modkey;

				if (CLEANMASK(mods) == CLEANMASK(want) &&
					g->motion == motion &&
					pinch_fingers == g->fingers_count &&
					g->func) {
					g->func(&g->arg);
					break;
				}
			}
		}
	}

	if (pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_end(
			pointer_gestures, seat, event->time_msec, event->cancelled);
}

#endif
