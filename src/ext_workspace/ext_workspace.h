#include "wlr_ext_workspace_v1.h"

#define EXT_WORKSPACE_CAPS \
	(EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE | \
	 EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_DEACTIVATE)

/* types */

struct ext_workspace {
	struct wl_list link;           
	uint32_t tag;                  
	Monitor *m;
	struct wlr_ext_workspace_handle_v1 *handle;
};

/* global */

static struct wlr_ext_workspace_manager_v1 *ext_workspace_manager;
static struct wl_list ext_workspaces; 
static struct wl_listener ext_commit_listener;
static struct wl_listener ext_destroy_listener;

/* helpers */

static int
get_tag_status(uint32_t tag, Monitor *m)
{
	Client *c;
	uint32_t mask = 1 << (tag - 1);

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		if (!(c->tags & mask))
			continue;
		if (c->isurgent)
			return 2;
		return 1;
	}
	return 0;
}

static const char *
tag_name(uint32_t tag)
{
	static const char *names[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9",
	};
	if (tag >= 1 && tag <= LENGTH(names))
		return names[tag - 1];
	return "?";
}

/* workspace lifecycle */

static void
ext_workspace_destroy(struct ext_workspace *ws)
{
	wlr_ext_workspace_handle_v1_destroy(ws->handle);
	wl_list_remove(&ws->link);
	free(ws);
}

static void
ext_workspace_create(uint32_t tag, Monitor *m)
{
	const char *name = tag_name(tag);

	struct ext_workspace *ws = ecalloc(1, sizeof(*ws));
	ws->tag = tag;
	ws->m = m;
	ws->handle = wlr_ext_workspace_handle_v1_create(
			ext_workspace_manager, name, EXT_WORKSPACE_CAPS);
	ws->handle->data = ws;

	wlr_ext_workspace_handle_v1_set_group(ws->handle, m->ext_group);
	wlr_ext_workspace_handle_v1_set_name(ws->handle, name);

	wl_list_insert(ext_workspaces.prev, &ws->link);
}

/* monitor integration */

static void
ext_workspace_createmon(Monitor *m)
{
	uint32_t i;

	m->ext_group = wlr_ext_workspace_group_handle_v1_create(
			ext_workspace_manager, 0);
	wlr_ext_workspace_group_handle_v1_output_enter(
			m->ext_group, m->wlr_output);

	for (i = 1; i <= TAGCOUNT; i++)
		ext_workspace_create(i, m);
}

static void
ext_workspace_cleanupmon(Monitor *m)
{
	struct ext_workspace *ws, *tmp;
	wl_list_for_each_safe(ws, tmp, &ext_workspaces, link)
		if (ws->m == m)
			ext_workspace_destroy(ws);

	wlr_ext_workspace_group_handle_v1_output_leave(
			m->ext_group, m->wlr_output);
	wlr_ext_workspace_group_handle_v1_destroy(m->ext_group);
	m->ext_group = NULL;
}

/* status */

static void
ext_workspace_printstatus(Monitor *m)
{
	struct ext_workspace *ws;

	wl_list_for_each(ws, &ext_workspaces, link) {
		if (ws->m != m)
			continue;

		int status = get_tag_status(ws->tag, m);
		int active = !!(m->tagset[m->seltags] & (1 << (ws->tag - 1)) & TAGMASK);

		wlr_ext_workspace_handle_v1_set_urgent(ws->handle, status == 2);
		wlr_ext_workspace_handle_v1_set_hidden(ws->handle, 0);
		wlr_ext_workspace_handle_v1_set_active(ws->handle, active);
	}
}

/* commit handler */

static void
handle_ext_commit(struct wl_listener *listener, void *data)
{
	struct wlr_ext_workspace_v1_commit_event *event = data;
	struct wlr_ext_workspace_v1_request *req;

	wl_list_for_each(req, event->requests, link) {
		struct ext_workspace *ws = NULL;
		struct ext_workspace *w;

		switch (req->type) {
		case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE:
			if (!req->activate.workspace)
				break;
			wl_list_for_each(w, &ext_workspaces, link) {
				if (w->handle == req->activate.workspace) {
					ws = w;
					break;
				}
			}
			if (!ws)
				break;
			view(&(Arg){.ui = 1 << (ws->tag - 1)});
			break;

		case WLR_EXT_WORKSPACE_V1_REQUEST_DEACTIVATE:
			if (!req->deactivate.workspace)
				break;
			wl_list_for_each(w, &ext_workspaces, link) {
				if (w->handle == req->deactivate.workspace) {
					ws = w;
					break;
				}
			}
			if (!ws)
				break;
			toggleview(&(Arg){.ui = 1 << (ws->tag - 1)});
			break;

		default:
			break;
		}
	}
}

/* init */

static void
handle_ext_destroy(struct wl_listener *listener, void *data)
{
	wl_list_remove(&ext_commit_listener.link);
	wl_list_remove(&ext_destroy_listener.link);
}

static void
workspaces_init(void)
{
	ext_workspace_manager = wlr_ext_workspace_manager_v1_create(dpy, 1);
	wl_list_init(&ext_workspaces);

	ext_commit_listener.notify = handle_ext_commit;
	wl_signal_add(&ext_workspace_manager->events.commit,
			&ext_commit_listener);

	ext_destroy_listener.notify = handle_ext_destroy;
	wl_signal_add(&ext_workspace_manager->events.destroy,
			&ext_destroy_listener);
}
