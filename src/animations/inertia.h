/*
 * Inertia: after a window or the canvas is flicked with the mouse it keeps
 * gliding and slowly comes to rest.
 *
 * The release velocity is estimated from the last pointer samples (screen
 * pixels per second).  A glide is a plain exponential decay driven by the
 * frame clock, so it is smooth and independent of the refresh rate:
 *
 *     travelled = v0 * tau * (1 - exp(-t / tau))
 *
 * Windows are moved in world coordinates (screen distance / zoom); the canvas
 * glides by moving the camera in the direction of the flick.
 */
#ifndef SOLUX_INERTIA
#define SOLUX_INERTIA 1            /* 0 = windows and canvas stop when released */
#endif
#define SOLUX_INERTIA_TAU        0.4   /* decay time constant (s): larger = longer glide */
#define SOLUX_INERTIA_GAIN       0.6   /* multiplier for the measured release velocity */
#define SOLUX_INERTIA_MAXSPEED   2400  /* px/s */
#define SOLUX_INERTIA_MINSTART   150   /* release speed (px/s) below which no glide starts */
#define SOLUX_INERTIA_MINSPEED   20    /* speed (px/s) below which the glide stops */
#define SOLUX_INERTIA_WINDOW_MS  100   /* only samples of the last N ms count */
#define SOLUX_INERTIA_STALE_MS   100   /* no glide if the pointer rested this long before release */
#define SOLUX_INERTIA_MAXDT      0.05  /* longest time step (s), keeps a hiccup from jumping */

typedef struct {
	double x, y;
	uint64_t ms;
} SoluxInertiaSample;

static SoluxInertiaSample solux_inertia_samples[32];
static int solux_inertia_nsamples;

/* Canvas glide state (one at a time: the camera of one monitor). */
static Monitor *solux_cin_mon;
static double solux_cin_vx, solux_cin_vy;   /* screen px/s */
static double solux_cin_x, solux_cin_y;     /* camera as we left it */
static double solux_cin_zoom;
static uint64_t solux_cin_ms;

static void
solux_inertia_reset(void)
{
	solux_inertia_nsamples = 0;
	solux_cin_mon = NULL;
}

static void
solux_inertia_sample(double x, double y)
{
	int n = solux_inertia_nsamples;

	if (n > 0 && solux_inertia_samples[n - 1].x == x && solux_inertia_samples[n - 1].y == y)
		return;
	if (n == (int)(sizeof(solux_inertia_samples) / sizeof(solux_inertia_samples[0]))) {
		memmove(&solux_inertia_samples[0], &solux_inertia_samples[1],
			sizeof(solux_inertia_samples[0]) * (size_t)(n - 1));
		n--;
	}
	solux_inertia_samples[n].x = x;
	solux_inertia_samples[n].y = y;
	solux_inertia_samples[n].ms = solux_animation_now_ms();
	solux_inertia_nsamples = n + 1;
}

/* Release velocity in screen px/s; false when the pointer was not flicked. */
static bool
solux_inertia_velocity(double *ovx, double *ovy)
{
	uint64_t now;
	int i, first = -1, last = solux_inertia_nsamples - 1;
	double dt, vx, vy, sp;

	if (solux_inertia_nsamples < 2)
		return false;
	now = solux_animation_now_ms();
	if (now < solux_inertia_samples[last].ms ||
			now - solux_inertia_samples[last].ms > SOLUX_INERTIA_STALE_MS)
		return false;
	for (i = 0; i < last; i++) {
		if (now >= solux_inertia_samples[i].ms &&
				now - solux_inertia_samples[i].ms <= SOLUX_INERTIA_WINDOW_MS) {
			first = i;
			break;
		}
	}
	if (first < 0)
		return false;
	dt = (double)(solux_inertia_samples[last].ms - solux_inertia_samples[first].ms) / 1000.0;
	if (dt < 0.008)
		return false;
	vx = (solux_inertia_samples[last].x - solux_inertia_samples[first].x) / dt * SOLUX_INERTIA_GAIN;
	vy = (solux_inertia_samples[last].y - solux_inertia_samples[first].y) / dt * SOLUX_INERTIA_GAIN;
	sp = hypot(vx, vy);
	if (sp < SOLUX_INERTIA_MINSTART)
		return false;
	if (sp > SOLUX_INERTIA_MAXSPEED) {
		vx *= SOLUX_INERTIA_MAXSPEED / sp;
		vy *= SOLUX_INERTIA_MAXSPEED / sp;
	}
	*ovx = vx;
	*ovy = vy;
	return true;
}

/* Inertia is an animation: it follows the global `animations` switch and the
 * per-window no-animation rule, exactly like every other animation. */
static bool
solux_inertia_enabled(void)
{
	return SOLUX_INERTIA && animations;
}

/* A floating window was let go while moving: start gliding. */
static void
solux_inertia_release_client(Client *c)
{
	double vx, vy;

	if (!solux_inertia_enabled() || !c || !c->mon || !c->scene || !c->isfloating ||
			c->isfullscreen || c->isnoanimation || client_is_unmanaged(c))
		return;
	if (!solux_inertia_velocity(&vx, &vy))
		return;
	c->ivx = vx;
	c->ivy = vy;
	c->irx = c->iry = 0.0;
	c->inertia_ms = solux_animation_now_ms();
	c->inertia = true;
	wlr_output_schedule_frame(c->mon->wlr_output);
}

/* The canvas was let go while being dragged: start gliding. */
static void
solux_inertia_release_canvas(Monitor *m)
{
	double vx, vy;

	if (!solux_inertia_enabled() || !m || !solux_canvas_active(m))
		return;
	if (!solux_inertia_velocity(&vx, &vy))
		return;
	solux_cin_mon = m;
	solux_cin_vx = vx;
	solux_cin_vy = vy;
	solux_cin_x = m->canvas_x;
	solux_cin_y = m->canvas_y;
	solux_cin_zoom = m->canvas_zoom;
	solux_cin_ms = solux_animation_now_ms();
	wlr_output_schedule_frame(m->wlr_output);
}

/* Time step and the travelled distance fraction for one frame. */
static double
solux_inertia_advance(uint64_t *last_ms, double *vx, double *vy, double *dx, double *dy)
{
	uint64_t now = solux_animation_now_ms();
	double dt = now > *last_ms ? (double)(now - *last_ms) / 1000.0 : 0.0;
	double decay;

	*last_ms = now;
	if (dt > SOLUX_INERTIA_MAXDT)
		dt = SOLUX_INERTIA_MAXDT;
	decay = exp(-dt / SOLUX_INERTIA_TAU);
	*dx = *vx * SOLUX_INERTIA_TAU * (1.0 - decay);
	*dy = *vy * SOLUX_INERTIA_TAU * (1.0 - decay);
	*vx *= decay;
	*vy *= decay;
	return dt;
}

static void
solux_inertia_step(Monitor *m)
{
	Client *c;
	bool active = false;

	if (!m)
		return;

	/* Animations switched off (also at run time by a config reload): drop any
	 * glide that is still running and leave everything where it is. */
	if (!solux_inertia_enabled()) {
		if (solux_cin_mon == m)
			solux_cin_mon = NULL;
		wl_list_for_each(c, &clients, link)
			if (c->mon == m)
				c->inertia = false;
		return;
	}

	/* Windows. */
	wl_list_for_each(c, &clients, link) {
		double dx, dy, z = 1.0;
		int mx, my;

		if (!c->inertia || c->mon != m)
			continue;
		if (c == grabc || !c->isfloating || c->isfullscreen || c->isnoanimation || !VISIBLEON(c, m) ||
				!c->scene || client_is_unmanaged(c) || !client_surface(c) ||
				!client_surface(c)->mapped) {
			c->inertia = false;
			continue;
		}
		if (solux_canvas_active(m) && m->canvas_zoom > 0.01)
			z = MAX(0.3, m->canvas_zoom);

		solux_inertia_advance(&c->inertia_ms, &c->ivx, &c->ivy, &dx, &dy);
		dx = dx / z + c->irx;
		dy = dy / z + c->iry;
		mx = (int)dx;
		my = (int)dy;
		c->irx = dx - mx;
		c->iry = dy - my;

		if (hypot(c->ivx, c->ivy) < SOLUX_INERTIA_MINSPEED)
			c->inertia = false;
		if (mx || my)
			resize(c, (struct wlr_box){.x = c->geom.x + mx, .y = c->geom.y + my,
				.width = c->geom.width, .height = c->geom.height}, 1);
		if (c->inertia)
			active = true;
	}

	/* Canvas. */
	if (solux_cin_mon == m) {
		double dx, dy, z;

		if (!solux_canvas_active(m) || m->canvas_animating || cursor_mode == CurCanvasMove ||
				fabs(m->canvas_zoom - solux_cin_zoom) > 1e-9 ||
				fabs(m->canvas_x - solux_cin_x) > 1e-6 ||
				fabs(m->canvas_y - solux_cin_y) > 1e-6) {
			/* Something else moved the camera: let it have it. */
			solux_cin_mon = NULL;
		} else {
			z = MAX(0.3, m->canvas_zoom);
			solux_inertia_advance(&solux_cin_ms, &solux_cin_vx, &solux_cin_vy, &dx, &dy);
			/* The content follows the flick, so the camera goes the other way. */
			m->canvas_x = m->canvas_target_x = m->canvas_x - dx / z;
			m->canvas_y = m->canvas_target_y = m->canvas_y - dy / z;
			solux_cin_x = m->canvas_x;
			solux_cin_y = m->canvas_y;
			if (hypot(solux_cin_vx, solux_cin_vy) < SOLUX_INERTIA_MINSPEED)
				solux_cin_mon = NULL;
			else
				active = true;
		}
	}

	if (active)
		wlr_output_schedule_frame(m->wlr_output);
}
