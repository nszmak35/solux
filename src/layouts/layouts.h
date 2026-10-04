/* Tiling layouts: dwindle (tree and fibonacci), tile and monocle. */
#ifndef SOLUX_LAYOUTS_LAYOUTS_H
#define SOLUX_LAYOUTS_LAYOUTS_H

static void
solux_dwindle_tree_free(DwindleNode *n)
{
	if (!n)
		return;
	solux_dwindle_tree_free(n->children[0]);
	solux_dwindle_tree_free(n->children[1]);
	free(n);
}

static DwindleNode *
solux_dwindle_leaf(DwindleNode *n, Client *c)
{
	DwindleNode *r;
	if (!n)
		return NULL;
	if (!n->is_node)
		return n->client == c ? n : NULL;
	r = solux_dwindle_leaf(n->children[0], c);
	return r ? r : solux_dwindle_leaf(n->children[1], c);
}

static DwindleNode *
solux_dwindle_first_leaf(DwindleNode *n)
{
	if (!n)
		return NULL;
	while (n->is_node)
		n = n->children[0];
	return n;
}

static int
solux_dwindle_visible_tiled(Client *c, Monitor *m)
{
	return c && c->mon == m && VISIBLEON(c, m) && !c->isfloating &&
		!c->isfullscreen && !client_is_unmanaged(c);
}

static void
solux_dwindle_remove_leaf(DwindleNode **rootp, Client *c)
{
	DwindleNode *leaf, *parent, *sibling, *grandparent;

	if (!rootp || !*rootp || !c || !(leaf = solux_dwindle_leaf(*rootp, c)))
		return;

	parent = leaf->parent;
	if (!parent) {
		free(leaf);
		*rootp = NULL;
		return;
	}

	sibling = parent->children[parent->children[0] == leaf ? 1 : 0];
	grandparent = parent->parent;

	if (grandparent) {
		if (grandparent->children[0] == parent)
			grandparent->children[0] = sibling;
		else
			grandparent->children[1] = sibling;
	} else {
		*rootp = sibling;
	}
	sibling->parent = grandparent;

	free(leaf);
	free(parent);
}

/* Insert newc by bisecting the focused/anchor leaf. The split orientation is
 * deliberately NOT decided here. Swindle decides it from the resulting
 * container's box in recalc(), which is what gives Dwindle its spiral. */
static void
solux_dwindle_insert(DwindleNode **rootp, Client *newc, Client *focused)
{
	DwindleNode *new_leaf, *opening_on, *new_parent;

	if (!rootp || !newc)
		return;

	new_leaf = ecalloc(1, sizeof(*new_leaf));
	new_leaf->client = newc;
	new_leaf->is_node = 0;

	if (!*rootp) {
		*rootp = new_leaf;
		return;
	}

	opening_on = focused ? solux_dwindle_leaf(*rootp, focused) : NULL;
	if (!opening_on)
		opening_on = solux_dwindle_first_leaf(*rootp);
	if (!opening_on) {
		free(new_leaf);
		return;
	}

	new_parent = ecalloc(1, sizeof(*new_parent));
	new_parent->is_node = 1;
	new_parent->box = opening_on->box;
	new_parent->split_ratio = 1.0f;
	new_parent->parent = opening_on->parent;
	new_parent->children[0] = opening_on;
	new_parent->children[1] = new_leaf;

	opening_on->parent = new_parent;
	new_leaf->parent = new_parent;

	if (new_parent->parent) {
		if (new_parent->parent->children[0] == opening_on)
			new_parent->parent->children[0] = new_parent;
		else
			new_parent->parent->children[1] = new_parent;
	} else {
		*rootp = new_parent;
	}
}

/* With dwindle_mouse_client enabled, use the tiled client directly under the
 * pointer as the insertion anchor. This is the only mouse-dependent part;
 * the actual Dwindle geometry remains exactly the Swindle recursive model. */
static Client *
solux_dwindle_mouse_client(Monitor *m)
{
	Client *c = NULL;

	if (!m || !cursor)
		return NULL;

	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	return solux_dwindle_visible_tiled(c, m) ? c : NULL;
}

static void
solux_dwindle_recalc(DwindleNode *n, int gap)
{
	int w1, h1;
	if (!n)
		return;

	if (!n->is_node) {
		if (n->client && !n->client->isfullscreen)
			resize(n->client, n->box, 0);
		return;
	}

	/* This is the key Swindle behavior: every internal node chooses its
	 * orientation from the shape of its own current box. */
	n->split_top = (n->box.height > n->box.width);

	if (!n->split_top) {
		w1 = MAX(1, (int)(n->box.width / 2.0f * n->split_ratio) - gap / 2);
		n->children[0]->box = (struct wlr_box){
			n->box.x, n->box.y, w1, n->box.height
		};
		n->children[1]->box = (struct wlr_box){
			n->box.x + w1 + gap, n->box.y,
			MAX(1, n->box.width - w1 - gap), n->box.height
		};
	} else {
		h1 = MAX(1, (int)(n->box.height / 2.0f * n->split_ratio) - gap / 2);
		n->children[0]->box = (struct wlr_box){
			n->box.x, n->box.y, n->box.width, h1
		};
		n->children[1]->box = (struct wlr_box){
			n->box.x, n->box.y + h1 + gap,
			n->box.width, MAX(1, n->box.height - h1 - gap)
		};
	}

	solux_dwindle_recalc(n->children[0], gap);
	solux_dwindle_recalc(n->children[1], gap);
}

static void
solux_dwindle_tree_sync(Monitor *m)
{
	unsigned int tag;
	DwindleNode **rootp;
	Client *c, *focused, *mouse_target;

	if (!m || !m->pertag)
		return;

	tag = solux_pertag_index(m, m->pertag->curtag);
	rootp = &m->pertag->dwindle_roots[tag];

	/* Prune clients that disappeared from this tiled tag. */
	for (;;) {
		Client *gone = NULL;
		DwindleNode *stack[256];
		int sp = 0;

		if (*rootp)
			stack[sp++] = *rootp;

		while (sp && !gone) {
			DwindleNode *n = stack[--sp];
			if (!n->is_node) {
				/* Keep fullscreen clients in the Dwindle tree.  Fullscreen is
				 * a temporary presentation state, not a removal from the
				 * layout.  If we prune the leaf here, leaving fullscreen makes
				 * the client get inserted again at the end of the tree and it
				 * loses its previous position. */
				Client *tc = n->client;
				if (!tc || tc->mon != m || !VISIBLEON(tc, m) || tc->isfloating ||
				    client_is_unmanaged(tc) ||
				    !(tc->tags & m->tagset[m->seltags]))
					gone = tc;
			} else {
				if (n->children[1])
					stack[sp++] = n->children[1];
				if (n->children[0])
					stack[sp++] = n->children[0];
			}
		}

		if (!gone)
			break;
		solux_dwindle_remove_leaf(rootp, gone);
	}

	/* Exactly like Swindle: start from the focused client. If mouse mode is
	 * enabled, the client under the pointer is used as the first anchor.
	 * Every subsequently inserted client becomes the next anchor, so a burst
	 * of maps still forms one recursive Dwindle tree rather than repeated
	 * splits of the same window. */
	focused = m->pertag->focused[tag];
	if (!focused || !solux_dwindle_visible_tiled(focused, m) ||
	    !(focused->tags & m->tagset[m->seltags]) || !solux_dwindle_leaf(*rootp, focused))
		focused = NULL;

	mouse_target = dwindle_mouse_client ? solux_dwindle_mouse_client(m) : NULL;
	if (mouse_target && solux_dwindle_leaf(*rootp, mouse_target))
		focused = mouse_target;

	wl_list_for_each(c, &clients, link) {
		if (!solux_dwindle_visible_tiled(c, m) ||
		    !(c->tags & m->tagset[m->seltags]) || solux_dwindle_leaf(*rootp, c))
			continue;

		solux_dwindle_insert(rootp, c, focused);
		focused = c;
	}
}

static void
solux_dwindle_tree_arrange_node(DwindleNode *n, struct wlr_box box, int gap)
{
	if (!n)
		return;

	n->box = box;
	if (!n->is_node) {
		if (n->client && !n->client->isfullscreen)
			resize(n->client, box, 0);
		return;
	}

	solux_dwindle_recalc(n, gap);
}

static void
solux_dwindle_tree_arrange(Monitor *m)
{
	unsigned int tag;
	int e, n = 0;
	Client *c;
	struct wlr_box box;

	if (!m || !m->pertag)
		return;

	tag = solux_pertag_index(m, m->pertag->curtag);
	e = (int)gappx * m->gaps;

	wl_list_for_each(c, &clients, link)
		if (solux_dwindle_visible_tiled(c, m) &&
		    (c->tags & m->tagset[m->seltags]))
			n++;

	if ((unsigned int)smartgaps == (unsigned int)n)
		e = 0;

	box = (struct wlr_box){
		m->w.x + e,
		m->w.y + e,
		MAX(1, m->w.width - 2 * e),
		MAX(1, m->w.height - 2 * e),
	};

	if (m->pertag->dwindle_roots[tag]) {
		/* Dwindle has no separate master stack, but mfact still controls
		 * the first/root split.  The old tree always used 0.5 here, which
		 * made setmfact appear to do nothing while mouse-client Dwindle
		 * was active.  Nested splits remain the normal 50/50 Dwindle
		 * splits. */
		DwindleNode *root = m->pertag->dwindle_roots[tag];
		if (root->is_node)
			root->split_ratio = 2.0f * m->mfact;
		solux_dwindle_tree_arrange_node(root, box, e);
	}
}

void
fibonacci(Monitor *mon, int s)
{
	unsigned int i = 0, n = 0;
	int nx, ny, nw, nh;
	unsigned int e = mon->gaps;
	const int minsize = MAX(1 + 2 * (int)borderpx,
			2 * (int)borderspx + 2 * (int)borderspx_offset);
	Client *c, **order;
	Client *focused = NULL;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, mon) && !c->isfloating && !c->isfullscreen)
			n++;

	if (n == 0)
		return;

	if (smartgaps >= 0 && (unsigned int)smartgaps == n)
		e = 0;

	order = ecalloc(n, sizeof(*order));
	i = 0;
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, mon) && !c->isfloating && !c->isfullscreen)
			order[i++] = c;
	}

	const int split_min = 2 * minsize + (int)gappx * e;

	nx = mon->w.x + (int)gappx * e;
	ny = mon->w.y + (int)gappx * e;
	nw = MAX(0, mon->w.width - 2 * (int)gappx * e);
	nh = MAX(0, mon->w.height - 2 * (int)gappx * e);

	for (i = 0; i < n; i++) {
		c = order[i];

		if ((i % 2 && nh > split_min)
		   || (!(i % 2) && nw > split_min)
		   || i == n - 1) {
			if (i < n - 1) {
				if (i % 2)
					nh = (nh - (int)gappx*e) / 2;
				else
					nw = (nw - (int)gappx*e) / 2;

				if ((i % 4) == 2 && !s)
					nx += nw + (int)gappx*e;
				else if ((i % 4) == 3 && !s)
					ny += nh + (int)gappx*e;
			}
			if ((i % 4) == 0) {
				if (s)
					ny += nh + (int)gappx*e;
				else
					ny -= nh + (int)gappx*e;
			}
			else if ((i % 4) == 1)
				nx += nw + (int)gappx*e;
			else if ((i % 4) == 2)
				ny += nh + (int)gappx*e;
			else if ((i % 4) == 3) {
				if (s)
					nx += nw + (int)gappx*e;
				else
					nx -= nw + (int)gappx*e;
			}

			if (i == 0) {
				if (n != 1)
					nw = MAX(0, (int)((mon->w.width - 2 * (int)gappx*e - (int)gappx*e) * mon->mfact));
				ny = mon->w.y + (int)gappx*e;
			}
			else if (i == 1) {
				nw = MAX(0, mon->w.width - 2 * (int)gappx*e - nw - (int)gappx*e);
			}
		}

		resize(c, (struct wlr_box){
			.x = nx,
			.y = ny,
			.width = nw,
			.height = nh
		}, 0);
	}

	free(order);
}

static void
mirror_layout_horizontally(Monitor *mon)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		struct wlr_box box;

		if (!VISIBLEON(c, mon) || c->isfloating || c->isfullscreen)
			continue;

		box = c->geom;
		box.x = mon->w.x + mon->w.width -
			(box.x - mon->w.x) - box.width;
		resize(c, box, 0);
	}
}

static void
mirror_layout_vertically(Monitor *mon)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		struct wlr_box box;

		if (!VISIBLEON(c, mon) || c->isfloating || c->isfullscreen)
			continue;

		box = c->geom;
		box.y = mon->w.y + mon->w.height -
			(box.y - mon->w.y) - box.height;
		resize(c, box, 0);
	}
}

void
dwindle(Monitor *mon)
{
	int s = dwindle_type && !strcasecmp(dwindle_type, "spiral") ? 0 : 1;

	if (!dwindle_mouse_client)
		fibonacci(mon, s);
	else {
		solux_dwindle_tree_sync(mon);
		solux_dwindle_tree_arrange(mon);
	}
	if (dwindle_attach)
		mirror_layout_vertically(mon);
	if (dwindle_mirror)
		mirror_layout_horizontally(mon);
}

void
monocle(Monitor *m)
{
	Client *c;
	struct wlr_box box;
	int n = 0;
	unsigned int e = m->gaps;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;

	if (smartgaps >= 0 && (unsigned int)smartgaps == n)
		e = 0;

	box = (struct wlr_box){
		.x = m->w.x + gappx*e,
		.y = m->w.y + gappx*e,
		.width = (m->w.width > 2 * gappx*e) ? m->w.width - 2 * gappx*e : 1,
		.height = (m->w.height > 2 * gappx*e) ? m->w.height - 2 * gappx*e : 1
	};

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;

		resize(c, box, 0);
	}

	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}

void
tile(Monitor *m)
{
	unsigned int h, r, e = m->gaps, mw, my, ty;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;
	if (smartgaps >= 0 && (unsigned int)smartgaps == n)
		e = 0;

	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf((m->w.width + gappx*e) * m->mfact) : 0;
	else
		mw = m->w.width;
	i = 0;
	my = ty = gappx*e;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < m->nmaster) {
			r = MIN(n, m->nmaster) - i;
			h = (m->w.height - my - gappx*e - gappx*e * (r - 1)) / r;
			resize(c, (struct wlr_box){.x = m->w.x + gappx*e, .y = m->w.y + my,
				.width = mw - 2*gappx*e, .height = h}, 0);
			my += h + gappx*e;
		} else {
			r = n - i;
			h = (m->w.height - ty - gappx*e - gappx*e * (r - 1)) / r;
			resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
				.width = m->w.width - mw - gappx*e, .height = h}, 0);
			ty += h + gappx*e;
		}
		i++;
	}
}

#endif
