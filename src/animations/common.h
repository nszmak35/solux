/* Solux animation core. Adapted from mangowc's animation implementation. */
#ifndef SOLUX_ANIMATION_COMMON_H
#define SOLUX_ANIMATION_COMMON_H

enum SoluxAnimationAction {
	SOLUX_ANIM_NONE = 0,
	SOLUX_ANIM_MOVE,
	SOLUX_ANIM_OPEN,
	SOLUX_ANIM_CLOSE,
	SOLUX_ANIM_FOCUS,
	SOLUX_ANIM_OPAFADEIN,
	SOLUX_ANIM_OPAFADEOUT,
	SOLUX_ANIM_TAG,
};

enum SoluxAnimationDirection {
	SOLUX_ANIM_UP,
	SOLUX_ANIM_DOWN,
	SOLUX_ANIM_LEFT,
	SOLUX_ANIM_RIGHT,
};

struct SoluxDVec2 { double x, y; };


static struct SoluxDVec2 solux_baked_move[256];
static struct SoluxDVec2 solux_baked_open[256];
static struct SoluxDVec2 solux_baked_close[256];
static struct SoluxDVec2 solux_baked_focus[256];
static struct SoluxDVec2 solux_baked_opafadein[256];
static struct SoluxDVec2 solux_baked_opafadeout[256];
static struct SoluxDVec2 solux_baked_tag[256];
static int solux_animation_curves_ready;

static double *solux_curve_for_type(int type)
{
	switch (type) {
	case SOLUX_ANIM_MOVE: return animation_curve_move;
	case SOLUX_ANIM_OPEN: return animation_curve_open;
	case SOLUX_ANIM_CLOSE: return animation_curve_close;
	case SOLUX_ANIM_FOCUS: return animation_curve_focus;
	case SOLUX_ANIM_OPAFADEIN: return animation_curve_opafadein;
	case SOLUX_ANIM_OPAFADEOUT: return animation_curve_opafadeout;
	case SOLUX_ANIM_TAG: return animation_curve_tag;
	default: return animation_curve_move;
	}
}

static struct SoluxDVec2 *solux_baked_for_type(int type)
{
	switch (type) {
	case SOLUX_ANIM_MOVE: return solux_baked_move;
	case SOLUX_ANIM_OPEN: return solux_baked_open;
	case SOLUX_ANIM_CLOSE: return solux_baked_close;
	case SOLUX_ANIM_FOCUS: return solux_baked_focus;
	case SOLUX_ANIM_OPAFADEIN: return solux_baked_opafadein;
	case SOLUX_ANIM_OPAFADEOUT: return solux_baked_opafadeout;
	case SOLUX_ANIM_TAG: return solux_baked_tag;
	default: return solux_baked_move;
	}
}

static struct SoluxDVec2 solux_animation_curve_at(double t, int type)
{
	double *p = solux_curve_for_type(type);
	struct SoluxDVec2 r;
	t = MAX(0.0, MIN(1.0, t));
	r.x = 3 * t * (1 - t) * (1 - t) * p[0] +
		  3 * t * t * (1 - t) * p[2] + t * t * t;
	r.y = 3 * t * (1 - t) * (1 - t) * p[1] +
		  3 * t * t * (1 - t) * p[3] + t * t * t;
	return r;
}

static void solux_animation_init_curves(void)
{
	int i;
	struct SoluxDVec2 *dsts[] = {
		solux_baked_move, solux_baked_open, solux_baked_close, solux_baked_focus,
		solux_baked_opafadein, solux_baked_opafadeout, solux_baked_tag
	};
	int types[] = {
		SOLUX_ANIM_MOVE, SOLUX_ANIM_OPEN, SOLUX_ANIM_CLOSE, SOLUX_ANIM_FOCUS,
		SOLUX_ANIM_OPAFADEIN, SOLUX_ANIM_OPAFADEOUT, SOLUX_ANIM_TAG
	};
	for (int k = 0; k < 7; k++)
		for (i = 0; i < 256; i++)
			dsts[k][i] = solux_animation_curve_at((double)i / 255.0, types[k]);
	solux_animation_curves_ready = 1;
}

static double solux_animation_factor(double t, int type)
{
	struct SoluxDVec2 *p;
	int lo, hi, mid;
	if (!solux_animation_curves_ready)
		solux_animation_init_curves();
	t = MAX(0.0, MIN(1.0, t));
	p = solux_baked_for_type(type);
	lo = 0;
	hi = 255;
	while (hi - lo > 1) {
		mid = (lo + hi) / 2;
		if (p[mid].x <= t)
			lo = mid;
		else
			hi = mid;
	}
	return p[hi].y;
}

static uint64_t solux_animation_now_ms(void)
{
	/* Keep the public animation state in milliseconds, but sample the clock with
	 * nanosecond precision before converting. This avoids visible quantisation
	 * when a frame lands around a millisecond boundary. */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}


static int solux_animation_is(const char *s, const char *name)
{
	return s && !strcasecmp(s, name);
}

static void solux_animation_schedule(Monitor *m)
{
	if (m && m->wlr_output && m->wlr_output->enabled)
		wlr_output_schedule_frame(m->wlr_output);
}

/* Snapshot the visual part of a scene tree so close animations can continue
 * after the original Wayland surface has been unmapped/destroyed. */
static bool solux_scene_snapshot_node(struct wlr_scene_node *node, int32_t lx,
		int32_t ly, struct wlr_scene_tree *parent)
{
	struct wlr_scene_node *out = NULL;
	if (!node->enabled && node->type != WLR_SCENE_NODE_TREE)
		return true;
	lx += node->x;
	ly += node->y;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE: {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &tree->children, link)
			solux_scene_snapshot_node(child, lx, ly, parent);
		break;
	}
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *src = wlr_scene_buffer_from_node(node);
		struct wlr_scene_buffer *dst = wlr_scene_buffer_create(parent, NULL);
		struct wlr_scene_surface *ss;
		if (!dst) return false;
		out = &dst->node;
		wlr_scene_buffer_set_dest_size(dst, src->dst_width, src->dst_height);
		wlr_scene_buffer_set_opaque_region(dst, &src->opaque_region);
		wlr_scene_buffer_set_source_box(dst, &src->src_box);
		wlr_scene_buffer_set_transform(dst, src->transform);
		wlr_scene_buffer_set_filter_mode(dst, src->filter_mode);
		wlr_scene_buffer_set_opacity(dst, src->opacity);
		wlr_scene_buffer_set_corner_radius(dst, scenefx_corner_radius);
		ss = wlr_scene_surface_try_from_buffer(src);
		if (ss && ss->surface->buffer)
			wlr_scene_buffer_set_buffer(dst, &ss->surface->buffer->base);
		else if (src->buffer)
			wlr_scene_buffer_set_buffer(dst, src->buffer);
		break;
	}
	case WLR_SCENE_NODE_SHADOW: {
		struct wlr_scene_shadow *src = wlr_scene_shadow_from_node(node);
		struct wlr_scene_shadow *dst = wlr_scene_shadow_create(parent,
				src->width, src->height, src->corner_radius, src->blur_sigma, src->color);
		if (!dst) return false;
		out = &dst->node;
		wlr_scene_shadow_set_clipped_region(dst, src->clipped_region);
		break;
	}
	case WLR_SCENE_NODE_RECT:
		/* Borders are recomputed by the live client and are intentionally not
		 * copied. This matches mangowc's snapshot path and avoids stale geometry. */
		break;
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:
		break;
	}
	if (out)
		wlr_scene_node_set_position(out, lx, ly);
	return true;
}

static struct wlr_scene_tree *solux_scene_snapshot(struct wlr_scene_node *node,
		struct wlr_scene_tree *parent)
{
	struct wlr_scene_tree *snapshot = wlr_scene_tree_create(parent);
	if (!snapshot) return NULL;
	wlr_scene_node_set_enabled(&snapshot->node, false);
	if (!solux_scene_snapshot_node(node, -node->x, -node->y, snapshot)) {
		wlr_scene_node_destroy(&snapshot->node);
		return NULL;
	}
	wlr_scene_node_set_enabled(&snapshot->node, true);
	return snapshot;
}

static void solux_animation_opacity_apply(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	(void)sx; (void)sy;
	wlr_scene_buffer_set_opacity(b, *(float *)data);
}

static void animation_buffer_opacity_apply(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	solux_animation_opacity_apply(b, sx, sy, data);
}

static void solux_animation_root_dest(BufferData *bd, struct wlr_surface *root, int32_t *w, int32_t *h)
{
	*w = root->current.width;
	*h = root->current.height;
	if (bd->width_scale >= 1.0f)
		*w = (int32_t)(bd->width_scale * *w);
	if (bd->height_scale >= 1.0f)
		*h = (int32_t)(bd->height_scale * *h);
	if (*w > bd->width)
		*w = bd->width;
	if (*h > bd->height)
		*h = bd->height;
}

static void solux_animation_child_apply(struct wlr_scene_buffer *b, struct wlr_surface *surface, BufferData *bd)
{
	struct wlr_subsurface *sub = wlr_subsurface_try_from_wlr_surface(surface);
	int32_t rw, rh;
	double kx, ky;

	if (!sub || bd->root->current.width <= 0 || bd->root->current.height <= 0 ||
		surface->current.width <= 0 || surface->current.height <= 0)
		return;
	solux_animation_root_dest(bd, bd->root, &rw, &rh);
	if (rw <= 0 || rh <= 0)
		return;
	kx = (double)rw / (double)bd->root->current.width;
	ky = (double)rh / (double)bd->root->current.height;
	wlr_scene_buffer_set_dest_size(b, MAX(1, (int32_t)lround(surface->current.width * kx)),
		MAX(1, (int32_t)lround(surface->current.height * ky)));
	if (b->node.parent)
		wlr_scene_node_set_position(&b->node.parent->node,
			(int)lround(sub->current.x * kx), (int)lround(sub->current.y * ky));
}

static void animation_buffer_scale_apply(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	BufferData *bd = data;
	struct wlr_scene_surface *scene_surface;
	struct wlr_surface *surface;
	int32_t surface_width, surface_height;
	(void)sx; (void)sy;

	if (!b || !bd || !(scene_surface = wlr_scene_surface_try_from_buffer(b)))
		return;

	surface = scene_surface->surface;

	/* XDG popups are separate transient surfaces.  Never resize them as part
	 * of the parent toplevel animation.  This check must happen BEFORE the
	 * scaling block: doing it afterwards still scales Chromium's hover
	 * previews/tooltips and makes them visibly grow/shrink with the window. */
	if (wlr_xdg_popup_try_from_wlr_surface(surface) != NULL)
		return;

	/* Firefox/Qt render into subsurfaces: scale them together with the root. */
	if (bd->follow_root && bd->root && bd->should_scale &&
		surface != bd->root && wlr_subsurface_try_from_wlr_surface(surface) != NULL &&
		wlr_surface_get_root_surface(surface) == bd->root) {
		solux_animation_child_apply(b, surface, bd);
		wlr_scene_buffer_set_corner_radii(b, bd->corner_location);
		return;
	}

	/* Match MangoWC's animation buffer path.  The animation must resize only
	 * the actual toplevel buffer.  Chromium creates XDG popups/subsurfaces for
	 * tab previews, tooltips, menus, etc.; resizing those buffers together with
	 * the parent produces the clipped/expanded mini-window seen during hover. */
	if (bd->should_scale) {
		surface_width = surface->current.width;
		surface_height = surface->current.height;

		if (bd->width_scale >= 1.0f)
			surface_width = (int32_t)(bd->width_scale * surface_width);
		if (bd->height_scale >= 1.0f)
			surface_height = (int32_t)(bd->height_scale * surface_height);

		if (surface_width > bd->width &&
			wlr_subsurface_try_from_wlr_surface(surface) == NULL)
			surface_width = bd->width;
		if (surface_height > bd->height &&
			wlr_subsurface_try_from_wlr_surface(surface) == NULL)
			surface_height = bd->height;

		/* Never resize a subsurface beyond its own geometry. */
		if (surface_width > bd->width &&
			wlr_subsurface_try_from_wlr_surface(surface) != NULL)
			return;
		if (surface_height > bd->height &&
			wlr_subsurface_try_from_wlr_surface(surface) != NULL)
			return;

		if (surface_width > 0 && surface_height > 0)
			wlr_scene_buffer_set_dest_size(b, surface_width, surface_height);
	}

	wlr_scene_buffer_set_corner_radii(b, bd->corner_location);
}

typedef struct {
	int x, y, w, h;
} SoluxSnapBase;

static void fadeout_buffer_size_apply(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	BufferData *bd = data;
	SoluxSnapBase *base;
	(void)sx; (void)sy;
	if (!b || !bd)
		return;
	if ((base = b->node.data)) {
		wlr_scene_buffer_set_dest_size(b, MAX(1, (int)lround(base->w * bd->width_scale)),
			MAX(1, (int)lround(base->h * bd->height_scale)));
		wlr_scene_node_set_position(&b->node, (int)lround(base->x * bd->width_scale),
			(int)lround(base->y * bd->height_scale));
		return;
	}
	wlr_scene_buffer_set_dest_size(b, bd->width, bd->height);
}

static void fadeout_buffer_free_base(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	(void)sx; (void)sy; (void)data;
	if (b && b->node.data) {
		free(b->node.data);
		b->node.data = NULL;
	}
}

static void solux_animation_set_buffers_opacity(struct wlr_scene_node *node, float opacity)
{	wlr_scene_node_for_each_buffer(node, solux_animation_opacity_apply, &opacity);
}

static void solux_animation_reset_buffer_visual(struct wlr_scene_buffer *b, int32_t sx, int32_t sy, void *data)
{
	Client *c = data;
	struct wlr_scene_surface *ss;
	struct wlr_surface *surface;
	(void)sx; (void)sy;

	if (!b || !c || !(ss = wlr_scene_surface_try_from_buffer(b)))
		return;
	surface = ss->surface;
	if (!surface)
		return;

	/* Restore exactly the unanimated SceneFX buffer state.  In particular,
	 * discard any destination size left by a previous MOVE/OPEN frame. */
	wlr_scene_buffer_set_dest_size(b, MAX(1, surface->current.width),
		MAX(1, surface->current.height));
	if (b->node.parent && surface != client_surface(c) &&
		wlr_surface_get_root_surface(surface) == client_surface(c) &&
		wlr_subsurface_try_from_wlr_surface(surface) != NULL)
		wlr_scene_node_set_position(&b->node.parent->node,
			wlr_subsurface_try_from_wlr_surface(surface)->current.x,
			wlr_subsurface_try_from_wlr_surface(surface)->current.y);
	wlr_scene_buffer_set_opacity(b, c->opacity);
	update_buffer_corner_radius(c, b);
}

static void solux_animation_reload_runtime(void)
{
	Client *c;
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		solux_taganim_finish(m);
	solux_animation_curves_ready = 0;
	solux_animation_init_curves();
	if (!animations) {
		wl_list_for_each(c, &clients, link) {
			if (!c->scene || !c->scene_surface || client_is_unmanaged(c)) continue;
			c->animation.running = false;
			c->animation.current = c->animation.target = c->geom;
			wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
			wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				solux_animation_reset_buffer_visual, c);
			update_client_rounded_borders(c);
		}
	}
	wl_list_for_each(m, &mons, link) {
		if (m->pertag)
			m->pertag->prevtag = m->pertag->curtag;
		if (m->wlr_output && m->wlr_output->enabled)
			wlr_output_schedule_frame(m->wlr_output);
	}
}

#endif
