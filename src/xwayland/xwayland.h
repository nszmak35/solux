/* XWayland client handling. */
#ifndef SOLUX_XWAYLAND_XWAYLAND_H
#define SOLUX_XWAYLAND_XWAYLAND_H

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (!client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);

	/* Same as XDG clients (commitnotify initial commit): capture the camera
	 * point that is visible now, before map/applyrules()/setmon() can refocus
	 * another client and move the canvas.  Without this XWayland windows have
	 * no spawn point and fall back to a fixed position. */
	if (!client_is_unmanaged(c))
		nfloat_capture_spawn_canvas(c);

	LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
	LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	if (!client_surface(c) || !client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if ((c->isfloating && c != grabc) || !c->mon->lt[c->mon->sellt]->arrange) {
		resize(c, (struct wlr_box){.x = event->x - c->bw,
				.y = event->y - c->bw, .width = event->width + c->bw * 2,
				.height = event->height + c->bw * 2}, 0);
	} else {
		arrange(c->mon);
	}
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xsurface = data;
	Client *c;

	/* Allocate a Client for this surface */
	c = xsurface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xsurface;
	c->type = X11;
	/* The XWayland surface is not associated yet here, so client_surface(c)
	 * is NULL.  Never call client_is_unmanaged() before associatex11().
	 * This was the direct cause of the XWayland startup SIGSEGV. */
	c->bw = borderpx;
	c->bws = borderspx;
	c->bwe = borderepx;

	

	c->opacity_unfocus = scenefx_opacity_inactive;
	c->opacity_focus = scenefx_opacity_active;
	c->opacity = c->opacity_unfocus;
	c->corner_radius = scenefx_corner_radius;

	/* Listen to the various events it can emit */
	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_minimize, &c->minimize, minimizenotify);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	if (nfloat_spawn_pending == c)
		nfloat_spawn_pending = NULL;
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

void
minimizenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, minimize);
	struct wlr_xwayland_surface *xsurface = c->surface.xwayland;
	struct wlr_xwayland_minimize_event *e = data;
	int focused;

	if (xsurface->surface == NULL || !xsurface->surface->mapped)
		return;

	focused = seat->keyboard_state.focused_surface == xsurface->surface;
	wlr_xwayland_surface_set_minimized(xsurface, !focused && e->minimize);
}

void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	struct wlr_surface *surface = client_surface(c);
	if (c == focustop(selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);

	if (c->isurgent && surface && surface->mapped)
		setclientborderstate(c, BorderUrgent);
}


void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;

	wlr_xwayland_set_seat(xwayland, seat);

	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
		wlr_xwayland_set_cursor(xwayland,
			wlr_xcursor_image_get_buffer(xcursor->images[0]),
			xcursor->images[0]->hotspot_x,
			xcursor->images[0]->hotspot_y);

	setenv("DISPLAY", xwayland->display_name, 1);
}

#endif

#endif
