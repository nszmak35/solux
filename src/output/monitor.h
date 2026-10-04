/* Monitors: creation, rules, output management, frame scheduling and rendering. */
#ifndef SOLUX_OUTPUT_MONITOR_H
#define SOLUX_OUTPUT_MONITOR_H

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	DwlIpcOutput *ipc_output, *ipc_output_tmp;

	ext_workspace_cleanupmon(m);

	wl_list_for_each_safe(ipc_output, ipc_output_tmp, &m->dwl_ipc_outputs, link)
		wl_resource_destroy(ipc_output->resource);

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}


	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	monitor_stop_skip_frame_timer(m);
	if (m->skip_frame_timeout) {
		wl_event_source_remove(m->skip_frame_timeout);
		m->skip_frame_timeout = NULL;
	}
	if (m->pertag) {
		for (i = 0; i <= TAGCOUNT; i++) {
			solux_dwindle_tree_free(m->pertag->dwindle_roots[i]);
			m->pertag->dwindle_roots[i] = NULL;
		}
	}
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	free(m->pertag);
	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setmon(c, selmon, c->tags);
	}
	focusclient(focustop(selmon), 1);
}

static void
apply_monitor_rule(Monitor *m)
{
	const MonitorRule *r = NULL;
	struct wlr_output_state state;
	int auto_position;
	int position_changed;
	unsigned int tag;

	if (!m || !m->wlr_output)
		return;

	/* Exactly the same first-match semantics as createmon(). */
	for (size_t i = 0; i < monrules_len; i++) {
		if (!monrules[i].name || strstr(m->wlr_output->name, monrules[i].name)) {
			r = &monrules[i];
			break;
		}
	}
	if (!r)
		return;

	auto_position = (r->x == -1 && r->y == -1);
	position_changed = auto_position
		? !m->monrule_auto_position
		: (m->m.x != r->x || m->m.y != r->y);

	

	wlr_output_state_init(&state);
	wlr_output_state_set_scale(&state, r->scale > 0 ? r->scale : 1.0f);
	wlr_output_state_set_transform(&state, r->rr);

	if (!wlr_output_commit_state(m->wlr_output, &state)) {
		fprintf(stderr, "solux: failed to apply monitor rule to %s\n",
			m->wlr_output->name);
		wlr_output_state_finish(&state);
		return;
	}
	wlr_output_state_finish(&state);

	/*
	 * Reinsert only when the rule actually changes placement semantics.
	 * This avoids moving an automatically positioned monitor on every reload.
	 */
	if (position_changed && m->wlr_output->enabled) {
		wlr_output_layout_remove(output_layout, m->wlr_output);
		if (auto_position)
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
		else
			wlr_output_layout_add(output_layout, m->wlr_output, r->x, r->y);
	}
	m->monrule_auto_position = auto_position;

	

	m->mfact = r->mfact < 0.1f ? 0.1f : (r->mfact > 0.9f ? 0.9f : r->mfact);
	m->nmaster = MAX(1, r->nmaster);
	m->lt[0] = r->lt ? r->lt : &layouts[0];
	m->lt[1] = &layouts[layouts_len > 1 && m->lt[0] != &layouts[1]];
	m->sellt = 0;

	if (m->pertag) {
		for (tag = 0; tag <= TAGCOUNT; tag++) {
			m->pertag->nmasters[tag] = m->nmaster;
			m->pertag->mfacts[tag] = m->mfact;
			m->pertag->ltidxs[tag][0] = m->lt[0];
			m->pertag->ltidxs[tag][1] = m->lt[1];
			m->pertag->sellts[tag] = 0;
		}
	}

	strncpy(m->ltsymbol, m->lt[0]->symbol, sizeof(m->ltsymbol));
	m->ltsymbol[sizeof(m->ltsymbol) - 1] = '\0';
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	wl_list_init(&m->dwl_ipc_outputs);

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	m->gaps = gaps;

	m->tagset[0] = m->tagset[1] = 1;
	for (r = monrules; r < monrules + monrules_len; r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			m->mfact = r->mfact;
			m->nmaster = r->nmaster;
			m->lt[0] = r->lt;
			m->lt[1] = &layouts[layouts_len > 1 && r->lt != &layouts[1]];
			strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}
	m->monrule_auto_position = (m->m.x == -1 && m->m.y == -1);
	m->canvas_zoom = m->canvas_target_zoom = 1.0;
	m->canvas_duration = 250;

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);


	wl_list_insert(&mons, &m->link);

	m->pertag = calloc(1, sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;

	for (i = 0; i <= TAGCOUNT; i++) {
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->mfacts[i] = m->mfact;

		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		m->pertag->sellts[i] = m->sellt;
		m->pertag->canvas_x[i] = m->m.x + m->m.width / 2.0;
		m->pertag->canvas_y[i] = m->m.y + m->m.height / 2.0;
		m->pertag->canvas_zoom[i] = 1.0;
		m->pertag->focused[i] = NULL;
	}
	m->canvas_x = m->canvas_target_x = m->pertag->canvas_x[1];
	m->canvas_y = m->canvas_target_y = m->pertag->canvas_y[1];
	m->canvas_zoom = m->canvas_target_zoom = m->pertag->canvas_zoom[1];

	ext_workspace_createmon(m);
	ext_workspace_printstatus(m);

	

	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	if (scenefx_blur) {
		m->blur_layer = wlr_scene_optimized_blur_create(&scene->tree, 0, 0);
		wlr_scene_node_reparent(&m->blur_layer->node, layers[LyrBlur]);
		wlr_scene_node_set_enabled(&m->blur_layer->node, 1);
	}

	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	

	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

static void
monitor_stop_skip_frame_timer(Monitor *m)
{
	if (!m)
		return;
	if (m->skip_frame_timeout)
		wl_event_source_timer_update(m->skip_frame_timeout, 0);
	m->skiping_frame = false;
}

static int
monitor_skip_frame_timeout_callback(void *data)
{
	Monitor *m = data;
	if (!m)
		return 0;
	m->skiping_frame = false;
	if (m->wlr_output && m->wlr_output->enabled)
		wlr_output_schedule_frame(m->wlr_output);
	return 0;
}

static bool
monitor_should_skip_frame(Monitor *m)
{
	Client *c;
	bool pending = false;

	if (!m)
		return false;
	if (animations) {
		if (m->skiping_frame)
			monitor_stop_skip_frame_timer(m);
		return false;
	}

	/* When animations are disabled, do not render the old client buffer after
	 * a tiled resize/move has sent a new configure.  MangoWC suppresses the
	 * frame until the client has committed that configure; otherwise the old
	 * buffer can briefly appear scaled/transparent inside the new geometry. */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->scene || !client_surface(c)->mapped ||
			client_is_unmanaged(c) || !VISIBLEON(c, m))
			continue;
		/* Float-canvas geometry is applied directly to the scene on every
		 * interactive resize. Do not stall the whole output waiting for the
		 * client's configure ack: that turns live resize into visible lag when
		 * animations are disabled. Keep the old skip-frame behavior for tiled
		 * clients, where waiting avoids showing a stale buffer in new geometry. */
		if (c->isfloating && solux_canvas_active(m))
			continue;
		if (c->type != XDGShell)
			continue;
		if (c->resize && c->surface.xdg &&
			c->resize > c->surface.xdg->current.configure_serial) {
			pending = true;
			break;
		}
	}

	if (pending) {
		if (!m->skiping_frame) {
			m->skiping_frame = true;
			if (!m->skip_frame_timeout)
				m->skip_frame_timeout = wl_event_loop_add_timer(
					event_loop, monitor_skip_frame_timeout_callback, m);
			if (m->skip_frame_timeout)
				wl_event_source_timer_update(m->skip_frame_timeout, 100);
		}
		return true;
	}

	if (m->skiping_frame)
		monitor_stop_skip_frame_timer(m);
	return false;
}

void
rendermon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	SoluxFadeClient *fc, *fctmp;
	LayerSurface *l;
	struct timespec now;
	bool need_more_frames = false;

	wl_list_for_each_safe(fc, fctmp, &fadeout_clients, link) {
		if (fc->mon == m) {
			solux_animation_client_fadeout_frame(fc);
			need_more_frames = true;
		}
	}
	/* Do not overwrite an animation's per-frame opacity with the normal
	 * SceneFX focus opacity pass. */
	wl_list_for_each(c, &clients, link) {
		if (!c->scene || !c->scene_surface) continue;
		if (!c->animation.running)
			wlr_scene_node_for_each_buffer(&c->scene_surface->node, scenebuffersetopacity, c);
		if (c->animation.running && c->mon == m) {
			/* XWayland clients use the same scene animation path as XDG
			 * clients.  Do not cancel their first animation frame: doing so
			 * leaves the newly mapped X11 scene at its disabled/initial state
			 * and makes the opening animation appear to render nothing. */
			solux_animation_client_frame(c);
			need_more_frames = true;
		}
	}

	solux_canvas_frame(m);
	solux_taganim_frame(m);

	if (!monitor_should_skip_frame(m))
		wlr_scene_output_commit(m->scene_output, NULL);
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	if (need_more_frames)
		wlr_output_schedule_frame(m->wlr_output);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	

	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when dwl is locked */
	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);
		if (m->blur_layer) {
			wlr_scene_node_set_position(&m->blur_layer->node, m->m.x, m->m.y);
			wlr_scene_optimized_blur_set_size(m->blur_layer, m->m.width, m->m.height);
			if (scenefx_blur)
				wlr_scene_optimized_blur_mark_dirty(m->blur_layer);
		}

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		if (m->canvas_zoom <= 0.01) m->canvas_zoom = m->canvas_target_zoom = 1.0;
		if (m->canvas_x == 0.0 && m->canvas_y == 0.0) {
			m->canvas_x = m->canvas_target_x = m->m.x + m->m.width / 2.0;
			m->canvas_y = m->canvas_target_y = m->m.y + m->m.height / 2.0;
		}
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon, c->tags);
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}


	

	wlr_cursor_move(cursor, NULL, 0, 0);

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

#endif
