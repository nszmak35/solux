/*
 * Client helpers kept separate from dwl.c.
 * This is the normal dwl client helper header; animation headers stay in
 * animations/ and contain only animation code.
 */
#ifndef SOLUX_CLIENT_H
#define SOLUX_CLIENT_H

static inline int
client_is_x11(Client *c)
{
#ifdef XWAYLAND
	return c && c->type == X11;
#else
	(void)c;
	return 0;
#endif
}

static inline struct wlr_surface *
client_surface(Client *c)
{
	if (!c)
		return NULL;
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland ? c->surface.xwayland->surface : NULL;
#endif
	return c->surface.xdg ? c->surface.xdg->surface : NULL;
}

static inline int
client_is_unmanaged(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland && c->surface.xwayland->override_redirect;
#endif
	return 0;
}

static inline int
toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc, LayerSurface **pl)
{
	struct wlr_xdg_surface *xdg_surface, *tmp_xdg_surface;
	struct wlr_surface *root_surface;
	struct wlr_layer_surface_v1 *layer_surface;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int type = -1;
#ifdef XWAYLAND
	struct wlr_xwayland_surface *xsurface;
#endif

	if (!s)
		return -1;
	root_surface = wlr_surface_get_root_surface(s);

#ifdef XWAYLAND
	if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(root_surface))) {
		c = xsurface->data;
		if (c)
			type = c->type;
		goto end;
	}
#endif

	if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root_surface))) {
		l = layer_surface->data;
		type = LayerShell;
		goto end;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);
	while (xdg_surface) {
		tmp_xdg_surface = NULL;
		switch (xdg_surface->role) {
		case WLR_XDG_SURFACE_ROLE_POPUP:
			if (!xdg_surface->popup || !xdg_surface->popup->parent)
				return -1;
			tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(
				xdg_surface->popup->parent);
			if (!tmp_xdg_surface)
				return toplevel_from_wlr_surface(xdg_surface->popup->parent, pc, pl);
			xdg_surface = tmp_xdg_surface;
			break;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
			c = xdg_surface->data;
			if (c)
				type = c->type;
			goto end;
		case WLR_XDG_SURFACE_ROLE_NONE:
		default:
			return -1;
		}
	}

end:
	if (pl)
		*pl = l;
	if (pc)
		*pc = c;
	return type;
}

static inline void
client_notify_enter(struct wlr_surface *s, struct wlr_keyboard *kb)
{
	if (!s)
		return;
	if (kb)
		wlr_seat_keyboard_notify_enter(seat, s, kb->keycodes,
				kb->num_keycodes, &kb->modifiers);
	else
		wlr_seat_keyboard_notify_enter(seat, s, NULL, 0, NULL);
}

static inline void
client_send_close(Client *c)
{
	if (!c)
		return;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_close(c->surface.xwayland);
		return;
	}
#endif
	wlr_xdg_toplevel_send_close(c->surface.xdg->toplevel);
}

static inline void
client_activate_surface(struct wlr_surface *s, int activated)
{
	struct wlr_xdg_toplevel *toplevel;
#ifdef XWAYLAND
	struct wlr_xwayland_surface *xsurface;
	if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(s))) {
		wlr_xwayland_surface_activate(xsurface, activated);
		return;
	}
#endif
	if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(s)))
		wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline uint32_t
client_set_bounds(Client *c, int32_t width, int32_t height)
{
	if (!c || width < 0 || height < 0)
		return 0;
#ifdef XWAYLAND
	if (client_is_x11(c))
		return 0;
#endif
#ifdef XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION
	if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
			(c->bounds.width != width || c->bounds.height != height)) {
		c->bounds.width = width;
		c->bounds.height = height;
		return wlr_xdg_toplevel_set_bounds(c->surface.xdg->toplevel,
				width, height);
	}
#endif
	return 0;
}

static inline const char *
client_get_appid(Client *c)
{
	if (!c)
		return "broken";
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->class ? c->surface.xwayland->class : "broken";
#endif
	return c->surface.xdg->toplevel->app_id ? c->surface.xdg->toplevel->app_id : "broken";
}

static inline void
client_get_clip(Client *c, struct wlr_box *clip)
{
	if (!c || !clip)
		return;
	*clip = (struct wlr_box){
		.x = 0,
		.y = 0,
		.width = MAX(1, c->geom.width - 2 * (int)c->bw),
		.height = MAX(1, c->geom.height - 2 * (int)c->bw),
	};
#ifdef XWAYLAND
	if (client_is_x11(c))
		return;
#endif
	clip->x = c->surface.xdg->geometry.x;
	clip->y = c->surface.xdg->geometry.y;
}

static inline void
client_get_geometry(Client *c, struct wlr_box *geom)
{
	if (!c || !geom)
		return;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		geom->x = c->surface.xwayland->x;
		geom->y = c->surface.xwayland->y;
		geom->width = c->surface.xwayland->width;
		geom->height = c->surface.xwayland->height;
		return;
	}
#endif
	*geom = c->surface.xdg->geometry;
}

static inline Client *
client_get_parent(Client *c)
{
	Client *p = NULL;
	if (!c)
		return NULL;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		if (c->surface.xwayland->parent)
			toplevel_from_wlr_surface(c->surface.xwayland->parent->surface, &p, NULL);
		return p;
	}
#endif
	if (c->surface.xdg->toplevel->parent)
		toplevel_from_wlr_surface(c->surface.xdg->toplevel->parent->base->surface, &p, NULL);
	return p;
}

static inline const char *
client_get_title(Client *c)
{
	if (!c)
		return "";
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->title ? c->surface.xwayland->title : "";
#endif
	return c->surface.xdg->toplevel->title ? c->surface.xdg->toplevel->title : "";
}

static inline int
client_is_float_type(Client *c)
{
	if (!c)
		return 0;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		struct wlr_xwayland_surface *s = c->surface.xwayland;
		xcb_size_hints_t *h = s->size_hints;
		if (s->modal)
			return 1;
		return h && h->min_width > 0 && h->min_height > 0 &&
			(h->max_width == h->min_width || h->max_height == h->min_height);
	}
#endif
	return c->surface.xdg->toplevel->parent ||
		(c->surface.xdg->toplevel->current.min_width > 0 &&
		 c->surface.xdg->toplevel->current.min_height > 0 &&
		 (c->surface.xdg->toplevel->current.min_width == c->surface.xdg->toplevel->current.max_width ||
		  c->surface.xdg->toplevel->current.min_height == c->surface.xdg->toplevel->current.max_height));
}

static inline int
client_has_children(Client *c)
{
	if (!c)
		return 0;
#ifdef XWAYLAND
	if (client_is_x11(c))
		return !wl_list_empty(&c->surface.xwayland->children);
#endif
	/* The xdg link contains the surface itself. */
	return wl_list_length(&c->surface.xdg->link) > 1;
}

static inline int
client_wants_focus(Client *c)
{
#ifdef XWAYLAND
	return c && client_is_unmanaged(c) &&
		wlr_xwayland_surface_override_redirect_wants_focus(c->surface.xwayland) &&
		wlr_xwayland_surface_icccm_input_model(c->surface.xwayland) != WLR_ICCCM_INPUT_MODEL_NONE;
#else
	(void)c;
	return 0;
#endif
}

static inline int
client_wants_fullscreen(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->fullscreen;
#endif
	return c->surface.xdg->toplevel->requested.fullscreen;
}

static inline void
client_set_fullscreen(Client *c, int fullscreen)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_set_fullscreen(c->surface.xwayland, fullscreen);
		return;
	}
#endif
	wlr_xdg_toplevel_set_fullscreen(c->surface.xdg->toplevel, fullscreen);
}

static inline void
client_set_scale(struct wlr_surface *s, float scale)
{
	if (!s)
		return;
	wlr_fractional_scale_v1_notify_scale(s, scale);
	wlr_surface_set_preferred_buffer_scale(s, (int32_t)ceilf(scale));
}

static inline uint32_t
client_set_size(Client *c, uint32_t width, uint32_t height)
{
	if (!c)
		return 0;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				c->geom.x, c->geom.y, width, height);
		return 0;
	}
#endif
	if (width == (uint32_t)c->surface.xdg->toplevel->current.width &&
		height == (uint32_t)c->surface.xdg->toplevel->current.height)
		return 0;
	return wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, width, height);
}

static inline void
client_set_tiled(Client *c, uint32_t edges)
{
	if (!c)
		return;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_set_maximized(c->surface.xwayland,
				edges != WLR_EDGE_NONE, edges != WLR_EDGE_NONE);
		return;
	}
#endif
	if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >=
			XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION)
		wlr_xdg_toplevel_set_tiled(c->surface.xdg->toplevel, edges);
	else
		wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel, edges != WLR_EDGE_NONE);
}

static inline void
client_set_suspended(Client *c, int suspended)
{
	if (!c)
		return;
#ifdef XWAYLAND
	if (client_is_x11(c))
		return;
#endif
	wlr_xdg_toplevel_set_suspended(c->surface.xdg->toplevel, suspended);
}

#endif /* SOLUX_CLIENT_H */
