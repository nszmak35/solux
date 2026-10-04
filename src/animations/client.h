/* Client/window animations for Solux. Kept close to mangowc's animation path. */
#ifndef SOLUX_ANIMATION_CLIENT_H
#define SOLUX_ANIMATION_CLIENT_H

static const char *solux_client_animation_name(Client *c)
{
	if (!c)
		return "none";
	switch (c->animation.action) {
	case SOLUX_ANIM_OPEN: return animation_type_open;
	case SOLUX_ANIM_CLOSE: return animation_type_close;
	default: return "none";
	}
}

static struct fx_corner_radii solux_animation_corner_location(Client *c, struct wlr_box visual)
{
	struct fx_corner_radii r = corner_radii_all(c->corner_radius);
	if (!c || !c->mon || c->isfullscreen ||
		(scenefx_corner_radius_only_floating && !c->isfloating))
		return corner_radii_none();

	/* Match MangoWC: while a client is animated, only remove the corners
	 * which are actually touching the output edge.  Do not use {0} here;
	 * that disables rounding for the entire XWayland/XDG buffer. */
	if (visual.x + c->corner_radius <= c->mon->m.x) {
		r.top_left = 0;
		r.bottom_left = 0;
	}
	if (visual.x + visual.width - c->corner_radius >=
		c->mon->m.x + c->mon->m.width) {
		r.top_right = 0;
		r.bottom_right = 0;
	}
	if (visual.y + c->corner_radius <= c->mon->m.y) {
		r.top_left = 0;
		r.top_right = 0;
	}
	if (visual.y + visual.height - c->corner_radius >=
		c->mon->m.y + c->mon->m.height) {
		r.bottom_left = 0;
		r.bottom_right = 0;
	}
	return r;
}

static void solux_animation_apply_rect_borders(Client *c, struct wlr_box visual)
{
	int x, y, w, h;
	if (!c || !c->border || !c->borders || !c->bordere)
		return;

	/* When corner radius is zero the normal rectangular border nodes are used.
	 * Unlike SceneFX's rounded nodes, these are not part of the buffer scaling
	 * path, so keep their geometry synchronized with the animated visual box.
	 * Do not touch c->geom: it remains the layout's real target geometry. */
	wlr_scene_rect_set_size(c->border[0], MAX(0, visual.width), c->bw);
	wlr_scene_rect_set_size(c->border[1], MAX(0, visual.width), c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw,
		MAX(0, visual.height - 2 * (int)c->bw));
	wlr_scene_rect_set_size(c->border[3], c->bw,
		MAX(0, visual.height - 2 * (int)c->bw));
	wlr_scene_node_set_position(&c->border[0]->node, 0, 0);
	wlr_scene_node_set_position(&c->border[1]->node, 0,
		MAX(0, visual.height - (int)c->bw));
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node,
		MAX(0, visual.width - (int)c->bw), c->bw);

	x = (int)borderspx_offset;
	y = x;
	w = MAX(0, visual.width - 2 * x);
	h = MAX(0, visual.height - 2 * y);
	wlr_scene_rect_set_size(c->borders[0], w, c->bws);
	wlr_scene_rect_set_size(c->borders[1], w, c->bws);
	wlr_scene_rect_set_size(c->borders[2], c->bws,
		MAX(0, visual.height - 2 * (int)c->bws - 2 * (int)borderspx_offset));
	wlr_scene_rect_set_size(c->borders[3], c->bws,
		MAX(0, visual.height - 2 * (int)c->bws - 2 * (int)borderspx_offset));
	wlr_scene_node_set_position(&c->borders[0]->node, x, y);
	wlr_scene_node_set_position(&c->borders[1]->node, x,
		MAX(0, visual.height - (int)c->bws - y));
	wlr_scene_node_set_position(&c->borders[2]->node, x, c->bws + y);
	wlr_scene_node_set_position(&c->borders[3]->node,
		MAX(0, visual.width - (int)c->bws - x), c->bws + y);

	wlr_scene_rect_set_size(c->bordere[0],
		MAX(0, visual.width - ((int)c->bw - (int)c->bwe) * 2
			+ (int)borderepx_negative_offset * 2), c->bwe);
	wlr_scene_rect_set_size(c->bordere[1],
		MAX(0, visual.width - ((int)c->bw - (int)c->bwe) * 2
			+ (int)borderepx_negative_offset * 2), c->bwe);
	wlr_scene_rect_set_size(c->bordere[2], c->bwe,
		MAX(0, visual.height - 2 * (int)c->bw
			+ 2 * (int)borderepx_negative_offset));
	wlr_scene_rect_set_size(c->bordere[3], c->bwe,
		MAX(0, visual.height - 2 * (int)c->bw
			+ 2 * (int)borderepx_negative_offset));
	wlr_scene_node_set_position(&c->bordere[0]->node,
		c->bw - c->bwe - borderepx_negative_offset,
		c->bw - c->bwe - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[1]->node,
		c->bw - c->bwe - borderepx_negative_offset,
		MAX(0, visual.height - (int)c->bw + borderepx_negative_offset));
	wlr_scene_node_set_position(&c->bordere[2]->node,
		c->bw - c->bwe - borderepx_negative_offset,
		c->bw - c->bwe - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[3]->node,
		MAX(0, visual.width - (int)c->bw + borderepx_negative_offset),
		c->bw - c->bwe - borderepx_negative_offset);
}

static void solux_client_apply_open_clip(Client *c, struct wlr_box visual, double linear_progress)
{
	struct wlr_box geometry, clip;
	BufferData bd;
	int32_t width, height, surface_width, surface_height;

	if (!c || !c->scene_surface || !client_surface(c) ||
		!client_surface(c)->mapped)
		return;

	/* This is MangoWC's OPEN path: animate the scene geometry, then clip the
	 * actual client surface to that intermediate geometry.  In particular, the
	 * clip is what makes zoom OPEN look like Mango rather than simply resizing
	 * the Wayland buffer.  Popups/subsurfaces are still handled by the existing
	 * buffer callback, which deliberately does not resize them. */
	client_get_geometry(c, &geometry);
	width = MAX(1, visual.width - 2 * (int)c->bw);
	height = MAX(1, visual.height - 2 * (int)c->bw);
	clip = (struct wlr_box){
		.x = geometry.x,
		.y = geometry.y,
		.width = width,
		.height = height,
	};
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		clip.x = 0;
		clip.y = 0;
	}
#endif

	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);

	surface_width = MAX(1, geometry.width);
	surface_height = MAX(1, geometry.height);
	bd.width = clip.width;
	bd.height = clip.height;
	bd.corner_location = solux_animation_corner_location(c, visual);
	bd.should_scale = true;
	bd.root = NULL;
	bd.follow_root = false;
	if (linear_progress >= 1.0) {
		bd.width_scale = 1.0f;
		bd.height_scale = 1.0f;
	} else {
		bd.width_scale = (float)bd.width / (float)surface_width;
		bd.height_scale = (float)bd.height / (float)surface_height;
	}
	wlr_scene_node_for_each_buffer(&c->scene_surface->node,
			animation_buffer_scale_apply, &bd);
}

static void solux_client_restore_open_clip(Client *c)
{
	struct wlr_box clip;
	if (!c || !c->scene_surface || !client_surface(c) ||
		!client_surface(c)->mapped)
		return;
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

static void solux_client_apply_visual(Client *c, struct wlr_box visual, double factor)
{
	int32_t aw, ah;
	float opacity;
	const char *atype;

	if (!c || !c->scene || !c->scene_surface)
		return;

	aw = MAX(1, visual.width - 2 * (int)c->bw);
	ah = MAX(1, visual.height - 2 * (int)c->bw);
	atype = solux_client_animation_name(c);
	opacity = c->opacity;

	/* The scene tree itself is the animated rectangle. Keep the surface at its
	 * normal border offset. Moving the surface child as well would apply the
	 * zoom translation twice and is the reason the old implementation could
	 * make the first open frame appear in the wrong place. */
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);

	/* Do not clip the entire subsurface tree here.  Chromium and other
	 * clients can place popups/subsurfaces outside the toplevel bounds.
	 * Clipping the whole tree makes those surfaces look cut off and can
	 * produce the asymmetric blur/rounded-corner artifact.  SceneFX applies
	 * the actual rounded clip to the client buffers themselves. */

	/* During MOVE, including a tiled resize, keep the displayed surface at the
	 * same intermediate size as the animated scene rectangle.  OPEN is handled
	 * separately below using MangoWC's clip-based OPEN path. */
	if (c->animation.action == SOLUX_ANIM_MOVE) {
		struct wlr_box corner_visual = visual;
		BufferData bd = {
			.width = aw, .height = ah,
			.width_scale = (float)aw /
					(float)MAX(1, c->geom.width - 2 * (int)c->bw),
				.height_scale = (float)ah /
					(float)MAX(1, c->geom.height - 2 * (int)c->bw),
				.should_scale = true,
				.corner_location = solux_animation_corner_location(c, corner_visual),
				.root = client_surface(c),
				.follow_root = true,
		};
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				animation_buffer_scale_apply, &bd);
	}

	if (c->animation.action == SOLUX_ANIM_OPEN)
		solux_client_apply_open_clip(c, visual, factor);

	if (c->animation.action == SOLUX_ANIM_OPEN && animation_fade_in &&
		strcasecmp(atype, "none")) {
		/* Mango applies the fade curve to the linear time progress, not to the
		 * already eased OPEN progress.  Feeding the eased value into the fade
		 * curve double-eases the animation and makes it look much sharper. */
		double fp = solux_animation_factor(factor, SOLUX_ANIM_OPAFADEIN);
		opacity = (float)MIN(c->opacity,
			fadein_begin_opacity + fp * (1.0 - fadein_begin_opacity));
	}

	wlr_scene_node_for_each_buffer(&c->scene_surface->node,
			animation_buffer_opacity_apply, &opacity);

	/* With no corner radius, the live rectangular border nodes must follow the
	 * same intermediate geometry as the animated client. */
	if (!(scenefx_corner_radius > 0 &&
		!(scenefx_corner_radius_only_floating && !c->isfloating) &&
		!c->isfullscreen))
		solux_animation_apply_rect_borders(c, visual);

	/* Keep SceneFX decorations synchronized with the transient visual geometry,
	 * without ever changing the layout geometry used by arrange().  MangoWC
	 * also sizes its blur from the animation geometry; leaving blur at the final
	 * target size is what creates the one-sided blur/rounded-corner artifact
	 * while Chromium is entering or being resized. */
	{
		struct wlr_box real = c->geom;
		c->geom = visual;
		update_client_rounded_borders(c);
		if (c->blur) {
			int radius = c->corner_radius;
			if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
				radius = 0;
			wlr_scene_blur_set_size(c->blur, visual.width, visual.height);
			struct fx_corner_radii cr = solux_animation_corner_location(c, visual);
			wlr_scene_blur_set_corner_radii(c->blur, cr);
			wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region) {
				.corners = cr,
				.area = { 0, 0, MAX(0, visual.width), MAX(0, visual.height) }
			});
		}
		if (c->shadow)
			client_set_shadow_blur_sigma(c, c->shadow->blur_sigma);
		c->geom = real;
	}
}

static int solux_client_slide_direction(Client *c)
{
	Client *it;
	int visible_tiled = 0;
	int32_t center_x, center_y;
	int32_t h, hr, v, vb;

	if (!c || !c->mon)
		return SOLUX_ANIM_DOWN;

	/* MangoWC opens a single tiled client from the bottom. */
	wl_list_for_each(it, &clients, link) {
		if (it != c && VISIBLEON(it, c->mon) && !it->isfloating && !it->isfullscreen)
			visible_tiled++;
	}
	if (!c->isfloating && !c->isfullscreen && visible_tiled == 0)
		return SOLUX_ANIM_DOWN;

	center_x = c->geom.x + c->geom.width / 2;
	center_y = c->geom.y + c->geom.height / 2;
	h = abs(center_x - c->mon->w.x);
	hr = abs(c->mon->w.x + c->mon->w.width - center_x);
	v = abs(center_y - c->mon->w.y);
	vb = abs(c->mon->w.y + c->mon->w.height - center_y);
	if (MIN(h, hr) < MIN(v, vb))
		return h < hr ? SOLUX_ANIM_LEFT : SOLUX_ANIM_RIGHT;
	return v < vb ? SOLUX_ANIM_UP : SOLUX_ANIM_DOWN;
}

static struct wlr_box solux_client_open_geometry(Client *c)
{
	struct wlr_box g = c->geom;
	int dir;
	if (solux_animation_is(animation_type_open, "fade") ||
		solux_animation_is(animation_type_open, "none"))
		return g;
	if (solux_animation_is(animation_type_open, "zoom")) {
		g.width = MAX(1, (int)(c->geom.width * zoom_initial_ratio));
		g.height = MAX(1, (int)(c->geom.height * zoom_initial_ratio));
		g.x = c->geom.x + (c->geom.width - g.width) / 2;
		g.y = c->geom.y + (c->geom.height - g.height) / 2;
		return g;
	}
	dir = solux_client_slide_direction(c);
	switch (dir) {
	case SOLUX_ANIM_UP: g.y = c->mon->w.y - c->geom.height; break;
	case SOLUX_ANIM_DOWN: g.y = c->mon->w.y + c->mon->w.height; break;
	case SOLUX_ANIM_LEFT: g.x = c->mon->w.x - c->geom.width; break;
	case SOLUX_ANIM_RIGHT: g.x = c->mon->w.x + c->mon->w.width; break;
	}
	return g;
}

static void solux_client_start_open(Client *c)
{
	if (!c || !c->mon || client_is_unmanaged(c) || !animations || c->isfullscreen)
		return;
	if (c->isnoopenanimation || solux_animation_is(animation_type_open, "none"))
		return;
	c->animation.action = SOLUX_ANIM_OPEN;
	c->animation.duration = animation_duration_open;
	c->animation.initial = solux_client_open_geometry(c);
	c->animation.current = c->animation.initial;
	c->animation.target = c->geom;
	c->animation.time_started = solux_animation_now_ms();
	c->animation.running = true;
	c->is_pending_open_animation = false;
	/* mapnotify deliberately leaves managed clients disabled until arrange().
	 * Enable the node here as well so the first frame of a newly mapped client
	 * can never be skipped. */
	wlr_scene_node_set_enabled(&c->scene->node, true);
	wlr_scene_node_set_position(&c->scene->node,
			c->animation.initial.x, c->animation.initial.y);
	if (c->scene_surface)
		wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	if (c->animation.initial.width != c->geom.width ||
			c->animation.initial.height != c->geom.height) {
		struct wlr_box real = c->geom;
		c->geom = c->animation.initial;
		update_client_rounded_borders(c);
		c->geom = real;
	}
	solux_client_apply_visual(c, c->animation.initial, 0.0);
	solux_animation_schedule(c->mon);
}

static void solux_client_start_move(Client *c, struct wlr_box target)
{
	if (!c || !c->mon || !animations || c->isnoanimation || c->isfullscreen) {
		c->animation.running = false;
		c->animation.current = c->animation.target = target;
		wlr_scene_node_set_position(&c->scene->node, target.x, target.y);
		return;
	}
	if (c->animation.running && wlr_box_equal(&c->animation.target, &target))
		return;
	c->animation.action = SOLUX_ANIM_MOVE;
	c->animation.duration = animation_duration_move;
	c->animation.initial = c->animation.running ? c->animation.current : c->animation.current;
	if (c->animation.initial.width <= 0 || c->animation.initial.height <= 0)
		c->animation.initial = target;
	c->animation.target = target;
	c->animation.time_started = solux_animation_now_ms();
	c->animation.running = true;
	wlr_scene_node_set_position(&c->scene->node, c->animation.initial.x, c->animation.initial.y);
	solux_client_apply_visual(c, c->animation.initial, 0.0);
	solux_animation_schedule(c->mon);
}

static void solux_client_resize(Client *c)
{
	if (!c || !c->scene || !client_surface(c)->mapped || !c->mon)
		return;
	if (c->is_pending_open_animation) {
		solux_client_start_open(c);
		return;
	}
	/* Once an XWayland surface is mapped, configure/arrange notifications can
	 * immediately follow the map event.  They are geometry updates, not a new
	 * move animation.  Keep the OPEN animation alive and only update its final
	 * target until the first appearance has completed.  This mirrors Mango's
	 * pending-open lifecycle and prevents X11 windows from animating only after
	 * a later move/configure. */
	if (c->animation.running && c->animation.action == SOLUX_ANIM_OPEN) {
		c->animation.target = c->geom;
		return;
	}
	if (c->taganim_role != TAGANIM_NONE && c->mon->taganim_active) {
		if (c->animation.running) {
			c->animation.initial = c->animation.current = c->animation.target = c->geom;
			c->animation.duration = 0;
			c->animation.time_started = solux_animation_now_ms();
			solux_animation_schedule(c->mon);
		} else {
			c->animation.current = c->animation.target = c->geom;
		}
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		return;
	}
	if (!animations || c->isnoanimation) {
		c->animation.running = false;
		c->animation.current = c->animation.target = c->geom;
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		return;
	}
	solux_client_start_move(c, c->geom);
}

static void solux_client_animation_next_tick(Client *c)
{
	double p, factor;
	struct wlr_box v;
	uint64_t now;
	if (!c || !c->animation.running)
		return;
	now = solux_animation_now_ms();
	p = c->animation.duration ?
		(double)(now - c->animation.time_started) / (double)c->animation.duration : 1.0;
	p = MAX(0.0, MIN(1.0, p));
	factor = solux_animation_factor(p, c->animation.action == SOLUX_ANIM_NONE ?
		SOLUX_ANIM_MOVE : c->animation.action);
	v.x = c->animation.initial.x +
		(int32_t)lround((c->animation.target.x - c->animation.initial.x) * factor);
	v.y = c->animation.initial.y +
		(int32_t)lround((c->animation.target.y - c->animation.initial.y) * factor);
	v.width = MAX(1, c->animation.initial.width +
		(int32_t)lround((c->animation.target.width - c->animation.initial.width) * factor));
	v.height = MAX(1, c->animation.initial.height +
		(int32_t)lround((c->animation.target.height - c->animation.initial.height) * factor));
	c->animation.current = v;
	wlr_scene_node_set_position(&c->scene->node, v.x, v.y);
	if (c->scene_surface)
		wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	solux_client_apply_visual(c, v, p);
	if (p >= 1.0) {
		c->animation.running = false;
		c->animation.action = SOLUX_ANIM_MOVE;
		c->animation.current = c->geom;
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		if (c->scene_surface)
			wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
		update_client_rounded_borders(c);
		if (c->shadow)
			client_set_shadow_blur_sigma(c, c->shadow->blur_sigma);
		/* Restore the real target buffer geometry/clip/opacity after zoom or
		 * fade. Otherwise the last animated SceneFX destination can survive until
		 * the next client commit. */
		{
			BufferData bd = {
				.width = MAX(1, c->geom.width - 2 * (int)c->bw),
				.height = MAX(1, c->geom.height - 2 * (int)c->bw),
				.width_scale = 1.0f, .height_scale = 1.0f,
				.should_scale = true,
				.root = client_surface(c),
				.follow_root = true,
			};
			wlr_scene_node_for_each_buffer(&c->scene_surface->node,
					animation_buffer_scale_apply, &bd);
			wlr_scene_node_for_each_buffer(&c->scene_surface->node,
					iter_xdg_scene_buffers, c);
			solux_client_restore_open_clip(c);
		}
		if (c->mon)
			solux_animation_schedule(c->mon);
	}
}

static void solux_client_close_snapshot(Client *c)
{
	nfloat_close_snapshot(c);
}

static void solux_client_fadeout_next_tick(SoluxFadeClient *f)
{
	double p, factor, op;
	struct wlr_box v;
	uint64_t now = solux_animation_now_ms();
	if (!f) return;
	p = f->duration ? (double)(now - f->time_started) / f->duration : 1.0;
	p = MAX(0.0, MIN(1.0, p));
	factor = solux_animation_factor(p, SOLUX_ANIM_CLOSE);
	v.x = f->initial.x + (f->current.x - f->initial.x) * factor;
	v.y = f->initial.y + (f->current.y - f->initial.y) * factor;
	v.width = MAX(1, f->initial.width + (f->current.width - f->initial.width) * factor);
	v.height = MAX(1, f->initial.height + (f->current.height - f->initial.height) * factor);
	wlr_scene_node_set_position(&f->scene->node, v.x, v.y);
	if (f->type == 2) {
		BufferData bd = { .width = v.width, .height = v.height,
			.width_scale = f->initial.width > 0 ? (float)v.width / (float)f->initial.width : 1.0f,
			.height_scale = f->initial.height > 0 ? (float)v.height / (float)f->initial.height : 1.0f,
			.should_scale = false };
		wlr_scene_node_for_each_buffer(&f->scene->node, fadeout_buffer_size_apply, &bd);
	}
	if (animation_fade_out) {
		op = MAX(0.0, fadeout_begin_opacity * (1.0 -
			solux_animation_factor(p, SOLUX_ANIM_OPAFADEOUT)));
		solux_animation_set_buffers_opacity(&f->scene->node, (float)op);
	}
	if (p >= 1.0) {
		wl_list_remove(&f->link);
		wlr_scene_node_for_each_buffer(&f->scene->node, fadeout_buffer_free_base, NULL);
		wlr_scene_node_destroy(&f->scene->node);
		free(f);
	}
}

static void solux_animation_client_resize(Client *c) { solux_client_resize(c); }
static void solux_animation_client_frame(Client *c) { solux_client_animation_next_tick(c); }
static void solux_animation_client_close(Client *c) { solux_client_close_snapshot(c); }
static void solux_animation_client_fadeout_frame(SoluxFadeClient *f) { solux_client_fadeout_next_tick(f); }
#endif
