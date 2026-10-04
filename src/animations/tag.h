/* Tag (workspace) switch animation.
 *
 * Outgoing windows slide off the monitor edge and incoming ones slide in from
 * the opposite side.  The offset is purely visual: it is applied to scene nodes
 * once per frame (see rendermon), so c->geom, the animation state, the camera
 * and the per-tag float geometry are never modified. */
#ifndef SOLUX_ANIMATION_TAG_H
#define SOLUX_ANIMATION_TAG_H

static bool
solux_taganim_should_run(Monitor *m, unsigned int oldtag)
{
	if (!animations || animation_duration_tag == 0 || !m || !m->pertag)
		return false;
	if (!m->wlr_output || !m->wlr_output->enabled)
		return false;
	if (oldtag == 0 || m->pertag->curtag == 0 || oldtag == m->pertag->curtag)
		return false;
	return true;
}

static double
solux_taganim_progress(Monitor *m, double *linear)
{
	uint64_t now = solux_animation_now_ms();
	double p = 1.0;

	if (m->taganim_duration > 0)
		p = now > m->taganim_started ?
			(double)(now - m->taganim_started) / (double)m->taganim_duration : 0.0;
	p = MAX(0.0, MIN(1.0, p));
	if (linear)
		*linear = p;
	return solux_animation_factor(p, SOLUX_ANIM_TAG);
}

static int
solux_taganim_offset(Client *c, double e)
{
	return c->taganim_from + (int)lround((double)(c->taganim_to - c->taganim_from) * e);
}

static struct wlr_box
solux_taganim_base_box(Monitor *m, Client *c)
{
	struct wlr_box world = c->animation.running ? c->animation.current : c->geom;

	if (world.width <= 0 || world.height <= 0)
		world = c->geom;
	if (solux_canvas_active(m) && !c->isfullscreen)
		return solux_canvas_world_to_screen(m, world);
	return world;
}

static bool
solux_taganim_overlaps(const struct wlr_box *a, const struct wlr_box *b)
{
	return a->x < b->x + b->width && b->x < a->x + a->width &&
		a->y < b->y + b->height && b->y < a->y + a->height;
}

#define TAGANIM_BIG (1 << 20)

static bool
solux_taganim_inside(const struct wlr_box *a, const struct wlr_box *b)
{
	return a->x >= b->x && a->y >= b->y &&
		a->x + a->width <= b->x + b->width &&
		a->y + a->height <= b->y + b->height;
}

static bool
solux_taganim_fbox_eq(const struct wlr_fbox *a, const struct wlr_fbox *b)
{
	return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

/* Redraw an outgoing float window with its frozen frame and zoom.  The canvas
 * code skips such windows, so a client commit would otherwise reset the zoom
 * in the middle of the transition. */
static void
solux_taganim_apply_frozen(Client *c)
{
	struct wlr_box v = c->taganim_frozen;
	double z = c->taganim_frozen_zoom;

	if (!c->scene_surface || z <= 0.0 || v.width <= 0 || v.height <= 0)
		return;
	nfloat_apply_canvas_client(c, v, z);
	solux_canvas_update_decorations(c, v, z);
	if (c->blur) {
		int radius = MAX(0, (int)lround((double)c->corner_radius * z));
		if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
			radius = 0;
		wlr_scene_blur_set_size(c->blur, v.width, v.height);
		wlr_scene_blur_set_corner_radius(c->blur, radius);
		wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region){
			.corners = corner_radii_all(radius), .area = {0, 0, v.width, v.height}});
	}
	if (c->shadow)
		client_set_shadow_blur_sigma(c, c->shadow->blur_sigma);
}

/* Corner radii of the visible part: corners next to the cut become square. */
static struct fx_corner_radii
solux_taganim_corners(const struct wlr_box *win, const struct wlr_box *v, int radius)
{
	struct fx_corner_radii r = corner_radii_all(MAX(0, radius));

	if (v->x > win->x) {
		r.top_left = 0;
		r.bottom_left = 0;
	}
	if (v->x + v->width < win->x + win->width) {
		r.top_right = 0;
		r.bottom_right = 0;
	}
	if (v->y > win->y) {
		r.top_left = 0;
		r.top_right = 0;
	}
	if (v->y + v->height < win->y + win->height) {
		r.bottom_left = 0;
		r.bottom_right = 0;
	}
	return r;
}

static bool
solux_taganim_rounded(Client *c)
{
	return scenefx_corner_radius > 0 &&
		!(scenefx_corner_radius_only_floating && !c->isfloating) &&
		!c->isfullscreen;
}

static int
solux_taganim_buffer_radius(Client *c)
{
	return solux_taganim_rounded(c) ? scenefx_corner_radius : 0;
}

static double
solux_taganim_zoom(Client *c)
{
	Monitor *m = c->mon;

	if (!m || !c->canvas_visualized)
		return 1.0;
	if (c->taganim_role == TAGANIM_OUT && c->taganim_frozen_zoom > 0.0)
		return c->taganim_frozen_zoom;
	return m->canvas_zoom > 0.01 ? m->canvas_zoom : 1.0;
}

static void
solux_taganim_sync_plain(Client *c, struct wlr_box visual)
{
	struct wlr_box real = c->geom;

	c->geom = visual;
	if (!solux_taganim_rounded(c))
		solux_animation_apply_rect_borders(c, visual);
	update_client_rounded_borders(c);
	if (c->shadow)
		client_set_shadow_blur_sigma(c, c->shadow->blur_sigma);
	c->geom = real;
}

static void
solux_taganim_crop_rect(struct wlr_scene_rect *r, const struct wlr_box *lc)
{
	struct wlr_box b, v;

	if (!r || !r->node.enabled)
		return;
	b = (struct wlr_box){ r->node.x, r->node.y, r->width, r->height };
	if (b.width <= 0 || b.height <= 0 || !wlr_box_intersection(&v, &b, lc)) {
		wlr_scene_rect_set_size(r, 0, 0);
		return;
	}
	wlr_scene_node_set_position(&r->node, v.x, v.y);
	wlr_scene_rect_set_size(r, v.width, v.height);
}

static void
solux_taganim_crop_shadow(Client *c, const struct wlr_box *win, const struct wlr_box *lc)
{
	int sigma, radius;
	struct wlr_box sb, v, vw;
	struct fx_corner_radii hole;

	if (!c->shadow)
		return;
	sigma = MAX(0, (int)lroundf(c->shadow->blur_sigma));
	radius = c->corner_radius + (int)c->bw;
	if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
		radius = 0;
	radius = MAX(0, radius);
	sb = (struct wlr_box){ win->x - sigma, win->y - sigma,
		win->width + 2 * sigma, win->height + 2 * sigma };
	if (sb.width <= 0 || sb.height <= 0 || !wlr_box_intersection(&v, &sb, lc)) {
		if (c->shadow->node.enabled) {
			wlr_scene_node_set_enabled(&c->shadow->node, false);
			c->taganim_shadow_hidden = true;
		}
		return;
	}
	if (c->taganim_shadow_hidden) {
		wlr_scene_node_set_enabled(&c->shadow->node, true);
		c->taganim_shadow_hidden = false;
	}
	if (!wlr_box_intersection(&vw, win, lc))
		vw = (struct wlr_box){ 0, 0, 0, 0 };
	hole = solux_taganim_corners(win, &vw, radius);
	wlr_scene_node_set_position(&c->shadow->node, v.x, v.y);
	wlr_scene_shadow_set_size(c->shadow, v.width, v.height);
	wlr_scene_shadow_set_corner_radius(c->shadow,
		MAX(0, MIN(radius, MIN(v.width / 2, v.height / 2))));
	wlr_scene_shadow_set_clipped_region(c->shadow, (struct clipped_region){
		.corners = hole,
		.area = { vw.x - v.x, vw.y - v.y, vw.width, vw.height } });
}

static void
solux_taganim_crop_blur(Client *c, const struct wlr_box *win, const struct wlr_box *lc, int radius)
{
	struct wlr_box v;
	struct fx_corner_radii cr;

	if (!c->blur)
		return;
	if (win->width <= 0 || win->height <= 0 || !wlr_box_intersection(&v, win, lc)) {
		if (c->blur->node.enabled) {
			wlr_scene_node_set_enabled(&c->blur->node, false);
			c->taganim_blur_hidden = true;
		}
		return;
	}
	if (c->taganim_blur_hidden) {
		wlr_scene_node_set_enabled(&c->blur->node, true);
		c->taganim_blur_hidden = false;
	}
	cr = solux_taganim_corners(win, &v, radius);
	wlr_scene_node_set_position(&c->blur->node, v.x, v.y);
	wlr_scene_blur_set_size(c->blur, v.width, v.height);
	wlr_scene_blur_set_corner_radii(c->blur, cr);
	wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region){
		.corners = cr, .area = { 0, 0, v.width, v.height } });
}

/* Borders, shadow and blur are cropped at the same seam as the window buffers
 * instead of being hidden.  clip and vis are in screen coordinates. */
static void
solux_taganim_crop_decor(Client *c, const struct wlr_box *clip, const struct wlr_box *vis)
{
	struct wlr_box lc, win, shwin;
	double z = solux_taganim_zoom(c);
	int i;

	lc = (struct wlr_box){ clip->x - vis->x, clip->y - vis->y, clip->width, clip->height };
	win = (struct wlr_box){ 0, 0, vis->width, vis->height };

	/* Decorations are first rebuilt from their logical state, then cropped, so the
	 * crop never accumulates between frames. */
	c->taganim_crop = lc;
	c->taganim_crop_on = true;
	if (c->canvas_visualized) {
		solux_canvas_update_decorations(c, *vis, z);
		if (c->shadow)
			client_set_shadow_blur_sigma(c, c->shadow->blur_sigma);
		shwin = (struct wlr_box){ 0, 0, c->geom.width, c->geom.height };
	} else {
		solux_taganim_sync_plain(c, win);
		shwin = win;
	}
	if (!solux_taganim_rounded(c)) {
		for (i = 0; i < 4; i++) {
			solux_taganim_crop_rect(c->border[i], &lc);
			solux_taganim_crop_rect(c->borders[i], &lc);
			solux_taganim_crop_rect(c->bordere[i], &lc);
		}
	}
	solux_taganim_crop_shadow(c, &shwin, &lc);
	solux_taganim_crop_blur(c, &win, &lc, solux_taganim_rounded(c) ?
		MAX(0, (int)lround((double)c->corner_radius * z)) : 0);
}

static void
solux_taganim_restore_decor(Client *c)
{
	Monitor *m = c->mon;

	if (c->shadow && c->taganim_shadow_hidden)
		wlr_scene_node_set_enabled(&c->shadow->node, true);
	if (c->blur && c->taganim_blur_hidden)
		wlr_scene_node_set_enabled(&c->blur->node, true);
	c->taganim_shadow_hidden = false;
	c->taganim_blur_hidden = false;
	if (c->blur)
		wlr_scene_node_set_position(&c->blur->node, 0, 0);
	if (!m || !c->scene || !c->scene_surface || client_is_unmanaged(c))
		return;
	if (c->canvas_visualized) {
		if (c->taganim_role == TAGANIM_OUT) {
			if (c->taganim_frozen_zoom > 0.0)
				solux_taganim_apply_frozen(c);
		} else if (solux_canvas_active(m) && !c->isfullscreen) {
			solux_canvas_apply_client(c);
		}
	} else {
		struct wlr_box visual = (c->animation.running &&
				c->animation.current.width > 0 && c->animation.current.height > 0) ?
				c->animation.current : c->geom;

		solux_taganim_sync_plain(c, visual);
		if (c->blur) {
			int radius = solux_taganim_rounded(c) ? c->corner_radius : 0;

			wlr_scene_blur_set_size(c->blur, visual.width, visual.height);
			wlr_scene_blur_set_corner_radius(c->blur, radius);
			wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region){
				.corners = corner_radii_all(radius),
				.area = { 0, 0, visual.width, visual.height } });
		}
	}
}

typedef struct {
	Client *c;
	struct wlr_box clip;
	bool ok;
} SoluxTagClipCtx;

/* Crop one scene buffer of the window to ctx->clip.  Only the buffer
 * (source box, size, position) is touched, never the client geometry. */
static void
solux_taganim_clip_buffer(struct wlr_scene_buffer *b, int sx, int sy, void *data)
{
	SoluxTagClipCtx *x = data;
	Client *c = x->c;
	SoluxTagClipEntry *e = NULL;
	struct wlr_scene_surface *ss;
	struct wlr_fbox src, nsrc;
	struct wlr_box area, vis;
	int lx = 0, ly = 0, i;
	double kx, ky;

	(void)sx;
	(void)sy;
	if (!x->ok || !b->buffer)
		return;
	if (!(ss = wlr_scene_surface_try_from_buffer(b)) || !ss->surface ||
		wlr_xdg_popup_try_from_wlr_surface(ss->surface) != NULL ||
		wlr_surface_get_root_surface(ss->surface) != client_surface(c))
		return;
	if (b->transform != WL_OUTPUT_TRANSFORM_NORMAL || b->dst_width <= 0 || b->dst_height <= 0) {
		x->ok = false;
		return;
	}
	for (i = 0; i < c->taganim_clip_n; i++)
		if (c->taganim_clip[i].buf == b) {
			e = &c->taganim_clip[i];
			break;
		}
	if (!e) {
		if (c->taganim_clip_n >= TAGANIM_CLIP_MAX) {
			x->ok = false;
			return;
		}
		e = &c->taganim_clip[c->taganim_clip_n++];
		memset(e, 0, sizeof(*e));
		e->buf = b;
		e->orig_src = e->last_src = b->src_box;
		e->orig_dw = e->last_dw = b->dst_width;
		e->orig_dh = e->last_dh = b->dst_height;
		e->orig_x = e->last_x = b->node.x;
		e->orig_y = e->last_y = b->node.y;
		e->orig_opacity = e->last_opacity = b->opacity;
	} else {
		if (!solux_taganim_fbox_eq(&b->src_box, &e->last_src))
			e->orig_src = b->src_box;
		if (b->dst_width != e->last_dw || b->dst_height != e->last_dh) {
			e->orig_dw = b->dst_width;
			e->orig_dh = b->dst_height;
		}
		if (b->node.x != e->last_x || b->node.y != e->last_y) {
			e->orig_x = b->node.x;
			e->orig_y = b->node.y;
		}
		if (b->opacity != e->last_opacity)
			e->orig_opacity = b->opacity;
	}

	wlr_scene_node_coords(&b->node, &lx, &ly);
	area = (struct wlr_box){ lx - (b->node.x - e->orig_x), ly - (b->node.y - e->orig_y), e->orig_dw, e->orig_dh };
	src = e->orig_src;
	if (src.width <= 0 || src.height <= 0)
		src = (struct wlr_fbox){ 0, 0, b->buffer->width, b->buffer->height };

	if (!wlr_box_intersection(&vis, &area, &x->clip)) {
		wlr_scene_buffer_set_source_box(b, &e->orig_src);
		wlr_scene_buffer_set_dest_size(b, e->orig_dw, e->orig_dh);
		wlr_scene_node_set_position(&b->node, e->orig_x, e->orig_y);
		wlr_scene_buffer_set_opacity(b, 0.0f);
	} else {
		kx = src.width / (double)e->orig_dw;
		ky = src.height / (double)e->orig_dh;
		nsrc = (struct wlr_fbox){ src.x + (vis.x - area.x) * kx, src.y + (vis.y - area.y) * ky,
			vis.width * kx, vis.height * ky };
		wlr_scene_buffer_set_source_box(b, &nsrc);
		wlr_scene_buffer_set_dest_size(b, vis.width, vis.height);
		wlr_scene_node_set_position(&b->node, e->orig_x + (vis.x - area.x), e->orig_y + (vis.y - area.y));
		if (b->opacity != e->orig_opacity)
			wlr_scene_buffer_set_opacity(b, e->orig_opacity);
		if (vis.width != area.width || vis.height != area.height)
			wlr_scene_buffer_set_corner_radii(b,
				solux_taganim_corners(&area, &vis, solux_taganim_buffer_radius(c)));
		else
			update_buffer_corner_radius(c, b);
	}
	e->last_src = b->src_box;
	e->last_dw = b->dst_width;
	e->last_dh = b->dst_height;
	e->last_x = b->node.x;
	e->last_y = b->node.y;
	e->last_opacity = b->opacity;
}

static void
solux_taganim_unclip_buffer(struct wlr_scene_buffer *b, int sx, int sy, void *data)
{
	Client *c = data;
	SoluxTagClipEntry *e = NULL;
	int i;

	(void)sx;
	(void)sy;
	for (i = 0; i < c->taganim_clip_n; i++)
		if (c->taganim_clip[i].buf == b) {
			e = &c->taganim_clip[i];
			break;
		}
	if (!e)
		return;
	if (solux_taganim_fbox_eq(&b->src_box, &e->last_src))
		wlr_scene_buffer_set_source_box(b, &e->orig_src);
	if (b->dst_width == e->last_dw && b->dst_height == e->last_dh)
		wlr_scene_buffer_set_dest_size(b, e->orig_dw, e->orig_dh);
	if (b->node.x == e->last_x && b->node.y == e->last_y)
		wlr_scene_node_set_position(&b->node, e->orig_x, e->orig_y);
	if (b->opacity == e->last_opacity)
		wlr_scene_buffer_set_opacity(b, e->orig_opacity);
	update_buffer_corner_radius(c, b);
}

static void
solux_taganim_unclip(Client *c)
{
	if (!c || (!c->taganim_clipped && c->taganim_clip_n == 0))
		return;
	if (c->scene_surface && c->taganim_clip_n > 0)
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, solux_taganim_unclip_buffer, c);
	c->taganim_clip_n = 0;
	c->taganim_clipped = false;
	c->taganim_crop_on = false;
	solux_taganim_restore_decor(c);
}

static bool
solux_taganim_crop(Client *c, const struct wlr_box *clip, const struct wlr_box *vis)
{
	SoluxTagClipCtx ctx = { .c = c, .clip = *clip, .ok = true };

	if (!c->scene_surface)
		return false;
	wlr_scene_node_for_each_buffer(&c->scene_surface->node, solux_taganim_clip_buffer, &ctx);
	if (!ctx.ok) {
		solux_taganim_unclip(c);
		return false;
	}
	solux_taganim_crop_decor(c, clip, vis);
	c->taganim_clipped = true;
	return true;
}

/* Remember the on-screen boxes of visible clients before the tag changes. */
static void
solux_taganim_freeze(Monitor *m)
{
	Client *c;

	if (!m)
		return;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->scene || client_is_unmanaged(c))
			continue;
		if (c->taganim_role == TAGANIM_OUT)
			continue;
		if (!VISIBLEON(c, m))
			continue;
		c->taganim_frozen = solux_taganim_base_box(m, c);
		c->taganim_frozen_zoom = (solux_canvas_active(m) && !c->isfullscreen && c->canvas_visualized) ?
			(m->canvas_zoom > 0.01 ? m->canvas_zoom : 1.0) : 0.0;
	}
}

static void
solux_taganim_finish(Monitor *m)
{
	Client *c;
	struct wlr_box base;
	bool was_active;

	if (!m)
		return;
	was_active = m->taganim_active;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->taganim_role == TAGANIM_NONE)
			continue;
		solux_taganim_unclip(c);
		if (c->scene && !client_is_unmanaged(c)) {
			if (VISIBLEON(c, m)) {
				base = solux_taganim_base_box(m, c);
				wlr_scene_node_set_position(&c->scene->node, base.x, base.y);
				wlr_scene_node_set_enabled(&c->scene->node, true);
			} else if (c->taganim_role == TAGANIM_OUT) {
				wlr_scene_node_set_enabled(&c->scene->node, false);
			}
		}
		c->taganim_role = TAGANIM_NONE;
		c->taganim_from = 0;
		c->taganim_to = 0;
		c->taganim_frozen_zoom = 0.0;
	}
	m->taganim_active = false;
	if (was_active && m->wlr_output && m->wlr_output->enabled)
		wlr_output_schedule_frame(m->wlr_output);
}

/* Assign roles and start the animation.  Called after tagset/curtag changed
 * and before arrange(). */
static void
solux_taganim_begin(Monitor *m, uint32_t oldtagset, unsigned int oldtag)
{
	Client *c;
	double e;
	int len, sign, cur;
	bool was_vis, now_vis, had_role;

	if (!m)
		return;
	if (!solux_taganim_should_run(m, oldtag)) {
		solux_taganim_finish(m);
		return;
	}
	e = m->taganim_active ? solux_taganim_progress(m, NULL) : 1.0;
	sign = m->pertag->curtag > oldtag ? 1 : -1;
	len = tag_animation_vertical ? m->m.height : m->m.width;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->scene || client_is_unmanaged(c))
			continue;
		had_role = c->taganim_role != TAGANIM_NONE;
		cur = had_role ? solux_taganim_offset(c, e) : 0;
		was_vis = (c->tags & oldtagset) != 0;
		now_vis = VISIBLEON(c, m);
		if (now_vis && (!was_vis || had_role)) {
			c->taganim_from = had_role ? cur : sign * len;
			c->taganim_to = 0;
			c->taganim_role = TAGANIM_IN;
		} else if (!now_vis && was_vis) {
			c->taganim_from = cur;
			c->taganim_to = -sign * len;
			c->taganim_role = TAGANIM_OUT;
		} else if (!now_vis && c->taganim_role == TAGANIM_OUT) {
			c->taganim_from = cur;
		} else {
			c->taganim_role = TAGANIM_NONE;
			c->taganim_from = 0;
			c->taganim_to = 0;
		}
	}
	m->taganim_vertical = tag_animation_vertical != 0;
	m->taganim_duration = animation_duration_tag;
	m->taganim_started = solux_animation_now_ms();
	m->taganim_active = true;
	solux_animation_schedule(m);
}

/* Per-frame step: apply the offset to the scene nodes.  Called from rendermon
 * after solux_canvas_frame(). */
static void
solux_taganim_frame(Monitor *m)
{
	Client *c;
	struct wlr_box base, vis, area, clip;
	double e, lin;
	bool done, show, in_role;
	int off;

	if (!m || !m->taganim_active)
		return;
	if (!m->wlr_output || !m->wlr_output->enabled) {
		solux_taganim_finish(m);
		return;
	}
	e = solux_taganim_progress(m, &lin);
	done = lin >= 1.0;
	area = m->m;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->scene || c->taganim_role == TAGANIM_NONE)
			continue;
		if (client_is_unmanaged(c)) {
			c->taganim_role = TAGANIM_NONE;
			continue;
		}
		in_role = c->taganim_role == TAGANIM_IN;
		if (in_role != (VISIBLEON(c, m) != 0)) {
			c->taganim_role = TAGANIM_NONE;
			c->taganim_from = 0;
			c->taganim_to = 0;
			solux_taganim_unclip(c);
			if (VISIBLEON(c, m) && c->scene) {
				base = solux_taganim_base_box(m, c);
				wlr_scene_node_set_position(&c->scene->node, base.x, base.y);
				wlr_scene_node_set_enabled(&c->scene->node, true);
			} else {
				wlr_scene_node_set_enabled(&c->scene->node, false);
			}
			continue;
		}
		off = done ? c->taganim_to : solux_taganim_offset(c, e);
		base = in_role ? solux_taganim_base_box(m, c) : c->taganim_frozen;
		vis = base;
		if (m->taganim_vertical)
			vis.y += off;
		else
			vis.x += off;
		wlr_scene_node_set_position(&c->scene->node, vis.x, vis.y);
		if (!in_role && !done && c->canvas_visualized && c->taganim_frozen_zoom > 0.0)
			solux_taganim_apply_frozen(c);
		show = in_role ? true : !done;
		if (show && !done) {
			clip = (struct wlr_box){ -TAGANIM_BIG, -TAGANIM_BIG, 2 * TAGANIM_BIG, 2 * TAGANIM_BIG };
			if (m->taganim_vertical) {
				if (off > 0) {
					clip.y = area.y + off;
					clip.height = TAGANIM_BIG - clip.y;
				} else if (off < 0) {
					clip.height = area.y + area.height + off - clip.y;
				}
			} else {
				if (off > 0) {
					clip.x = area.x + off;
					clip.width = TAGANIM_BIG - clip.x;
				} else if (off < 0) {
					clip.width = area.x + area.width + off - clip.x;
				}
			}
			if (!solux_taganim_overlaps(&vis, &area) || !solux_taganim_overlaps(&vis, &clip)) {
				show = false;
			} else if (solux_taganim_inside(&vis, &clip)) {
				if (c->taganim_clipped) {
					solux_taganim_unclip(c);
					wlr_scene_node_set_position(&c->scene->node, vis.x, vis.y);
				}
			} else if (solux_taganim_crop(c, &clip, &vis)) {
				wlr_scene_node_set_position(&c->scene->node, vis.x, vis.y);
			} else {
				show = false;
			}
		}
		if (!show && (c->taganim_clipped || c->taganim_clip_n > 0)) {
			solux_taganim_unclip(c);
			wlr_scene_node_set_position(&c->scene->node, vis.x, vis.y);
		}
		wlr_scene_node_set_enabled(&c->scene->node, show);
	}
	if (done)
		solux_taganim_finish(m);
	else
		wlr_output_schedule_frame(m->wlr_output);
}

#endif
