/* Client decorations: borders, corner radius, shadow, blur and opacity (SceneFX). */
#ifndef SOLUX_EFFECTS_DECORATIONS_H
#define SOLUX_EFFECTS_DECORATIONS_H

/* Applies one rounded decoration ring (position, size, outer radius, inner hole).
 * Normally this is exactly the plain sequence of scene calls.  While the window
 * is cut at the seam of a tag transition (c->taganim_crop_on) the ring is
 * reduced to the visible part instead of being hidden: the hole is extended to
 * the cut edge (no border line at an artificial edge) and the hole corners that
 * touch the cut are squared.  Coordinates are window-local. */
static void
solux_decor_ring_apply(Client *c, struct wlr_scene_rect *ring, int x, int y,
		int w, int h, int thickness, int radius, int inner_radius)
{
	struct clipped_region clip;
	struct fx_corner_radii hole_r;
	struct wlr_box rb, v;
	int hx0, hy0, hx1, hy1;

	if (!c->taganim_crop_on) {
		wlr_scene_node_set_position(&ring->node, x, y);
		wlr_scene_rect_set_size(ring, w, h);
		wlr_scene_rect_set_corner_radius(ring, MAX(0, radius));
		clip = (struct clipped_region){ .corners = corner_radii_all(MAX(0, inner_radius)),
			.area = { thickness, thickness, MAX(0, w - 2 * thickness),
				MAX(0, h - 2 * thickness) } };
		wlr_scene_rect_set_clipped_region(ring, clip);
		return;
	}

	rb = (struct wlr_box){ x, y, w, h };
	if (w <= 0 || h <= 0 || !wlr_box_intersection(&v, &rb, &c->taganim_crop) ||
			v.width <= 0 || v.height <= 0) {
		wlr_scene_rect_set_size(ring, 0, 0);
		return;
	}
	hx0 = MAX(x + thickness, v.x);
	hy0 = MAX(y + thickness, v.y);
	hx1 = MIN(x + w - thickness, v.x + v.width);
	hy1 = MIN(y + h - thickness, v.y + v.height);
	hole_r = corner_radii_all(MAX(0, inner_radius));
	if (hx0 > x + thickness) { hole_r.top_left = 0; hole_r.bottom_left = 0; }
	if (hx1 < x + w - thickness) { hole_r.top_right = 0; hole_r.bottom_right = 0; }
	if (hy0 > y + thickness) { hole_r.top_left = 0; hole_r.top_right = 0; }
	if (hy1 < y + h - thickness) { hole_r.bottom_left = 0; hole_r.bottom_right = 0; }
	wlr_scene_node_set_position(&ring->node, v.x, v.y);
	wlr_scene_rect_set_size(ring, v.width, v.height);
	wlr_scene_rect_set_corner_radius(ring,
		MAX(0, MIN(radius, MIN(v.width / 2, v.height / 2))));
	clip = (struct clipped_region){ .corners = hole_r,
		.area = { hx0 - v.x, hy0 - v.y, MAX(0, hx1 - hx0), MAX(0, hy1 - hy0) } };
	wlr_scene_rect_set_clipped_region(ring, clip);
}

void
scenebuffersetopacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	Client *c = data;
	/* xdg-popups are children of Client.scene, we do not have to worry about
	 * messing with them. */
	wlr_scene_buffer_set_opacity(buffer, c->isfullscreen ? 1.0f : c->opacity);
}

static void
setclientborderstate(Client *c, int state)
{
	const float *original = bordercolor;
	const float *start = borderscolor;
	const float *end = borderecolor;
	const float *active_original = bordercolor;
	const float *active_start = borderscolor;
	const float *active_end = borderecolor;
	int i;

	if (!c || !c->scene)
		return;

	switch (state) {
	case BorderFocus:
		active_original = focuscolor;
		active_start = borders_focuscolor;
		active_end = bordere_focuscolor;
		break;
	case BorderUrgent:
		active_original = urgentcolor;
		active_start = borders_urgentcolor;
		active_end = bordere_urgentcolor;
		break;
	case BorderNormal:
	default:
		break;
	}

	for (i = 0; i < 4; i++) {
		wlr_scene_rect_set_color(c->border[i], original);
		wlr_scene_rect_set_color(c->borders[i], start);
		wlr_scene_rect_set_color(c->bordere[i], end);
	}

	switch (border_color_type) {
	case BrdStart:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->borders[i], active_start);
		break;
	case BrdEnd:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->bordere[i], active_end);
		break;
	case BrdStartEnd:
		for (i = 0; i < 4; i++) {
			wlr_scene_rect_set_color(c->borders[i], active_start);
			wlr_scene_rect_set_color(c->bordere[i], active_end);
		}
		break;
	case BrdOriginal:
	default:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->border[i], active_original);
		break;
	}

	/* Keep the normal border rectangles visible at all times.  SceneFX rounded
	 * decoration nodes are an additional layer, not a replacement for the
	 * configured border colors.  This avoids the old transparent-border state
	 * that made borders disappear when corner radius was enabled. */
	if (c->round_border && c->round_borders && c->round_bordere) {
		const float *round_original = active_original;
		const float *round_start = active_start;
		const float *round_end = active_end;
		wlr_scene_rect_set_color(c->round_border, round_original);
		wlr_scene_rect_set_color(c->round_borders, round_start);
		wlr_scene_rect_set_color(c->round_bordere, round_end);
	}

}

void
iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface;
	struct wlr_xdg_surface *xdg_surface;

	(void)sx;
	(void)sy;

	if (!c || !buffer || !(scene_surface = wlr_scene_surface_try_from_buffer(buffer)))
		return;

#ifdef XWAYLAND
	if (c->type == X11) {
		if (scene_surface->surface != client_surface(c))
			return;
		wlr_scene_buffer_set_opacity(buffer, c->opacity);
		update_buffer_corner_radius(c, buffer);
		return;
	}
#endif

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;
	if (!wlr_subsurface_try_from_wlr_surface(xdg_surface->surface)) {
		wlr_scene_buffer_set_opacity(buffer, c->opacity);
		update_buffer_corner_radius(c, buffer);
	}
}

void
iter_xdg_scene_buffers_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface;
	struct wlr_xdg_surface *xdg_surface;

	(void)sx;
	(void)sy;

	if (!c || !buffer || !(scene_surface = wlr_scene_surface_try_from_buffer(buffer)))
		return;

#ifdef XWAYLAND
	if (c->type == X11) {
		if (scene_surface->surface == client_surface(c))
			wlr_scene_buffer_set_opacity(buffer, c->opacity);
		return;
	}
#endif

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;
	if (!wlr_subsurface_try_from_wlr_surface(xdg_surface->surface))
		wlr_scene_buffer_set_opacity(buffer, c->opacity);
}

void
iter_xdg_scene_buffers_corner_radius(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface;
	struct wlr_xdg_surface *xdg_surface;

	(void)sx;
	(void)sy;

	if (!c || !buffer || !(scene_surface = wlr_scene_surface_try_from_buffer(buffer)))
		return;

#ifdef XWAYLAND
	if (c->type == X11) {
		if (scene_surface->surface == client_surface(c))
			update_buffer_corner_radius(c, buffer);
		return;
	}
#endif

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;
	if (!wlr_subsurface_try_from_wlr_surface(xdg_surface->surface))
		update_buffer_corner_radius(c, buffer);
}

int
in_shadow_ignore_list(const char *str)
{
	size_t i;
	if (!str)
		return 0;
	for (i = 0; scenefx_shadow_ignore_list[i]; i++)
		if (!strcmp(scenefx_shadow_ignore_list[i], str))
			return 1;
	return 0;
}

void
client_set_shadow_blur_sigma(Client *c, float blur_sigma)
{
	int sigma, radius;

	if (!c || !c->shadow)
		return;
	sigma = MAX(0, (int)lroundf(blur_sigma));
	radius = c->corner_radius + (int)c->bw;
	if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
		radius = 0;
	radius = MAX(0, radius);
	wlr_scene_shadow_set_blur_sigma(c->shadow, sigma);
	wlr_scene_node_set_position(&c->shadow->node, -sigma, -sigma);
	wlr_scene_shadow_set_size(c->shadow,
			MAX(0, c->geom.width + sigma * 2),
			MAX(0, c->geom.height + sigma * 2));
	wlr_scene_shadow_set_corner_radius(c->shadow, radius);
	wlr_scene_shadow_set_clipped_region(c->shadow, (struct clipped_region) {
		.corners = corner_radii_all(radius),
		.area = { sigma, sigma, MAX(0, c->geom.width), MAX(0, c->geom.height) }
	});
}

void
update_client_rounded_borders(Client *c)
{
	int radius, inner_radius;
	int x, y, w, h;
	int thickness;
	int rounded;

	if (!c || !c->scene || !c->scene_surface)
		return;

	rounded = scenefx_corner_radius > 0 &&
		!(scenefx_corner_radius_only_floating && !c->isfloating) &&
		!c->isfullscreen;

	/* A rounded decoration is a replacement for the three ordinary rectangular
	 * border sets.  Keeping both enabled was the reason an extra square frame
	 * remained visible on top of the rounded decoration. */
	if (!rounded) {
		if (c->round_border)
			wlr_scene_node_set_enabled(&c->round_border->node, 0);
		if (c->round_borders)
			wlr_scene_node_set_enabled(&c->round_borders->node, 0);
		if (c->round_bordere)
			wlr_scene_node_set_enabled(&c->round_bordere->node, 0);
		for (int i = 0; i < 4; i++) {
			wlr_scene_node_set_enabled(&c->border[i]->node, 1);
			wlr_scene_node_set_enabled(&c->borders[i]->node, c->bws > 0);
			wlr_scene_node_set_enabled(&c->bordere[i]->node, c->bwe > 0);
		}
		return;
	}

	/* Create the rounded nodes lazily.  This makes 0 -> radius -> 0 -> radius
	 * reloads safe without ever dereferencing a partially-created node set. */
	if (!c->round_border)
		c->round_border = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
	if (!c->round_borders)
		c->round_borders = wlr_scene_rect_create(c->scene, 0, 0, borderscolor);
	if (!c->round_bordere)
		c->round_bordere = wlr_scene_rect_create(c->scene, 0, 0, borderecolor);

	if (!c->round_border || !c->round_borders || !c->round_bordere)
		return;

	c->round_border->node.data = c;
	c->round_borders->node.data = c;
	c->round_bordere->node.data = c;

	/* Rounded decorations must sit above the client surface but below nothing
	 * that should visually cover the configured frame. */
	wlr_scene_node_place_above(&c->round_border->node, &c->scene_surface->node);
	wlr_scene_node_place_above(&c->round_borders->node, &c->scene_surface->node);
	wlr_scene_node_place_above(&c->round_bordere->node, &c->scene_surface->node);

	if (c->geom.width <= 0 || c->geom.height <= 0)
		return;

	/* Disable the old square rectangles while SceneFX rounding is active. */
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&c->border[i]->node, 0);
		wlr_scene_node_set_enabled(&c->borders[i]->node, 0);
		wlr_scene_node_set_enabled(&c->bordere[i]->node, 0);
	}

	/* ---------------------------------------------------------------
	 * borderpx: the main/base ring.
	 * Outer radius = requested corner radius + ring thickness.
	 * Inner radius = the same scenefx_corner_radius.
	 * --------------------------------------------------------------- */
	thickness = (int)c->bw;
	if (thickness > 0) {
		radius = c->corner_radius + thickness;
		inner_radius = c->corner_radius;
		radius = MIN(radius, MIN(c->geom.width / 2, c->geom.height / 2));
		inner_radius = MIN(inner_radius,
			MAX(0, MIN((c->geom.width - 2 * thickness) / 2,
				   (c->geom.height - 2 * thickness) / 2)));

		solux_decor_ring_apply(c, c->round_border, 0, 0, c->geom.width, c->geom.height, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_border->node, 1);
	} else {
		wlr_scene_node_set_enabled(&c->round_border->node, 0);
	}

	/* ---------------------------------------------------------------
	 * borderspx: the start/outer detail ring.
	 * Its offset is measured inward from the client rectangle.
	 * --------------------------------------------------------------- */
	thickness = (int)c->bws;
	if (thickness > 0) {
		x = (int)borderspx_offset;
		y = x;
		w = MAX(0, c->geom.width - 2 * x);
		h = MAX(0, c->geom.height - 2 * y);
		radius = c->corner_radius - x + thickness;
		inner_radius = c->corner_radius - x;
		radius = MAX(0, MIN(radius, MIN(w / 2, h / 2)));
		inner_radius = MAX(0, MIN(inner_radius,
			MAX(0, MIN((w - 2 * thickness) / 2,
				   (h - 2 * thickness) / 2))));

		solux_decor_ring_apply(c, c->round_borders, x, y, w, h, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_borders->node, 1);
	} else {
		wlr_scene_node_set_enabled(&c->round_borders->node, 0);
	}

	/* ---------------------------------------------------------------
	 * borderepx: the end/inner detail ring.  The geometry is kept identical
	 * to the normal bordere geometry, but its outer and inner edges are rounded.
	 * --------------------------------------------------------------- */
	thickness = (int)c->bwe;
	if (thickness > 0) {
		x = (int)c->bw - thickness - (int)borderepx_negative_offset;
		y = x;
		w = MAX(0, c->geom.width - 2 * ((int)c->bw - thickness)
			+ 2 * (int)borderepx_negative_offset);
		h = MAX(0, c->geom.height - 2 * ((int)c->bw - thickness)
			+ 2 * (int)borderepx_negative_offset);
		radius = c->corner_radius - x + thickness;
		inner_radius = c->corner_radius - x;
		radius = MAX(0, MIN(radius, MIN(w / 2, h / 2)));
		inner_radius = MAX(0, MIN(inner_radius,
			MAX(0, MIN((w - 2 * thickness) / 2,
				   (h - 2 * thickness) / 2))));

		solux_decor_ring_apply(c, c->round_bordere, x, y, w, h, thickness, radius, inner_radius);
		wlr_scene_node_set_enabled(&c->round_bordere->node, 1);
	} else {
		wlr_scene_node_set_enabled(&c->round_bordere->node, 0);
	}
}

void
update_client_corner_radius(Client *c)
{
	int radius;

	if (!c)
		return;
	radius = c->corner_radius + (int)c->bw;
	if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
		radius = 0;

	update_client_rounded_borders(c);
	if (c->scene_surface)
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				iter_xdg_scene_buffers_corner_radius, c);

	if (c->blur) {
		wlr_scene_blur_set_corner_radius(c->blur,
				radius > 0 ? c->corner_radius : 0);
		wlr_scene_blur_set_clipped_region(c->blur, (struct clipped_region) {
			.corners = corner_radii_all(c->corner_radius),
			.area = { 0, 0, c->geom.width, c->geom.height }
		});
	}
}

void
update_client_shadow_color(Client *c)
{
	const float *color;
	int enabled = 1;

	if (!c || !c->shadow)
		return;
	color = (c->mon && focustop(c->mon) == c)
		? scenefx_shadow_color_focus : scenefx_shadow_color;
	if ((scenefx_shadow_only_floating && !c->isfloating) ||
			in_shadow_ignore_list(client_get_appid(c)) || c->isfullscreen) {
		color = scenefx_transparent;
		enabled = 0;
	}
	wlr_scene_shadow_set_color(c->shadow, color);
	c->has_shadow_enabled = enabled;
	wlr_scene_node_set_enabled(&c->shadow->node, scenefx_shadow && enabled);
	client_set_shadow_blur_sigma(c,
			(c->mon && focustop(c->mon) == c)
				? scenefx_shadow_blur_sigma_focus
				: scenefx_shadow_blur_sigma);
}

void
update_client_focus_decorations(Client *c, int focused, int urgent)
{
	if (!c)
		return;

	/* Border colors and rounded-border visibility are handled atomically by
	 * setclientborderstate().  Do not overwrite the rounded color here. */
	(void)urgent;

	if (c->shadow) {
		client_set_shadow_blur_sigma(c,
				focused ? scenefx_shadow_blur_sigma_focus : scenefx_shadow_blur_sigma);
		if (c->has_shadow_enabled)
			wlr_scene_shadow_set_color(c->shadow,
					focused ? scenefx_shadow_color_focus : scenefx_shadow_color);
	}

	if (c->scene_surface) {
		c->opacity = focused ? scenefx_opacity_active : scenefx_opacity_inactive;
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				iter_xdg_scene_buffers_opacity, c);
	}
}

void
update_client_blur(Client *c)
{
	if (!c || !c->blur)
		return;
	wlr_scene_blur_set_should_only_blur_bottom_layer(c->blur, !scenefx_blur_xray);
	wlr_scene_blur_set_strength(c->blur, 1.0f);
	wlr_scene_blur_set_alpha(c->blur, 1.0f);
	update_client_corner_radius(c);
}

void
update_buffer_corner_radius(Client *c, struct wlr_scene_buffer *buffer)
{
	int radius;

	if (!c || !buffer || scenefx_corner_radius <= 0)
		return;
	radius = scenefx_corner_radius;
	if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
		radius = 0;
	wlr_scene_buffer_set_corner_radius(buffer, radius);
}

void
update_scenefx_runtime(void)
{
	Client *c;
	Monitor *m;

	if (scene)
		wlr_scene_set_blur_data(scene, scenefx_blur_num_passes,
				scenefx_blur_radius, scenefx_blur_noise,
				scenefx_blur_brightness, scenefx_blur_contrast,
				scenefx_blur_saturation);

	wl_list_for_each(m, &mons, link) {
		Client *fc;
		int monitor_fullscreen = 0;

		wl_list_for_each(fc, &clients, link) {
			if (fc->mon == m && fc->isfullscreen && VISIBLEON(fc, m)) {
				monitor_fullscreen = 1;
				break;
			}
		}

		if (scenefx_blur && !m->blur_layer) {
			m->blur_layer = wlr_scene_optimized_blur_create(&scene->tree, 0, 0);
			wlr_scene_node_reparent(&m->blur_layer->node, layers[LyrBlur]);
		}
		if (m->blur_layer) {
			wlr_scene_node_set_enabled(&m->blur_layer->node,
				scenefx_blur && !monitor_fullscreen);
			wlr_scene_optimized_blur_set_size(m->blur_layer,
					m->m.width, m->m.height);
			if (scenefx_blur && !monitor_fullscreen)
				wlr_scene_optimized_blur_mark_dirty(m->blur_layer);
		}
	}

	wl_list_for_each(c, &clients, link) {
		if (!client_surface(c)->mapped || !c->scene || client_is_unmanaged(c))
			continue;

		c->corner_radius = scenefx_corner_radius;

		update_client_rounded_borders(c);

		if (scenefx_shadow && !c->shadow) {
			c->shadow = wlr_scene_shadow_create(c->scene, 0, 0,
					c->corner_radius, scenefx_shadow_blur_sigma,
					scenefx_shadow_color);
			wlr_scene_node_lower_to_bottom(&c->shadow->node);
		}
		if (c->shadow)
			wlr_scene_node_set_enabled(&c->shadow->node, scenefx_shadow);

		if (scenefx_blur && !c->blur) {
			c->blur = wlr_scene_blur_create(c->scene, 0, 0);
			wlr_scene_node_place_below(&c->blur->node, &c->scene_surface->node);
		}
		if (c->blur)
			wlr_scene_node_set_enabled(&c->blur->node, scenefx_blur);

		update_client_corner_radius(c);
		update_client_shadow_color(c);
		update_client_blur(c);

		c->opacity = (c == focustop(c->mon)) ? scenefx_opacity_active : scenefx_opacity_inactive;
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				iter_xdg_scene_buffers_opacity, c);

		setclientborderstate(c,
				c->isurgent ? BorderUrgent :
				(c == focustop(c->mon) ? BorderFocus : BorderNormal));
		resize(c, c->geom, 0);
	}
}

void
setopacityunfocus(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;

	sel->opacity_unfocus += arg->f;
	if (sel->opacity_unfocus > 1.0)
		sel->opacity_unfocus = 1.0f;

	if (sel->opacity_unfocus < 0.1)
		sel->opacity_unfocus = 0.1f;

	wlr_scene_node_for_each_buffer(&sel->scene_surface->node, scenebuffersetopacity, sel);
}

void
setopacityfocus(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;

	sel->opacity_focus += arg->f;
	if (sel->opacity_focus > 1.0)
		sel->opacity_focus = 1.0f;

	if (sel->opacity_focus < 0.1)
		sel->opacity_focus = 0.1f;

	/* Change opacity from current client */
	sel->opacity = sel->opacity_focus;

	wlr_scene_node_for_each_buffer(&sel->scene_surface->node, scenebuffersetopacity, sel);
}

#endif
