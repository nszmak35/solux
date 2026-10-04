/* dwl-ipc-unstable-v2 protocol implementation (status for bars and soluxctl). */
#ifndef SOLUX_IPC_DWL_IPC_H
#define SOLUX_IPC_DWL_IPC_H

void
dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *manager_resource = wl_resource_create(client, &zdwl_ipc_manager_v2_interface, version, id);
	if (!manager_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(manager_resource, &dwl_manager_implementation, NULL, dwl_ipc_manager_destroy);

	zdwl_ipc_manager_v2_send_tags(manager_resource, TAGCOUNT);

	for (unsigned int i = 0; i < layouts_len; i++)
		zdwl_ipc_manager_v2_send_layout(manager_resource, layouts[i].symbol);
}

void
dwl_ipc_manager_destroy(struct wl_resource *resource)
{
	/* No state to destroy */
}

void
dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output)
{
	DwlIpcOutput *ipc_output;
	Monitor *monitor = wlr_output_from_resource(output)->data;
	struct wl_resource *output_resource = wl_resource_create(client, &zdwl_ipc_output_v2_interface, wl_resource_get_version(resource), id);
	if (!output_resource)
		return;

	ipc_output = ecalloc(1, sizeof(*ipc_output));
	ipc_output->resource = output_resource;
	ipc_output->mon = monitor;
	wl_resource_set_implementation(output_resource, &dwl_output_implementation, ipc_output, dwl_ipc_output_destroy);
	wl_list_insert(&monitor->dwl_ipc_outputs, &ipc_output->link);
	dwl_ipc_output_printstatus_to(ipc_output);
}

static void
printstatus(void)
{
 	Monitor *m = NULL;
	wl_list_for_each(m, &mons, link) {
		dwl_ipc_output_printstatus(m);
		ext_workspace_printstatus(m);
	}
 }

void
dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
dwl_ipc_output_destroy(struct wl_resource *resource)
{
	DwlIpcOutput *ipc_output = wl_resource_get_user_data(resource);
	wl_list_remove(&ipc_output->link);
	free(ipc_output);
}

void
dwl_ipc_output_printstatus(Monitor *monitor)
{
	DwlIpcOutput *ipc_output;
	wl_list_for_each(ipc_output, &monitor->dwl_ipc_outputs, link)
		dwl_ipc_output_printstatus_to(ipc_output);
}

void
dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output)
{
	Monitor *monitor = ipc_output->mon;
	Client *c, *focused;
	int tagmask, state, numclients, focused_client;
	unsigned int tag;
	const char *title, *appid;
	focused = focustop(monitor);
	zdwl_ipc_output_v2_send_active(ipc_output->resource, monitor == selmon);

	for (tag = 0 ; tag < TAGCOUNT; tag++) {
		numclients = state = focused_client = 0;
		tagmask = 1 << tag;
		if ((tagmask & monitor->tagset[monitor->seltags]) != 0)
			state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE;

		wl_list_for_each(c, &clients, link) {
			if (c->mon != monitor)
				continue;
			if (!(c->tags & tagmask))
				continue;
			if (c == focused)
				focused_client = 1;
			if (c->isurgent)
				state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT;

			numclients++;
		}
		zdwl_ipc_output_v2_send_tag(ipc_output->resource, tag, state, numclients, focused_client);
	}
	title = focused ? client_get_title(focused) : "";
	appid = focused ? client_get_appid(focused) : "";

	zdwl_ipc_output_v2_send_layout(ipc_output->resource, monitor->lt[monitor->sellt] - layouts);
	zdwl_ipc_output_v2_send_title(ipc_output->resource, title);
	zdwl_ipc_output_v2_send_appid(ipc_output->resource, appid);
	zdwl_ipc_output_v2_send_layout_symbol(ipc_output->resource, monitor->ltsymbol);
	if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FULLSCREEN_SINCE_VERSION) {
		zdwl_ipc_output_v2_send_fullscreen(ipc_output->resource, focused ? focused->isfullscreen : 0);
	}
	if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FLOATING_SINCE_VERSION) {
		zdwl_ipc_output_v2_send_floating(ipc_output->resource, focused ? focused->isfloating : 0);
	}
	zdwl_ipc_output_v2_send_frame(ipc_output->resource);
}

void
dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags)
{
	DwlIpcOutput *ipc_output;
	Monitor *monitor;
	Client *selected_client;
	unsigned int newtags = 0;

	ipc_output = wl_resource_get_user_data(resource);
	if (!ipc_output)
		return;

	monitor = ipc_output->mon;
	selected_client = focustop(monitor);
	if (!selected_client)
		return;

	newtags = (selected_client->tags & and_tags) ^ xor_tags;
	if (!newtags)
		return;

	selected_client->tags = newtags;
	solux_apply_tag_layout(selected_client, monitor, newtags);
	if (selmon == monitor)
		focusclient(focustop(monitor), 1);
	arrange(selmon);
	printstatus();
}

void
dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index)
{
	DwlIpcOutput *ipc_output;
	Monitor *monitor;

	ipc_output = wl_resource_get_user_data(resource);
	if (!ipc_output)
		return;

	monitor = ipc_output->mon;
	if (index >= layouts_len)
		return;
	if (index != monitor->lt[monitor->sellt] - layouts)
		monitor->sellt ^= 1;

	monitor->lt[monitor->sellt] = &layouts[index];
	strncpy(monitor->ltsymbol, monitor->lt[monitor->sellt]->symbol,
			sizeof(monitor->ltsymbol));
	solux_animation_relayout(monitor);
	printstatus();
	dwl_ipc_output_printstatus(monitor);
	ext_workspace_printstatus(monitor);
}

void
dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset)
{
	DwlIpcOutput *ipc_output = wl_resource_get_user_data(resource);
	Monitor *monitor;
	uint32_t newtags = tagmask & TAGMASK;

	if (!ipc_output || !(monitor = ipc_output->mon) || !newtags)
		return;

	/* Route Waybar/dwl IPC through the same state transition as the normal
	 * view() command. This guarantees the tag animation is prepared before
	 * visibility is recalculated. */
	if (!toggle_tagset && newtags == monitor->tagset[monitor->seltags])
		return;

	view_on_monitor(monitor, newtags, toggle_tagset);
	printstatus();
	dwl_ipc_output_printstatus(monitor);
	ext_workspace_printstatus(monitor);
}

void
dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

#endif
