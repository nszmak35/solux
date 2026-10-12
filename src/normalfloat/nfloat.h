/*
 * Solux normal floating/canvas helpers.
 *
 * All Float-canvas specific fixes live here so the compositor's normal
 * layout/animation code does not have to know about the camera's coordinate
 * system.  The important rule is: Client::geom is always WORLD geometry;
 * scene nodes/buffers are the SCREEN representation of that geometry.
 */
#ifndef SOLUX_NORMALFLOAT_NFLOAT_H
#define SOLUX_NORMALFLOAT_NFLOAT_H

static Client *nfloat_spawn_pending;

/* Shadow node created by the last nfloat_snapshot() call (NULL if none). */
static struct wlr_scene_shadow *nfloat_snap_shadow;

static bool
nfloat_spawn_focus_blocked(Client *c)
{
	/* During the initial map sequence setmon()/focusclient() may briefly
	 * refocus the old client.  That must not move the canvas camera: the new
	 * Float is deliberately spawned at the camera position that was visible
	 * before mapping. */
	return nfloat_spawn_pending && c != nfloat_spawn_pending;
}

static void
nfloat_capture_spawn_canvas(Client *c)
{
	Monitor *m = selmon;

	if (!c)
		return;

	c->has_spawn_canvas = false;
	c->spawn_canvas_mon = NULL;

	if (!m || !solux_canvas_active(m))
		m = xytomon(cursor ? cursor->x : 0, cursor ? cursor->y : 0);

	if (!m || !solux_canvas_active(m))
		return;

	nfloat_spawn_pending = c;

	/* canvas_x/y are the camera position that is actually visible NOW.
	 * Do not use canvas_target_x/y: when animations are enabled the target can
	 * belong to the previously focused client while the user is looking at an
	 * intermediate camera position. */
	c->spawn_canvas_mon = m;
	c->spawn_canvas_x = m->canvas_x;
	c->spawn_canvas_y = m->canvas_y;
	c->has_spawn_canvas = true;
}

static void
nfloat_place_spawn_canvas(Client *c, Client *parent)
{
	Monitor *m;
	double x, y;

	if (!c || parent || !c->has_spawn_canvas ||
		!(m = c->spawn_canvas_mon) || c->mon != m ||
		!solux_canvas_active(m) || c->isfullscreen || client_is_unmanaged(c))
		return;

	x = c->spawn_canvas_x;
	y = c->spawn_canvas_y;
	if (x == 0.0 && y == 0.0) {
		x = m->m.x + m->m.width / 2.0;
		y = m->m.y + m->m.height / 2.0;
	}

	/* The client is created at the exact point currently visible through the
	 * camera, not at the geometry left by the previous client. */
	c->geom.x = (int)lround(x - c->geom.width / 2.0);
	c->geom.y = (int)lround(y - c->geom.height / 2.0);

	/* mapnotify initializes animation.current/target before this spawn point is
	 * known.  If there is no OPEN animation, keep the animation state at the
	 * actual spawn geometry; otherwise the first MOVE frame can begin at the
	 * old focused window's geometry.  OPEN itself replaces these values with
	 * its own initial geometry, so leave pending OPEN untouched. */
	if (!c->is_pending_open_animation)
		c->animation.initial = c->animation.current = c->animation.target = c->geom;
}

static void
nfloat_finish_spawn_canvas(Client *c)
{
	Monitor *m;
	double x, y, zoom;

	if (!c)
		return;
	if (!(m = c->mon) || !solux_canvas_active(m)) {
		if (nfloat_spawn_pending == c)
			nfloat_spawn_pending = NULL;
		return;
	}

	/* Re-apply the captured camera point at the last possible moment.  This
	 * prevents any focus/arrange callback between place_spawn and here from
	 * replacing the requested Float spawn geometry with the previous focused
	 * client's geometry. */
	if (c->has_spawn_canvas && c->spawn_canvas_mon == m && !c->isfullscreen) {
		c->geom.x = (int)lround(c->spawn_canvas_x - c->geom.width / 2.0);
		c->geom.y = (int)lround(c->spawn_canvas_y - c->geom.height / 2.0);
		if (c->animation.running && c->animation.action == SOLUX_ANIM_OPEN) {
			/* XWayland starts OPEN from mapnotify, before this runs, so
			 * is_pending_open_animation is already false.  Keep the running
			 * OPEN origin and only refresh its final target. */
			c->animation.target = c->geom;
		} else if (!c->is_pending_open_animation) {
			c->animation.initial = c->animation.current = c->animation.target = c->geom;
		}
	}

	x = c->geom.x + c->geom.width / 2.0;
	y = c->geom.y + c->geom.height / 2.0;
	zoom = m->canvas_target_zoom > 0.01 ? m->canvas_target_zoom : 1.0;

	/* Keep the camera exactly at the captured spawn point; the integer window
	 * center can differ from it by 0.5px. */
	if (c->has_spawn_canvas && c->spawn_canvas_mon == m && !c->isfullscreen &&
		(c->spawn_canvas_x != 0.0 || c->spawn_canvas_y != 0.0)) {
		x = c->spawn_canvas_x;
		y = c->spawn_canvas_y;
	}

	/* With animations disabled, set both the current and target camera at once.
	 * This is the part that prevents Rofi/GTK/foot/etc. from inheriting the
	 * previous focused client's camera position. */
	if (!animations) {
		m->canvas_x = m->canvas_target_x = x;
		m->canvas_y = m->canvas_target_y = y;
		m->canvas_zoom = m->canvas_target_zoom = zoom;
		m->canvas_animating = false;
		solux_canvas_apply(m);
		/* Do not synthesize a pointer motion here.  The physical cursor has not
		 * moved, and forcing a pointer-enter/motion update during spawn is what
		 * makes client-side cursors jump even when the canvas was not zoomed. */
		nfloat_spawn_pending = NULL;
		c->has_spawn_canvas = false;
		c->spawn_canvas_mon = NULL;
		return;
	}

	/* Animated mode follows the same visible camera point, without inventing a
	 * second spawn location. */
	if (fabs(m->canvas_x - x) < 0.001 && fabs(m->canvas_y - y) < 0.001 &&
		fabs(m->canvas_zoom - zoom) < 0.001) {
		/* Camera is already there: do not start an empty animation. */
		m->canvas_target_x = x;
		m->canvas_target_y = y;
		m->canvas_target_zoom = zoom;
		m->canvas_animating = false;
	} else {
		solux_canvas_start(m, x, y, zoom);
	}
	nfloat_spawn_pending = NULL;
	c->has_spawn_canvas = false;
	c->spawn_canvas_mon = NULL;
}

/*
 * Canvas buffer transform.
 *
 * Every buffer of the toplevel (root surface and real subsurfaces: foot
 * scrollbars, Firefox/GTK/Qt content areas, ...) is placed by ONE rule so the
 * whole tree always scales as a single rigid picture:
 *
 *   - The visible part of a surface is its rectangle intersected with the
 *     window clip (the xdg geometry, exactly what resize() hands to wlroots).
 *     Anything outside it (CSD shadows, subsurfaces that overhang while the
 *     client is still catching up with a resize) is cropped, never stretched.
 *   - The clip origin maps to the window's inner origin and one scale factor
 *     (screen px per surface px) is used on both axes of every buffer.
 *   - Buffer positions are computed as absolute positions inside the window's
 *     surface tree and corrected by the REAL positions of the parent nodes, so
 *     they do not depend on how wlroots happened to place the nodes (xdg
 *     geometry offset, subsurface offsets, clip) and cannot be applied twice.
 *
 * XDG popups are independent screen-space surfaces and are never touched.
 */
typedef struct {
	Client *client;
	struct wlr_box clip;   /* visible window area, root-surface coordinates */
	double zoom;
	double scale_x;        /* screen px per surface px */
	double scale_y;
} NFloatCanvasData;

/* Position of `surface` inside `root` (sum of the subsurface offsets). */
static bool
nfloat_surface_root_offset(struct wlr_surface *surface,
	struct wlr_surface *root, int *ox, int *oy)
{
	struct wlr_subsurface *sub;
	int x = 0, y = 0, depth = 0;

	if (!surface || !root || !ox || !oy)
		return false;
	while (surface != root) {
		sub = wlr_subsurface_try_from_wlr_surface(surface);
		if (!sub || !sub->parent || ++depth > 64)
			return false;
		x += sub->current.x;
		y += sub->current.y;
		surface = sub->parent;
	}
	*ox = x;
	*oy = y;
	return true;
}

/* Sum of the positions of all ancestors of `node` below `top` (top itself is
 * not included).  Fails if `top` is not an ancestor. */
static bool
nfloat_node_offset_below(struct wlr_scene_node *node,
	struct wlr_scene_node *top, int *x, int *y)
{
	struct wlr_scene_tree *p;
	int sx = 0, sy = 0, depth = 0;

	if (!node || !top || !x || !y)
		return false;
	p = node->parent;
	while (p && &p->node != top) {
		sx += p->node.x;
		sy += p->node.y;
		if (++depth > 256)
			return false;
		p = p->node.parent;
	}
	if (!p)
		return false;
	*x = sx;
	*y = sy;
	return true;
}

/* The clip the client's surface tree is cropped to.  `world` is the geometry
 * currently shown (animation frame or final geometry).  A running OPEN
 * animation reveals the window through a growing clip, like the normal path. */
static struct wlr_box
nfloat_canvas_clip(Client *c, const struct wlr_box *world)
{
	struct wlr_box clip;

	client_get_clip(c, &clip);
	if (world && c->animation.running &&
		c->animation.action == SOLUX_ANIM_OPEN) {
		clip.width = MAX(1, world->width - 2 * (int)c->bw);
		clip.height = MAX(1, world->height - 2 * (int)c->bw);
	}
	return clip;
}

static void
nfloat_canvas_buffer_apply(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	NFloatCanvasData *d = data;
	Client *c;
	struct wlr_scene_surface *ss;
	struct wlr_surface *surface;
	struct wlr_box R, I;
	struct wlr_fbox src, nsrc;
	int ox = 0, oy = 0, px = 0, py = 0;
	int x0, y0, x1, y1, dw, dh;
	double kx, ky;

	(void)sx;
	(void)sy;
	if (!buffer || !d || !(c = d->client) || !c->scene_surface ||
		!(ss = wlr_scene_surface_try_from_buffer(buffer)))
		return;

	surface = ss->surface;
	if (!surface || surface->current.width <= 0 || surface->current.height <= 0)
		return;

	/* Popups are not part of the toplevel's canvas geometry. */
	if (wlr_xdg_popup_try_from_wlr_surface(surface) != NULL)
		return;

	/* Subsurfaces of another root (for example popups) are not part of this toplevel. */
	if (wlr_surface_get_root_surface(surface) != client_surface(c))
		return;

	if (!nfloat_surface_root_offset(surface, client_surface(c), &ox, &oy) ||
		!nfloat_node_offset_below(&buffer->node, &c->scene_surface->node, &px, &py))
		return;

	R = (struct wlr_box){ ox, oy, surface->current.width, surface->current.height };
	if (wlr_box_intersection(&I, &R, &d->clip)) {
		/* Round the edges (not the sizes) so neighbouring buffers never gap or overlap. */
		x0 = (int)lround((double)(I.x - d->clip.x) * d->scale_x);
		y0 = (int)lround((double)(I.y - d->clip.y) * d->scale_y);
		x1 = (int)lround((double)(I.x + I.width - d->clip.x) * d->scale_x);
		y1 = (int)lround((double)(I.y + I.height - d->clip.y) * d->scale_y);
		dw = MAX(1, x1 - x0);
		dh = MAX(1, y1 - y0);

		/* Crop the source to the visible part.  Rotated buffers keep the
		 * source box wlroots gave them. */
		if (buffer->transform == WL_OUTPUT_TRANSFORM_NORMAL) {
			wlr_surface_get_buffer_source_box(surface, &src);
			if ((src.width <= 0 || src.height <= 0) && buffer->buffer)
				src = (struct wlr_fbox){ 0, 0,
					buffer->buffer->width, buffer->buffer->height };
			if (src.width > 0 && src.height > 0) {
				kx = src.width / (double)R.width;
				ky = src.height / (double)R.height;
				nsrc = (struct wlr_fbox){
					src.x + (I.x - R.x) * kx, src.y + (I.y - R.y) * ky,
					I.width * kx, I.height * ky };
				wlr_scene_buffer_set_source_box(buffer, &nsrc);
			}
		}
	} else {
		/* Completely outside the window area: nothing of it may show. */
		x0 = y0 = -1;
		dw = dh = 1;
	}

	wlr_scene_node_set_position(&buffer->node, x0 - px, y0 - py);
	wlr_scene_buffer_set_dest_size(buffer, dw, dh);

	update_buffer_corner_radius(c, buffer);
	if (scenefx_corner_radius > 0 && fabs(d->zoom - 1.0) > 1e-6) {
		/* The client's corners must follow the (zoomed) border rings. */
		int radius = scenefx_corner_radius;

		if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
			radius = 0;
		wlr_scene_buffer_set_corner_radius(buffer,
			MAX(0, (int)lround((double)radius * d->zoom)));
	}
}

/*
 * XDG popups (tooltips, menus) are children of c->scene, which sits at the
 * window's screen position, but wlroots keeps their offset in unscaled
 * surface units.  On the zoomed canvas the anchor point on the window moves
 * with the zoom while that offset does not, so tooltips drifted away from the
 * element they belong to.  The popup stays unscaled; only its offset from the
 * window is scaled.  wlroots rewrites the node position on popup commits, so
 * the last unscaled value is remembered per popup to tell its changes apart
 * from our own.  Slots are only compared by pointer, never dereferenced.
 */
#define NFLOAT_POPUP_SLOTS 64
typedef struct {
	struct wlr_scene_tree *tree;
	int ux, uy;   /* position as wlroots set it (unscaled) */
	int wx, wy;   /* position we wrote last */
	bool applied;
} NFloatPopupSlot;

static NFloatPopupSlot nfloat_popup_slots[NFLOAT_POPUP_SLOTS];
static unsigned int nfloat_popup_next;

static NFloatPopupSlot *
nfloat_popup_slot(struct wlr_scene_tree *tree)
{
	NFloatPopupSlot *slot;
	unsigned int i;

	for (i = 0; i < NFLOAT_POPUP_SLOTS; i++)
		if (nfloat_popup_slots[i].tree == tree)
			return &nfloat_popup_slots[i];

	slot = &nfloat_popup_slots[nfloat_popup_next++ % NFLOAT_POPUP_SLOTS];
	memset(slot, 0, sizeof(*slot));
	slot->tree = tree;
	return slot;
}

static void
nfloat_canvas_popups(Client *c, double scale_x, double scale_y, int bwz)
{
	struct wlr_xdg_popup *popup;
	struct wlr_scene_tree *tree;
	NFloatPopupSlot *slot;
	int nx, ny;

	if (!c || !c->scene || c->type != XDGShell || !c->surface.xdg)
		return;

	wl_list_for_each(popup, &c->surface.xdg->popups, link) {
		if (!popup->base || !popup->base->surface)
			continue;
		tree = popup->base->surface->data;
		/* Only popups already attached to this window's scene tree. */
		if (!tree || tree->node.parent != c->scene)
			continue;

		slot = nfloat_popup_slot(tree);
		if (!slot->applied || tree->node.x != slot->wx || tree->node.y != slot->wy) {
			slot->ux = tree->node.x;
			slot->uy = tree->node.y;
		}
		/* (bwz - bw) keeps the normal, unzoomed placement exactly as is. */
		nx = (int)lround(slot->ux * scale_x) + (bwz - (int)c->bw);
		ny = (int)lround(slot->uy * scale_y) + (bwz - (int)c->bw);
		wlr_scene_node_set_position(&tree->node, nx, ny);
		slot->wx = nx;
		slot->wy = ny;
		slot->applied = true;
	}
}

static void
nfloat_apply_canvas_client(Client *c, struct wlr_box visual, double zoom)
{
	NFloatCanvasData d;
	struct wlr_box world;
	int bwz;

	if (!c || !c->scene_surface || !client_surface(c) || !c->mon)
		return;

	zoom = zoom > 0.01 ? zoom : 1.0;
	world = c->animation.running ? c->animation.current : c->geom;
	if (world.width <= 0 || world.height <= 0)
		world = c->geom;
	bwz = (int)lround(c->bw * zoom);

	d.client = c;
	d.zoom = zoom;
	d.clip = nfloat_canvas_clip(c, &world);
	/* Scale what the client really shows right now (its committed geometry),
	 * not the geometry we merely asked for.  While the client is still catching
	 * up with a resize/animation its content then always fills the animated
	 * rectangle instead of leaving the desktop visible next to the border.
	 * Once the client is in sync both are identical.  The OPEN animation keeps
	 * its growing reveal clip. */
	if (!(c->animation.running && c->animation.action == SOLUX_ANIM_OPEN)) {
		struct wlr_box actual;

		client_get_geometry(c, &actual);
		if (actual.width > 0 && actual.height > 0) {
			d.clip.width = actual.width;
			d.clip.height = actual.height;
		}
	}
	d.scale_x = (double)MAX(1, visual.width - 2 * bwz) / (double)MAX(1, d.clip.width);
	d.scale_y = (double)MAX(1, visual.height - 2 * bwz) / (double)MAX(1, d.clip.height);
	/* Remembered so pointer coordinates use exactly what was applied. */
	c->canvas_scale_x = d.scale_x;
	c->canvas_scale_y = d.scale_y;

	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_node_for_each_buffer(&c->scene_surface->node,
		nfloat_canvas_buffer_apply, &d);
	nfloat_canvas_popups(c, d.scale_x, d.scale_y, bwz);
}

static void
nfloat_restore_canvas_client(Client *c)
{
	NFloatCanvasData d;

	if (!c || !c->scene_surface || !client_surface(c))
		return;

	d.client = c;
	d.zoom = 1.0;
	d.scale_x = 1.0;
	d.scale_y = 1.0;
	client_get_clip(c, &d.clip);
	c->canvas_scale_x = 1.0;
	c->canvas_scale_y = 1.0;

	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_node_for_each_buffer(&c->scene_surface->node,
		nfloat_canvas_buffer_apply, &d);
	nfloat_canvas_popups(c, 1.0, 1.0, (int)c->bw);
}

/* --- Close snapshot ----------------------------------------------------- */

static bool
nfloat_snapshot_node(Client *c, struct wlr_scene_node *node,
	int32_t lx, int32_t ly, struct wlr_scene_tree *parent,
	struct wlr_box initial)
{
	struct wlr_scene_node *out = NULL;

	if (!node || !parent)
		return true;
	if (!node->enabled && node->type != WLR_SCENE_NODE_TREE)
		return true;

	lx += node->x;
	ly += node->y;

	switch (node->type) {
	case WLR_SCENE_NODE_TREE: {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &tree->children, link)
			if (!nfloat_snapshot_node(c, child, lx, ly, parent, initial))
				return false;
		break;
	}
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *src = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(src);
		struct wlr_surface *surface = ss ? ss->surface : NULL;
		struct wlr_scene_buffer *dst;
		SoluxSnapBase *base = NULL;
		int w, h;

		if (!src || !surface)
			break;

		/* The snapshot holds the toplevel root and its own subsurfaces (Firefox/Qt draw
		 * into subsurfaces); popups and foreign surfaces stay out. */
		if (wlr_xdg_popup_try_from_wlr_surface(surface) != NULL)
			break;
		if (surface != client_surface(c) &&
			(wlr_subsurface_try_from_wlr_surface(surface) == NULL ||
			 wlr_surface_get_root_surface(surface) != client_surface(c)))
			break;

		if (surface == client_surface(c)) {
			w = src->dst_width;
			h = src->dst_height;
			w = MAX(1, initial.width - 2 * (int)c->bw);
			h = MAX(1, initial.height - 2 * (int)c->bw);
		} else {
			w = src->dst_width > 0 ? src->dst_width : surface->current.width;
			h = src->dst_height > 0 ? src->dst_height : surface->current.height;
		}
		if (w <= 0 || h <= 0)
			break;

		dst = wlr_scene_buffer_create(parent, NULL);
		if (!dst)
			return false;

		wlr_scene_buffer_set_dest_size(dst, w, h);
		wlr_scene_buffer_set_opaque_region(dst, &src->opaque_region);
		wlr_scene_buffer_set_source_box(dst, &src->src_box);
		wlr_scene_buffer_set_transform(dst, src->transform);
		wlr_scene_buffer_set_filter_mode(dst, src->filter_mode);
		wlr_scene_buffer_set_opacity(dst, src->opacity);
		wlr_scene_buffer_set_corner_radius(dst, scenefx_corner_radius);

		if (ss && ss->surface->buffer)
			wlr_scene_buffer_set_buffer(dst, &ss->surface->buffer->base);
		else if (src->buffer)
			wlr_scene_buffer_set_buffer(dst, src->buffer);

		if (surface != client_surface(c)) {
			base = ecalloc(1, sizeof(*base));
			base->x = lx;
			base->y = ly;
			base->w = w;
			base->h = h;
			dst->node.data = base;
		}

		out = &dst->node;
		break;
	}
	case WLR_SCENE_NODE_SHADOW: {
		struct wlr_scene_shadow *src = wlr_scene_shadow_from_node(node);
		struct wlr_scene_shadow *dst;
		if (!src)
			break;
		dst = wlr_scene_shadow_create(parent, src->width, src->height,
			src->corner_radius, src->blur_sigma, src->color);
		if (!dst)
			return false;
		wlr_scene_shadow_set_clipped_region(dst, src->clipped_region);
		nfloat_snap_shadow = dst;
		out = &dst->node;
		break;
	}
	case WLR_SCENE_NODE_RECT:
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:
		/* Decorations/blur are deliberately regenerated by the compositor; a
		 * close snapshot must contain client pixels only. */
		break;
	}

	if (out)
		wlr_scene_node_set_position(out, lx, ly);
	return true;
}

static struct wlr_scene_tree *
nfloat_snapshot(Client *c, struct wlr_scene_node *node,
	struct wlr_scene_tree *parent, struct wlr_box initial)
{
	struct wlr_scene_tree *snapshot;

	snapshot = wlr_scene_tree_create(parent);
	if (!snapshot)
		return NULL;
	wlr_scene_node_set_enabled(&snapshot->node, false);
	if (!nfloat_snapshot_node(c, node, -node->x, -node->y,
			snapshot, initial)) {
		wlr_scene_node_for_each_buffer(&snapshot->node, fadeout_buffer_free_base, NULL);
		wlr_scene_node_destroy(&snapshot->node);
		return NULL;
	}
	wlr_scene_node_set_enabled(&snapshot->node, true);
	return snapshot;
}

static void
nfloat_close_snapshot(Client *c)
{
	SoluxFadeClient *f;
	struct wlr_box initial;
	int nx = 0, ny = 0;
	struct wlr_box world;

	if (!c || client_is_unmanaged(c) || !c->scene || !c->mon ||
		!VISIBLEON(c, c->mon) || !animations || !animation_fade_out ||
		c->isfullscreen || c->isminimized ||
		solux_animation_is(animation_type_close, "none"))
		return;

	/* Freeze the close animation in SCREEN space.  Canvas movement after the
	 * unmap therefore cannot drag the closing image with the camera. */
	if (solux_canvas_active(c->mon)) {
		world = c->animation.running ? c->animation.current : c->geom;
		if (world.width <= 0 || world.height <= 0)
			world = c->geom;
		initial = solux_canvas_world_to_screen(c->mon, world);
		if (wlr_scene_node_coords(&c->scene->node, &nx, &ny)) {
			initial.x = nx;
			initial.y = ny;
		}
	} else {
		initial = c->animation.running ? c->animation.current : c->geom;
		if (initial.width <= 0 || initial.height <= 0)
			initial = c->geom;
	}

	c->iskilling = true;
	f = ecalloc(1, sizeof(*f));
	nfloat_snap_shadow = NULL;
	f->scene = nfloat_snapshot(c, &c->scene->node, layers[LyrFadeOut], initial);
	if (!f->scene) {
		nfloat_snap_shadow = NULL;
		free(f);
		return;
	}
	if (nfloat_snap_shadow) {
		int i;

		f->shadow = nfloat_snap_shadow;
		f->shadow_sigma = MAX(0, (int)lroundf(f->shadow->blur_sigma));
		f->shadow_w = MAX(0, (int)f->shadow->width - 2 * f->shadow_sigma);
		f->shadow_h = MAX(0, (int)f->shadow->height - 2 * f->shadow_sigma);
		f->shadow_radius = MAX(0, (int)f->shadow->corner_radius);
		for (i = 0; i < 4; i++)
			f->shadow_color[i] = f->shadow->color[i];
	}
	nfloat_snap_shadow = NULL;

	f->mon = c->mon;
	f->initial = initial;
	f->current = initial;
	f->duration = animation_duration_close;
	f->type = solux_animation_is(animation_type_close, "zoom") ? 2 :
		(solux_animation_is(animation_type_close, "slide") ? 1 : 0);

	if (f->type == 2) {
		f->current.width = MAX(1, (int)lround(f->initial.width * zoom_end_ratio));
		f->current.height = MAX(1, (int)lround(f->initial.height * zoom_end_ratio));
		f->current.x = f->initial.x + (f->initial.width - f->current.width) / 2;
		f->current.y = f->initial.y + (f->initial.height - f->current.height) / 2;
	} else if (f->type == 1) {
		int left = f->initial.x - c->mon->m.x;
		int right = c->mon->m.x + c->mon->m.width -
			(f->initial.x + f->initial.width);
		int top = f->initial.y - c->mon->m.y;
		int bottom = c->mon->m.y + c->mon->m.height -
			(f->initial.y + f->initial.height);
		int edge = MIN(MIN(abs(left), abs(right)), MIN(abs(top), abs(bottom)));

		if (edge == abs(left))
			f->current.x = c->mon->m.x - f->initial.width;
		else if (edge == abs(right))
			f->current.x = c->mon->m.x + c->mon->m.width;
		else if (edge == abs(top))
			f->current.y = c->mon->m.y - f->initial.height;
		else
			f->current.y = c->mon->m.y + c->mon->m.height;
	}

	f->time_started = solux_animation_now_ms();
	f->start_opacity = c->opacity;
	f->action = SOLUX_ANIM_CLOSE;
	wl_list_insert(&fadeout_clients, &f->link);
	wlr_scene_node_set_enabled(&f->scene->node, true);
	wlr_scene_node_set_position(&f->scene->node, f->initial.x, f->initial.y);
	solux_animation_schedule(f->mon);
}

#endif /* SOLUX_NORMALFLOAT_NFLOAT_H */
