/* wlr-foreign-toplevel-management glue. */
#ifndef SOLUX_SHELL_FOREIGN_TOPLEVEL_H
#define SOLUX_SHELL_FOREIGN_TOPLEVEL_H

void
createforeigntoplevel(Client *c)
{
	c->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_mgr);

	LISTEN(&c->foreign_toplevel->events.request_activate, &c->factivate, factivatenotify);
	LISTEN(&c->foreign_toplevel->events.request_close, &c->fclose, fclosenotify);
	LISTEN(&c->foreign_toplevel->events.request_fullscreen, &c->ffullscreen, ffullscreennotify);
	LISTEN(&c->foreign_toplevel->events.destroy, &c->fdestroy, fdestroynotify);
}

void
factivatenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, factivate);
	Monitor *m;

	(void)data;

	

	if (!c || !(m = c->mon))
		return;

	/* If the window is on another output, switch focus to that output. */
	selmon = m;

	/* Show the client's existing tags instead of changing c->tags. */
	if (c->tags && c->tags != m->tagset[m->seltags]) {
		Arg a = { .ui = c->tags };
		view(&a);
	}

	focusclient(c, 1);
	arrange(m);
	printstatus();
}

void
fclosenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fclose);
	client_send_close(c);
}

void
ffullscreennotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, ffullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	setfullscreen(c, event->fullscreen);
}

void
fdestroynotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fdestroy);
	wl_list_remove(&c->factivate.link);
	wl_list_remove(&c->fclose.link);
	wl_list_remove(&c->ffullscreen.link);
	wl_list_remove(&c->fdestroy.link);
}

#endif
