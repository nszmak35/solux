/* Float-layout canvas: per-tag camera, zoom, world/screen conversion and canvas commands. */
#ifndef SOLUX_CANVAS_CANVAS_H
#define SOLUX_CANVAS_CANVAS_H

static unsigned int
solux_pertag_index(Monitor *m, unsigned int tag)
{
	if (!m || !m->pertag)
		return 1;
	return tag <= TAGCOUNT ? tag : 0;
}

static void
solux_canvas_save_pertag(Monitor *m)
{
	unsigned int tag;
	if (!m || !m->pertag)
		return;
	tag = solux_pertag_index(m, m->pertag->curtag);
	m->pertag->canvas_x[tag] = m->canvas_target_x;
	m->pertag->canvas_y[tag] = m->canvas_target_y;
	/* Persist the requested zoom, not an in-flight animation frame. */
	m->pertag->canvas_zoom[tag] = m->canvas_target_zoom > 0.01 ? m->canvas_target_zoom : 1.0;
}

static void
solux_canvas_load_pertag(Monitor *m, unsigned int tag, bool animate)
{
	double x, y, zoom;
	if (!m || !m->pertag)
		return;
	tag = solux_pertag_index(m, tag);
	x = m->pertag->canvas_x[tag];
	y = m->pertag->canvas_y[tag];
	zoom = m->pertag->canvas_zoom[tag];
	if (zoom <= 0.01)
		zoom = 1.0;
	if (!animate || !solux_canvas_active(m)) {
		m->canvas_x = m->canvas_target_x = x;
		m->canvas_y = m->canvas_target_y = y;
		m->canvas_zoom = m->canvas_target_zoom = zoom;
		m->canvas_animating = false;
		return;
	}
	solux_canvas_start(m, x, y, zoom);
}

static void
solux_canvas_record_client_geom(Client *c)
{
	unsigned int tag;

	if (!c || !c->mon || !c->mon->pertag || !solux_canvas_active(c->mon) ||
		c->isfullscreen || client_is_unmanaged(c) || !c->isfloating ||
		!VISIBLEON(c, c->mon))
		return;

	tag = c->mon->pertag->curtag;
	if (tag > TAGCOUNT)
		return;

	/* Client::geom is the permanent world-space position.  Animation.current
	 * is deliberately ignored: it is only a visual interpolation state. */
	c->canvas_tag_geom[tag] = c->geom;
	c->canvas_tag_geom_valid |= (uint32_t)1u << tag;
}

static void
solux_canvas_save_tag_geometries(Monitor *m, unsigned int tag)
{
	Client *c;

	if (!m || !m->pertag || tag > TAGCOUNT || !solux_canvas_active(m))
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !VISIBLEON(c, m) || c->isfullscreen ||
			client_is_unmanaged(c) || !c->isfloating)
			continue;

		c->canvas_tag_geom[tag] = c->geom;
		c->canvas_tag_geom_valid |= (uint32_t)1u << tag;
	}
}

static void
solux_canvas_load_tag_geometries(Monitor *m, unsigned int tag)
{
	Client *c;

	if (!m || tag > TAGCOUNT || !solux_canvas_active(m))
		return;

	wl_list_for_each(c, &clients, link) {
		struct wlr_box target;

		if (c->mon != m || !VISIBLEON(c, m) || c->isfullscreen ||
			client_is_unmanaged(c) || !c->isfloating)
			continue;

		if (c->canvas_tag_geom_valid & ((uint32_t)1u << tag)) {
			target = c->canvas_tag_geom[tag];
		} else {
			/* First visit of this tag: establish its initial world geometry
			 * exactly once.  Do not manufacture a new position and do not
			 * clamp it to the monitor. */
			c->canvas_tag_geom[tag] = c->geom;
			c->canvas_tag_geom_valid |= (uint32_t)1u << tag;
			continue;
		}

		/*
		 * A tag switch is not a client move.  Do not let a stale MOVE animation
		 * (possibly started before the client was hidden) participate in the
		 * restore, because its current position can be a screen-space
		 * interpolation from a previous arrange.  The saved value is the
		 * authoritative Float world position for this tag.
		 */
		c->animation.running = false;
		c->animation.action = SOLUX_ANIM_MOVE;
		c->animation.initial = target;
		c->animation.current = target;
		c->animation.target = target;
		resize(c, target, 0);
		c->animation.running = false;
		c->animation.initial = c->animation.current =
			c->animation.target = target;
		wlr_scene_node_set_position(&c->scene->node, target.x, target.y);
		solux_canvas_apply_client(c);
	}
}

static void
solux_pertag_save_focus(Monitor *m)
{
	unsigned int tag;
	Client *c;
	if (!m || !m->pertag)
		return;
	tag = solux_pertag_index(m, m->pertag->curtag);
	c = focustop(m);
	m->pertag->focused[tag] = c && c->mon == m && VISIBLEON(c, m) ? c : NULL;
}

static Client *
solux_pertag_focus(Monitor *m)
{
	unsigned int tag;
	Client *c;
	if (!m || !m->pertag)
		return NULL;
	tag = solux_pertag_index(m, m->pertag->curtag);
	c = m->pertag->focused[tag];
	if (c && c->mon == m && VISIBLEON(c, m) && !client_is_unmanaged(c) &&
		c->scene && client_surface(c)->mapped)
		return c;
	return focustop(m);
}

static bool
solux_canvas_active(Monitor *m)
{
	return m && m->wlr_output && m->wlr_output->enabled &&
		m->lt[m->sellt] && !m->lt[m->sellt]->arrange;
}

static struct wlr_box
solux_canvas_world_to_screen(Monitor *m, struct wlr_box world)
{
	struct wlr_box v = world;
	double cx, cy, z;
	if (!m || !solux_canvas_active(m)) return v;
	cx = m->m.x + m->m.width / 2.0;
	cy = m->m.y + m->m.height / 2.0;
	z = m->canvas_zoom > 0.01 ? m->canvas_zoom : 1.0;
	v.x = (int)lround(cx + (world.x - m->canvas_x) * z);
	v.y = (int)lround(cy + (world.y - m->canvas_y) * z);
	v.width = MAX(1, (int)lround(world.width * z));
	v.height = MAX(1, (int)lround(world.height * z));
	return v;
}

static void
solux_canvas_screen_to_world(Monitor *m, double sx, double sy, double *wx, double *wy)
{
	double cx, cy, z;
	if (!m || !solux_canvas_active(m)) {
		if (wx) *wx = sx;
		if (wy) *wy = sy;
		return;
	}
	cx = m->m.x + m->m.width / 2.0;
	cy = m->m.y + m->m.height / 2.0;
	z = m->canvas_zoom > 0.01 ? m->canvas_zoom : 1.0;
	if (wx) *wx = m->canvas_x + (sx - cx) / z;
	if (wy) *wy = m->canvas_y + (sy - cy) / z;
}


/*
 * A window dropped on another monitor keeps the spot where it was released.
 * Client::geom is world geometry of the monitor's own camera, so reusing the
 * old numbers on the new monitor would make it jump by the difference of the
 * two cameras.  Keep the on-screen centre and the world size.
 */
static void
solux_canvas_transfer_client(Client *c, Monitor *newmon)
{
	Monitor *old;
	struct wlr_box visual, g;
	double wx, wy;

	if (!c || !newmon || !(old = c->mon) || old == newmon ||
		c->isfullscreen || !c->isfloating || client_is_unmanaged(c) ||
		!solux_canvas_active(old) || !solux_canvas_active(newmon))
		return;

	visual = solux_canvas_world_to_screen(old, c->geom);
	solux_canvas_screen_to_world(newmon,
		visual.x + visual.width / 2.0, visual.y + visual.height / 2.0, &wx, &wy);
	g = c->geom;
	g.x = (int)lround(wx - g.width / 2.0);
	g.y = (int)lround(wy - g.height / 2.0);

	c->geom = g;
	/* Positions are final: no MOVE animation from the old camera's numbers. */
	c->animation.running = false;
	c->animation.action = SOLUX_ANIM_MOVE;
	c->animation.initial = c->animation.current = c->animation.target = g;
}

/*
 * Canvas decorations are screen-space decorations.  Never feed Canvas
 * coordinates back into Client::geom or the global border configuration.
 * The latter was the source of the Float/Chromium border drift: Canvas used
 * to temporarily rewrite bw/bws/bwe and the global offsets, then call the
 * normal decoration code, which left nodes sized from a different coordinate
 * system than the surface.
 */
static void
solux_canvas_update_decorations(Client *c, struct wlr_box visual, double zoom)
{
	int bw, bws, bwe, off, neg, radius, inner_radius;
	int x, y, w, h, thickness;
	bool rounded;

	if (!c || !c->scene || !c->scene_surface) return;
	zoom = zoom > 0.01 ? zoom : 1.0;
	bw = MAX(0, (int)lround((double)c->bw * zoom));
	bws = MAX(0, (int)lround((double)c->bws * zoom));
	bwe = MAX(0, (int)lround((double)c->bwe * zoom));
	off = MAX(0, (int)lround((double)borderspx_offset * zoom));
	neg = (int)lround((double)borderepx_negative_offset * zoom);
	radius = MAX(0, (int)lround((double)c->corner_radius * zoom));
	inner_radius = radius;

	/* The client surface and every decoration share this exact outer rect. */
	wlr_scene_node_set_position(&c->scene_surface->node, bw, bw);

	rounded = scenefx_corner_radius > 0 &&
		!(scenefx_corner_radius_only_floating && !c->isfloating) &&
		!c->isfullscreen;

	if (!rounded) {
		if (c->round_border) wlr_scene_node_set_enabled(&c->round_border->node, 0);
		if (c->round_borders) wlr_scene_node_set_enabled(&c->round_borders->node, 0);
		if (c->round_bordere) wlr_scene_node_set_enabled(&c->round_bordere->node, 0);

		wlr_scene_rect_set_size(c->border[0], MAX(0, visual.width), bw);
		wlr_scene_rect_set_size(c->border[1], MAX(0, visual.width), bw);
		wlr_scene_rect_set_size(c->border[2], bw, MAX(0, visual.height - 2 * bw));
		wlr_scene_rect_set_size(c->border[3], bw, MAX(0, visual.height - 2 * bw));
		wlr_scene_node_set_position(&c->border[0]->node, 0, 0);
		wlr_scene_node_set_position(&c->border[1]->node, 0, MAX(0, visual.height - bw));
		wlr_scene_node_set_position(&c->border[2]->node, 0, bw);
		wlr_scene_node_set_position(&c->border[3]->node, MAX(0, visual.width - bw), bw);
		for (int i = 0; i < 4; i++)
			wlr_scene_node_set_enabled(&c->border[i]->node, bw > 0);

		x = off; y = off;
		w = MAX(0, visual.width - 2 * off);
		h = MAX(0, visual.height - 2 * off);
		wlr_scene_rect_set_size(c->borders[0], w, bws);
		wlr_scene_rect_set_size(c->borders[1], w, bws);
		wlr_scene_rect_set_size(c->borders[2], bws, MAX(0, visual.height - 2 * bws - 2 * off));
		wlr_scene_rect_set_size(c->borders[3], bws, MAX(0, visual.height - 2 * bws - 2 * off));
		wlr_scene_node_set_position(&c->borders[0]->node, x, y);
		wlr_scene_node_set_position(&c->borders[1]->node, x, MAX(0, visual.height - bws - y));
		wlr_scene_node_set_position(&c->borders[2]->node, x, bws + y);
		wlr_scene_node_set_position(&c->borders[3]->node, MAX(0, visual.width - bws - x), bws + y);
		for (int i = 0; i < 4; i++)
			wlr_scene_node_set_enabled(&c->borders[i]->node, bws > 0);

		wlr_scene_rect_set_size(c->bordere[0], MAX(0, visual.width - (bw - bwe) * 2 + neg * 2), bwe);
		wlr_scene_rect_set_size(c->bordere[1], MAX(0, visual.width - (bw - bwe) * 2 + neg * 2), bwe);
		wlr_scene_rect_set_size(c->bordere[2], bwe, MAX(0, visual.height - 2 * bw + 2 * neg));
		wlr_scene_rect_set_size(c->bordere[3], bwe, MAX(0, visual.height - 2 * bw + 2 * neg));
		x = bw - bwe - neg;
		y = x;
		wlr_scene_node_set_position(&c->bordere[0]->node, x, y);
		wlr_scene_node_set_position(&c->bordere[1]->node, x, MAX(0, visual.height - bw + neg));
		wlr_scene_node_set_position(&c->bordere[2]->node, x, y);
		wlr_scene_node_set_position(&c->bordere[3]->node, MAX(0, visual.width - bw + neg), y);
		for (int i = 0; i < 4; i++)
			wlr_scene_node_set_enabled(&c->bordere[i]->node, bwe > 0);
		return;
	}

	if (!c->round_border)
		c->round_border = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
	if (!c->round_borders)
		c->round_borders = wlr_scene_rect_create(c->scene, 0, 0, borderscolor);
	if (!c->round_bordere)
		c->round_bordere = wlr_scene_rect_create(c->scene, 0, 0, borderecolor);
	if (!c->round_border || !c->round_borders || !c->round_bordere) return;
	c->round_border->node.data = c;
	c->round_borders->node.data = c;
	c->round_bordere->node.data = c;
	wlr_scene_node_place_above(&c->round_border->node, &c->scene_surface->node);
	wlr_scene_node_place_above(&c->round_borders->node, &c->scene_surface->node);
	wlr_scene_node_place_above(&c->round_bordere->node, &c->scene_surface->node);
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&c->border[i]->node, 0);
		wlr_scene_node_set_enabled(&c->borders[i]->node, 0);
		wlr_scene_node_set_enabled(&c->bordere[i]->node, 0);
	}

	thickness = bw;
	if (thickness > 0) {
		radius = MIN(radius + thickness, MIN(visual.width / 2, visual.height / 2));
		inner_radius = MIN(inner_radius, MAX(0, MIN((visual.width - 2 * thickness) / 2,
										 (visual.height - 2 * thickness) / 2)));
		solux_decor_ring_apply(c, c->round_border, 0, 0, visual.width, visual.height, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_border->node, 1);
	} else wlr_scene_node_set_enabled(&c->round_border->node, 0);

	thickness = bws;
	if (thickness > 0) {
		x = off; y = off; w = MAX(0, visual.width - 2 * x); h = MAX(0, visual.height - 2 * y);
		radius = MAX(0, MIN(radius - x + thickness, MIN(w / 2, h / 2)));
		inner_radius = MAX(0, MIN(inner_radius - x, MAX(0, MIN((w - 2 * thickness) / 2,
												(h - 2 * thickness) / 2))));
		solux_decor_ring_apply(c, c->round_borders, x, y, w, h, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_borders->node, 1);
	} else wlr_scene_node_set_enabled(&c->round_borders->node, 0);

	thickness = bwe;
	if (thickness > 0) {
		x = bw - thickness - neg; y = x;
		w = MAX(0, visual.width - 2 * (bw - thickness) + 2 * neg);
		h = MAX(0, visual.height - 2 * (bw - thickness) + 2 * neg);
		radius = MAX(0, MIN(radius - x + thickness, MIN(w / 2, h / 2)));
		inner_radius = MAX(0, MIN(inner_radius - x, MAX(0, MIN((w - 2 * thickness) / 2,
												(h - 2 * thickness) / 2))));
		solux_decor_ring_apply(c, c->round_bordere, x, y, w, h, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_bordere->node, 1);
	} else wlr_scene_node_set_enabled(&c->round_bordere->node, 0);
}

static void
solux_canvas_apply_client(Client *c)
{
	struct wlr_box world, visual;
	double zoom;
	if (!c || !c->mon || !c->scene || !c->scene_surface || client_is_unmanaged(c) ||
		c->isfullscreen || !solux_canvas_active(c->mon)) return;
	if (c->mon->taganim_active && c->taganim_role == TAGANIM_OUT) return;

	world = c->animation.running ? c->animation.current : c->geom;
	if (world.width <= 0 || world.height <= 0) world = c->geom;
	visual = solux_canvas_world_to_screen(c->mon, world);
	zoom = c->mon->canvas_zoom > 0.01 ? c->mon->canvas_zoom : 1.0;
	c->canvas_visualized = true;

	/* Scale every buffer by the same visual factor, but preserve each buffer's
	 * own source dimensions.  This is essential for clients with subsurfaces
	 * (foot/Chromium are common examples): an absolute destination size for
	 * every buffer turns a child buffer into a large solid rectangle. */
	wlr_scene_node_set_position(&c->scene->node, visual.x, visual.y);
	nfloat_apply_canvas_client(c, visual, zoom);
	solux_canvas_update_decorations(c, visual, zoom);

	if (c->blur) {
		int radius = MAX(0, (int)lround((double)c->corner_radius * zoom));
		if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
			radius = 0;
		wlr_scene_blur_set_size(c->blur, visual.width, visual.height);
		wlr_scene_blur_set_corner_radius(c->blur, radius);
		wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region){
			.corners = corner_radii_all(radius), .area = {0, 0, visual.width, visual.height}});
	}
	if (c->shadow) client_refresh_shadow(c);
}

static void
solux_canvas_restore_client(Client *c)
{
	if (!c || !c->scene || !c->scene_surface || client_is_unmanaged(c)) return;
	nfloat_restore_canvas_client(c);
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	if (!(scenefx_corner_radius > 0 && !(scenefx_corner_radius_only_floating && !c->isfloating) && !c->isfullscreen))
		solux_animation_apply_rect_borders(c, c->geom);
	update_client_rounded_borders(c);
	if (c->blur) {
		int radius = c->corner_radius;
		if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen) radius = 0;
		wlr_scene_blur_set_size(c->blur, c->geom.width, c->geom.height);
		wlr_scene_blur_set_corner_radius(c->blur, radius);
		wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region) {
			.corners = corner_radii_all(radius), .area = {0, 0, c->geom.width, c->geom.height}});
	}
	if (c->shadow) client_refresh_shadow(c);
	c->canvas_visualized = false;
}

static void
solux_canvas_apply(Monitor *m)
{
	Client *c;
	if (!m) return;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->scene || client_is_unmanaged(c)) continue;
		if (m->taganim_active && c->taganim_role == TAGANIM_OUT) continue;
		if (solux_canvas_active(m) && !c->isfullscreen)
			solux_canvas_apply_client(c);
		else if (c->canvas_visualized)
			solux_canvas_restore_client(c);
	}
}

/*
 * Sharp text while zoomed in.
 *
 * The canvas stretches the buffer a client rendered, so past 1x text turns
 * soft.  To keep it crisp the client is asked to render at a larger buffer
 * scale (fractional-scale + preferred buffer scale), as if the output had that
 * scale.  The size on screen never changes: the scene draws every buffer at an
 * explicit destination size derived from the window's world geometry, so only
 * the amount of detail does.
 *
 * Zoomed out works the same way (a smaller buffer shrinks cleanly instead of
 * being skimmed by the filter).  It is done gently, because every change makes
 * the client redraw itself:
 *   - only clients that use wp_viewporter are touched: they keep their logical
 *     size whatever buffer scale they render at, so the window can never change
 *     size; others are simply scaled by the compositor
 *   - the scale snaps to a few steps, and up to ~12% of stretching is left alone
 *   - nothing happens while the camera moves or the zoom is still changing;
 *     the scale follows once it has been still for a moment
 *   - windows are updated a couple per frame, visible ones only, and never
 *     while they are animating
 */
#define SOLUX_TEXT_SCALE_SETTLE_MS 250
#define SOLUX_TEXT_SCALE_PER_FRAME 2

static double
solux_canvas_text_mult(double zoom)
{
	static const double levels[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0 };
	const size_t n = sizeof(levels) / sizeof(levels[0]);
	size_t i;

	for (i = 0; i < n; i++)
		if (levels[i] >= zoom / 1.12)
			return levels[i];
	return levels[n - 1];
}

static void
solux_canvas_scale_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
	(void)sx;
	(void)sy;
	client_set_scale(surface, *(float *)data);
}

/* Popups (and their sub-popups) follow the window's scale. */
static void
solux_canvas_scale_popups(struct wlr_xdg_surface *xdg, float scale, int depth)
{
	struct wlr_xdg_popup *popup;

	if (!xdg || depth > 8)
		return;
	wl_list_for_each(popup, &xdg->popups, link) {
		if (!popup->base || !popup->base->surface)
			continue;
		wlr_surface_for_each_surface(popup->base->surface,
			solux_canvas_scale_surface, &scale);
		solux_canvas_scale_popups(popup->base, scale, depth + 1);
	}
}

/* Would applying `scale` to this client change anything? */
static bool
solux_canvas_scale_differs(Client *c, float scale)
{
	if (!c || !c->mon || !c->mon->wlr_output || !c->scene ||
		client_is_unmanaged(c) || !client_surface(c))
		return false;
#ifdef XWAYLAND
	/* Xwayland does not take its buffer scale from the compositor per window. */
	if (client_is_x11(c))
		return false;
#endif
	if (fabsf(c->canvas_buf_scale - scale) < 0.001f)
		return false;
	/* A different scale only for clients that keep their logical size by
	 * themselves (viewporter); taking one back is always allowed. */
	if (fabsf(scale - c->mon->wlr_output->scale) > 0.001f &&
			!client_surface(c)->current.viewport.has_dst)
		return false;
	/* Nothing to restore for a client the canvas never touched. */
	if (c->canvas_buf_scale == 0.0f &&
			fabsf(scale - c->mon->wlr_output->scale) < 0.001f)
		return false;
	return true;
}

static void
solux_canvas_apply_scale(Client *c, float scale)
{
	if (!solux_canvas_scale_differs(c, scale))
		return;
	c->canvas_buf_scale = scale;
	wlr_surface_for_each_surface(client_surface(c), solux_canvas_scale_surface, &scale);
	if (c->type == XDGShell && c->surface.xdg)
		solux_canvas_scale_popups(c->surface.xdg, scale, 0);
}

static void
solux_canvas_sync_buffer_scale(Monitor *m)
{
	Client *c;
	uint64_t now;
	double mult, base;
	int budget = SOLUX_TEXT_SCALE_PER_FRAME;
	bool settling = false, pending = false;

	if (!m || !m->wlr_output || !m->wlr_output->enabled)
		return;
	base = m->wlr_output->scale > 0 ? m->wlr_output->scale : 1.0;
	if (m->canvas_scale_mult <= 0.0)
		m->canvas_scale_mult = 1.0;

	if (!solux_canvas_active(m)) {
		mult = 1.0;
	} else {
		now = solux_animation_now_ms();
		mult = solux_canvas_text_mult(m->canvas_zoom);
		if (m->canvas_animating || cursor_mode == CurCanvasMove ||
				fabs(m->canvas_zoom - m->canvas_scale_seen_zoom) > 1e-6) {
			m->canvas_scale_seen_zoom = m->canvas_zoom;
			m->canvas_scale_settle_ms = now;
		}
		settling = m->canvas_animating || cursor_mode == CurCanvasMove ||
			now - m->canvas_scale_settle_ms < SOLUX_TEXT_SCALE_SETTLE_MS;
		if (settling && fabs(mult - m->canvas_scale_mult) > 1e-6) {
			/* Wait for the camera to stop before changing the scale. */
			pending = true;
			mult = m->canvas_scale_mult;
		}
	}
	m->canvas_scale_mult = mult;

	wl_list_for_each(c, &clients, link) {
		struct wlr_box visual, hit;
		double want = base;

		if (c->mon != m || !c->scene)
			continue;
		if (solux_canvas_active(m) && c->canvas_visualized && !c->isfullscreen &&
				VISIBLEON(c, m)) {
			want = base * mult;
			if (want > 4.0 && base <= 4.0)
				want = 4.0;
		} else if (!solux_canvas_active(m) || c->isfullscreen) {
			want = base;
		} else {
			continue;
		}
		if (!solux_canvas_scale_differs(c, (float)want))
			continue;
		/* Never while the window itself is animating. */
		if (c->animation.running) {
			pending = true;
			continue;
		}
		/* Windows out of sight keep their scale until they come into view. */
		if (want > c->canvas_buf_scale && solux_canvas_active(m)) {
			visual = solux_canvas_world_to_screen(m, c->geom);
			if (!wlr_box_intersection(&hit, &visual, &m->m))
				continue;
		}
		if (budget <= 0) {
			pending = true;
			continue;
		}
		budget--;
		solux_canvas_apply_scale(c, (float)want);
	}

	if (pending)
		wlr_output_schedule_frame(m->wlr_output);
}

static void
solux_canvas_frame(Monitor *m)
{
	double p, f;
	if (!m || !m->wlr_output || !m->wlr_output->enabled) return;
	if (!solux_canvas_active(m)) {
		m->canvas_animating = false;
		solux_canvas_apply(m);
		solux_canvas_sync_buffer_scale(m);
		solux_canvas_refresh_pointer();
		return;
	}
	if (m->canvas_zoom <= 0.01) m->canvas_zoom = m->canvas_target_zoom = 1.0;
	if (m->canvas_x == 0.0 && m->canvas_y == 0.0) {
		m->canvas_x = m->canvas_target_x = m->m.x + m->m.width / 2.0;
		m->canvas_y = m->canvas_target_y = m->m.y + m->m.height / 2.0;
	}
	if (m->canvas_animating) {
		p = m->canvas_duration ? (double)(solux_animation_now_ms() - m->canvas_time_started) / (double)m->canvas_duration : 1.0;
		p = MAX(0.0, MIN(1.0, p));
		f = animations ? solux_animation_factor(p, SOLUX_ANIM_MOVE) : 1.0;
		m->canvas_x = m->canvas_start_x + (m->canvas_target_x - m->canvas_start_x) * f;
		m->canvas_y = m->canvas_start_y + (m->canvas_target_y - m->canvas_start_y) * f;
		m->canvas_zoom = m->canvas_start_zoom + (m->canvas_target_zoom - m->canvas_start_zoom) * f;
		if (p >= 1.0)
			m->canvas_animating = false;
	}
	solux_canvas_apply(m);
	solux_canvas_sync_buffer_scale(m);
	/* Keep Wayland pointer coordinates in sync with the animated camera. */
	solux_canvas_refresh_pointer();
	if (m->canvas_animating) wlr_output_schedule_frame(m->wlr_output);
}

static void
solux_canvas_start(Monitor *m, double x, double y, double zoomlevel)
{
	if (!m || !solux_canvas_active(m)) return;
	zoomlevel = MAX(0.3, MIN(4.0, zoomlevel));
	m->canvas_target_x = x; m->canvas_target_y = y; m->canvas_target_zoom = zoomlevel;
	if (!animations) {
		m->canvas_x = x; m->canvas_y = y; m->canvas_zoom = zoomlevel; m->canvas_animating = false;
		solux_canvas_apply(m);
		solux_canvas_refresh_pointer();
		wlr_output_schedule_frame(m->wlr_output);
		return;
	}
	m->canvas_start_x = m->canvas_x;
	m->canvas_start_y = m->canvas_y;
	m->canvas_start_zoom = m->canvas_zoom;
	m->canvas_time_started = solux_animation_now_ms();
	m->canvas_duration = animation_duration_move > 0 ? animation_duration_move : 250;
	m->canvas_animating = true;
	wlr_output_schedule_frame(m->wlr_output);
}

/*
 * Canvas buffers are rendered with a destination size larger/smaller than
 * their Wayland source size, and cropped to the window geometry.
 * wlr_scene_node_at() reports coordinates in that destination space, so
 * without converting them back a zoomed window receives pointer events at the
 * wrong place -- and the error differs per surface, which is how hovering one
 * element lit up its neighbours.
 *
 * The conversion is done from first principles for ANY surface of the
 * toplevel (root or subsurface, e.g. the Firefox content area): the screen
 * point is mapped through the same scale/crop the buffers were placed with
 * (see nfloat_canvas_buffer_apply), then made relative to the surface.  It
 * does not depend on the node that wlroots happened to hit.  Normal
 * Float/tiled pointer coordinates are left untouched (returns false).
 */
/*
 * Popups (menus, tooltips) are scaled with their window (see
 * nfloat_canvas_popups), so the pointer position inside one is mapped back the
 * same way as for the window itself: from the screen position of the popup's
 * tree origin, which is the origin of its window geometry.
 */
static bool
solux_canvas_popup_local(Client *c, struct wlr_surface *surface,
	double px, double py, double *sx, double *sy)
{
	struct wlr_surface *root, *p;
	struct wlr_xdg_popup *popup;
	struct wlr_scene_tree *tree;
	struct wlr_box geo;
	double kx, ky;
	int ox = 0, oy = 0, tx = 0, ty = 0, depth = 0;

	if (!c || !surface || !sx || !sy || c->type != XDGShell)
		return false;
	kx = c->canvas_scale_x > 0.001 ? c->canvas_scale_x : 1.0;
	ky = c->canvas_scale_y > 0.001 ? c->canvas_scale_y : 1.0;
	/* Unscaled popups keep the coordinates wlroots computed. */
	if (fabs(kx - 1.0) < 1e-6 && fabs(ky - 1.0) < 1e-6)
		return false;

	root = wlr_surface_get_root_surface(surface);
	popup = wlr_xdg_popup_try_from_wlr_surface(root);
	if (!popup || !popup->base)
		return false;
	/* The popup must belong to this window (directly or through other popups). */
	for (p = popup->parent; p && p != client_surface(c) && depth < 16; depth++) {
		struct wlr_xdg_popup *pp = wlr_xdg_popup_try_from_wlr_surface(p);

		if (!pp)
			return false;
		p = pp->parent;
	}
	if (p != client_surface(c))
		return false;

	tree = root->data;
	if (!tree || !nfloat_surface_root_offset(surface, root, &ox, &oy) ||
		!wlr_scene_node_coords(&tree->node, &tx, &ty))
		return false;

	geo = popup->base->geometry;
	*sx = geo.x + (px - tx) / kx - ox;
	*sy = geo.y + (py - ty) / ky - oy;
	return true;
}

/*
 * The box a new popup is kept inside, in the surface coordinates of its
 * toplevel.  On the zoomed canvas that is the monitor as seen through the
 * camera -- the popup will be shown scaled -- not the monitor in world space.
 */
static void
solux_canvas_popup_box(Client *c, struct wlr_box *box)
{
	struct wlr_box clip;
	double kx, ky;
	int lx = 0, ly = 0;

	if (!c || !box || !c->mon || !c->scene_surface || c->isfullscreen ||
		client_is_unmanaged(c) || !solux_canvas_active(c->mon) ||
		!c->canvas_visualized || !wlr_scene_node_coords(&c->scene_surface->node, &lx, &ly))
		return;

	kx = c->canvas_scale_x > 0.001 ? c->canvas_scale_x : 1.0;
	ky = c->canvas_scale_y > 0.001 ? c->canvas_scale_y : 1.0;
	client_get_clip(c, &clip);

	/* (lx, ly) is where the clip origin is drawn. */
	box->x = (int)floor((double)clip.x + (c->mon->w.x - lx) / kx);
	box->y = (int)floor((double)clip.y + (c->mon->w.y - ly) / ky);
	box->width = MAX(1, (int)lround(c->mon->w.width / kx));
	box->height = MAX(1, (int)lround(c->mon->w.height / ky));
}

static bool
solux_canvas_surface_local(Client *c, struct wlr_surface *surface,
	double px, double py, double *sx, double *sy)
{
	struct wlr_box clip;
	double kx, ky;
	int ox = 0, oy = 0, lx = 0, ly = 0;

	if (!c || !surface || !sx || !sy || !c->mon || !c->scene_surface ||
		!solux_canvas_active(c->mon) || !c->canvas_visualized ||
		client_is_unmanaged(c) || !client_surface(c))
		return false;

	/* Popups are scaled with the window; they have their own mapping. */
	if (wlr_xdg_popup_try_from_wlr_surface(wlr_surface_get_root_surface(surface)) != NULL)
		return solux_canvas_popup_local(c, surface, px, py, sx, sy);
	if (wlr_surface_get_root_surface(surface) != client_surface(c) ||
		!nfloat_surface_root_offset(surface, client_surface(c), &ox, &oy) ||
		!wlr_scene_node_coords(&c->scene_surface->node, &lx, &ly))
		return false;

	kx = c->canvas_scale_x > 0.001 ? c->canvas_scale_x : 1.0;
	ky = c->canvas_scale_y > 0.001 ? c->canvas_scale_y : 1.0;
	client_get_clip(c, &clip);

	/* (lx, ly) is where the clip origin is drawn. */
	*sx = clip.x + (px - lx) / kx - ox;
	*sy = clip.y + (py - ly) / ky - oy;
	return true;
}

static void
solux_canvas_surface_coords(Client *c, struct wlr_surface *surface,
	double *sx, double *sy)
{
	double lx, ly;

	if (!cursor || !sx || !sy)
		return;
	if (solux_canvas_surface_local(c, surface, cursor->x, cursor->y, &lx, &ly)) {
		*sx = lx;
		*sy = ly;
	}
}

/*
 * Refresh the client's pointer coordinates after a canvas camera/zoom frame.
 * This is intentionally a zero-time internal update: it does not move the
 * compositor cursor or generate a relative-motion event, it only keeps the
 * Wayland pointer focus coordinates synchronized with the transformed scene.
 */
static void
solux_canvas_refresh_pointer(void)
{
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	double sx = 0, sy = 0;

	if (!cursor || !seat)
		return;

	xytonode(cursor->x, cursor->y, &surface, &c, &l, &sx, &sy);
	if (c && surface && solux_canvas_active(c->mon))
		solux_canvas_surface_coords(c, surface, &sx, &sy);
	/* This runs every frame.  When the pointer is already where the client
	 * last saw it there is nothing to tell it; repeating the same motion each
	 * frame only wakes the client up for nothing (and keeps the compositor
	 * rendering while a screen recorder is running). */
	if (surface && surface == seat->pointer_state.focused_surface &&
			fabs(sx - seat->pointer_state.sx) < 1e-6 &&
			fabs(sy - seat->pointer_state.sy) < 1e-6)
		return;
	pointerfocus(c, surface, sx, sy, 0);
}

static void
zoomcanvas(const Arg *arg)
{
	Monitor *m = selmon;
	int dir = arg ? arg->i : 1;
	double oldzoom, newzoom, cx, cy, wx, wy, targetx, targety;
	/* Canvas controls are valid only while the selected layout is Float. */
	if (!m || !m->lt[m->sellt] || m->lt[m->sellt]->arrange ||
			!solux_canvas_active(m))
		return;
	oldzoom = m->canvas_target_zoom > 0.01 ? m->canvas_target_zoom : 1.0;
	newzoom = MAX(0.3, MIN(4.0, oldzoom * (dir < 0 ? 0.9 : 1.1)));
	/* Keep the world point under the pointer fixed while zooming. */
	cx = m->m.x + m->m.width / 2.0;
	cy = m->m.y + m->m.height / 2.0;
	solux_canvas_screen_to_world(m, cursor->x, cursor->y, &wx, &wy);
	targetx = wx - (cursor->x - cx) / newzoom;
	targety = wy - (cursor->y - cy) / newzoom;
	solux_canvas_start(m, targetx, targety, newzoom);
}

static void
movecanvas(const Arg *arg)
{
	Monitor *m = selmon;
	int dir = arg ? arg->i : 0;
	double step;
	/* Canvas controls are valid only while the selected layout is Float. */
	if (!m || !m->lt[m->sellt] || m->lt[m->sellt]->arrange ||
			!solux_canvas_active(m))
		return;
	step = 200.0 / MAX(0.3, m->canvas_zoom);
	switch (dir) {
	case 0: m->canvas_target_x = m->canvas_x - step; break;
	case 1: m->canvas_target_x = m->canvas_x + step; break;
	case 2: m->canvas_target_y = m->canvas_y - step; break;
	case 3: m->canvas_target_y = m->canvas_y + step; break;
	default: return;
	}
	solux_canvas_start(m, m->canvas_target_x, m->canvas_target_y, m->canvas_target_zoom);
}

static void
canvasdrag(const Arg *arg)
{
	Monitor *m = selmon;
	(void)arg;
	/* Canvas dragging is Float-only, just like zoomcanvas/movecanvas. */
	if (!m || !m->lt[m->sellt] || m->lt[m->sellt]->arrange ||
			!solux_canvas_active(m))
		return;
	if (m->canvas_x == 0.0 && m->canvas_y == 0.0) {
		m->canvas_x = m->canvas_target_x = m->m.x + m->m.width / 2.0;
		m->canvas_y = m->canvas_target_y = m->m.y + m->m.height / 2.0;
	}
	solux_inertia_reset();
	canvas_drag_mon = m;
	canvas_drag_cursor_x = cursor->x;
	canvas_drag_cursor_y = cursor->y;
	canvas_drag_start_x = m->canvas_x;
	canvas_drag_start_y = m->canvas_y;
	cursor_mode = CurCanvasMove;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
}

static void
solux_canvas_focus_client(Client *c)
{
	Monitor *m;
	double target_x, target_y;
	if (!c || !(m = c->mon) || !solux_canvas_active(m) || c->isfullscreen ||
		client_is_unmanaged(c) || c->geom.width <= 0 || c->geom.height <= 0)
		return;
	if (nfloat_spawn_focus_blocked(c))
		return;
	if (solux_pertag_restoring)
		return;
	if (solux_focus_restoring)
		return;
	if (cursor_mode == CurCanvasMove)
		return;

	/* Follow focus in Float canvas mode: keep the focused window centred in
	 * the viewport, including newly mapped windows and focusdir navigation. */
	target_x = c->geom.x + c->geom.width / 2.0;
	target_y = c->geom.y + c->geom.height / 2.0;
	if (fabs(m->canvas_target_x - target_x) < 0.5 &&
		fabs(m->canvas_target_y - target_y) < 0.5 && !m->canvas_animating)
		return;
	solux_canvas_start(m, target_x, target_y, m->canvas_target_zoom > 0.01 ? m->canvas_target_zoom : 1.0);
}

#endif
