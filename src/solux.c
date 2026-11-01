/* See LICENSE file for copyright and license details. */
#include <limits.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <libdrm/drm_fourcc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <time.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include "ext_workspace/wlr_ext_workspace_v1.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <X11/Xlib.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

#ifdef XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION
#define XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION 4
#endif
enum { BrdOriginal, BrdStart, BrdEnd, BrdStartEnd };
enum { BorderNormal, BorderFocus, BorderUrgent };

#include "dwl-ipc-unstable-v2-protocol.h"
#include "util.h"
#include <stddef.h>
void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
int fd_set_nonblock(int fd);
extern const struct wl_interface ext_workspace_manager_v1_interface;
extern const struct wl_interface ext_workspace_group_handle_v1_interface;
extern const struct wl_interface ext_workspace_handle_v1_interface;

static const struct wl_message solux_ext_workspace_manager_v1_requests[] = {
	{ "commit", "", NULL },
	{ "stop", "", NULL },
};
static const struct wl_message solux_ext_workspace_manager_v1_events[] = {
	{ "workspace_group", "n", (const struct wl_interface *[]){ &ext_workspace_group_handle_v1_interface } },
	{ "workspace", "n", (const struct wl_interface *[]){ &ext_workspace_handle_v1_interface } },
	{ "done", "", NULL },
	{ "finished", "", NULL },
};
static const struct wl_message solux_ext_workspace_group_handle_v1_requests[] = {
	{ "create_workspace", "s", NULL },
	{ "destroy", "", NULL },
};
static const struct wl_message solux_ext_workspace_group_handle_v1_events[] = {
	{ "capabilities", "u", NULL },
	{ "output_enter", "o", (const struct wl_interface *[]){ &wl_output_interface } },
	{ "output_leave", "o", (const struct wl_interface *[]){ &wl_output_interface } },
	{ "workspace_enter", "o", (const struct wl_interface *[]){ &ext_workspace_handle_v1_interface } },
	{ "workspace_leave", "o", (const struct wl_interface *[]){ &ext_workspace_handle_v1_interface } },
	{ "removed", "", NULL },
};
static const struct wl_message solux_ext_workspace_handle_v1_requests[] = {
	{ "destroy", "", NULL },
	{ "activate", "", NULL },
	{ "deactivate", "", NULL },
	{ "assign", "o", (const struct wl_interface *[]){ &ext_workspace_group_handle_v1_interface } },
	{ "remove", "", NULL },
};
static const struct wl_message solux_ext_workspace_handle_v1_events[] = {
	{ "id", "s", NULL },
	{ "name", "s", NULL },
	{ "coordinates", "a", NULL },
	{ "state", "u", NULL },
	{ "capabilities", "u", NULL },
	{ "removed", "", NULL },
};

const struct wl_interface ext_workspace_manager_v1_interface = {
	"ext_workspace_manager_v1", 1,
	2, solux_ext_workspace_manager_v1_requests,
	4, solux_ext_workspace_manager_v1_events,
};
const struct wl_interface ext_workspace_group_handle_v1_interface = {
	"ext_workspace_group_handle_v1", 1,
	2, solux_ext_workspace_group_handle_v1_requests,
	6, solux_ext_workspace_group_handle_v1_events,
};
const struct wl_interface ext_workspace_handle_v1_interface = {
	"ext_workspace_handle_v1", 1,
	5, solux_ext_workspace_handle_v1_requests,
	6, solux_ext_workspace_handle_v1_events,
};


#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))

#define MIN_WINDOW_WIDTH        120
#define MIN_WINDOW_HEIGHT       80
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << TAGCOUNT) - 1u)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)
#define PREFIX(str, prefix)     !strncmp(str, prefix, strlen(prefix))

enum { CurNormal, CurPressed, CurMove, CurResize, CurCanvasMove };
enum { XDGShell, LayerShell, X11 };
enum { LyrBg, LyrBlur, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrFadeOut, LyrOverlay, LyrBlock, NUM_LAYERS };
enum { ClkClient, ClkRoot };
enum { SWIPE_LEFT, SWIPE_RIGHT, SWIPE_DOWN, SWIPE_UP, TZOOM_IN, TZOOM_OUT };

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mod;
	unsigned int button;
	int scroll;
	void (*func)(const Arg *);
	Arg arg;
} Button;

typedef struct {
	unsigned int mod;
	unsigned int motion;
	unsigned int fingers_count;
	void (*func)(const Arg *);
	Arg arg;
} Gesture;

typedef struct PointerDevice PointerDevice;
struct PointerDevice {
	struct wl_list link;
	struct wlr_pointer *pointer;
	struct wl_listener destroy;
};

#ifndef MAX_TAGS
#define MAX_TAGS 31
#endif

typedef struct Pertag Pertag;
typedef struct Monitor Monitor;
typedef struct SoluxFadeClient SoluxFadeClient;

struct SoluxFadeClient {
	struct wl_list link;
	struct wlr_scene_tree *scene;
	Monitor *mon;
	struct wlr_box initial, current;
	uint64_t time_started;
	uint32_t duration;
	int type;
	int action;
	float start_opacity;
	struct wlr_scene_shadow *shadow;
	int shadow_sigma, shadow_w, shadow_h, shadow_radius;
	float shadow_color[4];
};


typedef struct {
	float width_scale;
	float height_scale;
	int32_t width;
	int32_t height;
	struct fx_corner_radii corner_location;
	bool should_scale;
	struct wlr_surface *root;
	bool follow_root;
} BufferData;

enum { TAGANIM_NONE = 0, TAGANIM_IN, TAGANIM_OUT };

#define TAGANIM_CLIP_MAX 16
typedef struct {
	struct wlr_scene_buffer *buf;
	struct wlr_fbox orig_src, last_src;
	int orig_dw, orig_dh, last_dw, last_dh;
	int orig_x, orig_y, last_x, last_y;
	float orig_opacity, last_opacity;
} SoluxTagClipEntry;

typedef struct {
	bool running;
	int action;
	uint64_t time_started;
	uint32_t duration;
	struct wlr_box initial;
	struct wlr_box current;
	struct wlr_box target;
} SoluxAnimation;

typedef struct Client {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4];
	struct wlr_scene_rect *borders[4];
	struct wlr_scene_rect *bordere[4];
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom;
	struct wlr_box pending;
	struct wlr_box prev;
	struct wlr_box float_geom;
	bool float_geom_placed;
	struct wlr_box bounds;
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	struct wl_listener factivate;
	struct wl_listener fclose;
	struct wl_listener ffullscreen;
	struct wl_listener fdestroy;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener minimize;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
#endif
	unsigned int bw;
	unsigned int bws;
	unsigned int bwe;
	uint32_t tags;
	int isfloating, isurgent, isfullscreen;
	float opacity;
	float opacity_focus;
	float opacity_unfocus;
	int corner_radius;
	struct wlr_scene_shadow *shadow;
	float shadow_base_sigma;
	bool shadow_base_set;
	struct wlr_scene_blur *blur;
	int has_shadow_enabled;
	struct wlr_scene_rect *round_border;
	struct wlr_scene_rect *round_borders;
	struct wlr_scene_rect *round_bordere;
	uint32_t resize;
	unsigned int kblayout_idx;
	SoluxAnimation animation;
	bool is_pending_open_animation;
	bool iskilling;
	int isnoopenanimation;
	int isnoanimation;
	int isminimized;
	bool canvas_visualized;
	float canvas_buf_scale;
	bool inertia;
	double ivx, ivy;
	double irx, iry;
	uint64_t inertia_ms;
	double canvas_scale_x;
	double canvas_scale_y;
	Monitor *spawn_canvas_mon;
	double spawn_canvas_x, spawn_canvas_y;
	bool has_spawn_canvas;
	bool is_clip_to_hide;
	struct wlr_box canvas_saved_geom;
	int canvas_saved_floating;
	bool has_canvas_saved_geom;
	struct wlr_box canvas_tag_geom[MAX_TAGS + 1];
	uint32_t canvas_tag_geom_valid;
	int taganim_role;
	int taganim_from;
	int taganim_to;
	struct wlr_box taganim_frozen;
	double taganim_frozen_zoom;
	bool taganim_clipped;
	bool taganim_shadow_hidden;
	bool taganim_blur_hidden;
	bool taganim_crop_on;
	struct wlr_box taganim_crop;
	int taganim_clip_n;
	SoluxTagClipEntry taganim_clip[TAGANIM_CLIP_MAX];
} Client;

typedef struct DwindleNode DwindleNode;
struct DwindleNode {
	DwindleNode *children[2];
	DwindleNode *parent;
	Client *client;
	struct wlr_box box;
	float split_ratio;
	int split_top;
	int is_node;
};

typedef struct {
	struct wl_list link;
	struct wl_resource *resource;
	Monitor *mon;
} DwlIpcOutput;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms;
	uint32_t mods;
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
	bool noanim;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;


struct Monitor {
	struct wl_list link;
	struct wl_list dwl_ipc_outputs;
	struct wlr_ext_workspace_group_handle_v1 *ext_group;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg;
	struct wlr_scene_optimized_blur *blur_layer;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m;
	struct wlr_box w;
	struct wl_list layers[4];
	const Layout *lt[2];
	Pertag *pertag;
	int gaps;
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	int monrule_auto_position;
	char ltsymbol[16];
	int asleep;
	struct wl_event_source *skip_frame_timeout;
	bool skiping_frame;
	double canvas_x, canvas_y, canvas_zoom;
	double canvas_target_x, canvas_target_y, canvas_target_zoom;
	double canvas_start_x, canvas_start_y, canvas_start_zoom;
	uint64_t canvas_time_started;
	uint32_t canvas_duration;
	bool canvas_animating;
	double canvas_scale_mult;
	double canvas_scale_seen_zoom;
	uint64_t canvas_scale_settle_ms;
	bool taganim_active;
	bool taganim_vertical;
	uint64_t taganim_started;
	uint32_t taganim_duration;
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
	char *ltname;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	char *appid;
	int workspace;
	int isfloating;
	int isfullscreen;
	char *monitor;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

typedef struct {
	int x;
	int y;
} Vector;

static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void movefloating(const Arg *arg);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void assignkeymap(struct wlr_keyboard *keyboard);
static void axisnotify(struct wl_listener *listener, void *data);
static void autostartexec(void);
static void buttonpress(struct wl_listener *listener, void *data);
static int ongesture(struct wlr_pointer_swipe_end_event *event);
static void swipe_begin(struct wl_listener *listener, void *data);
static void swipe_update(struct wl_listener *listener, void *data);
static void swipe_end(struct wl_listener *listener, void *data);
static void pinch_begin(struct wl_listener *listener, void *data);
static void pinch_update(struct wl_listener *listener, void *data);
static void pinch_end(struct wl_listener *listener, void *data);
static void apply_pointer_config(struct wlr_pointer *pointer);
static void reapply_pointer_config(void);
static void destroy_pointer_config(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void cyclelayout(const Arg *arg);
static void cycletag(const Arg *arg);
static void reload_config(const Arg *arg);
static void apply_monitor_rule(Monitor *m);
static void apply_runtime_modkey(void);
static void reload_runtime_keymap(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static void setclientborderstate(Client *c, int state);
static Monitor *dirtomon(enum wlr_direction dir);
static void dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void dwl_ipc_manager_destroy(struct wl_resource *resource);
static void dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output);
static void dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource);
static void dwl_ipc_output_destroy(struct wl_resource *resource);
static void dwl_ipc_output_printstatus(Monitor *monitor);
static void dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output);
static void dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags);
static void dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index);
static void dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset);
static void dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource);
static void printstatus(void);
static void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void focusdir(const Arg *arg);
static void swapdir(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void incnmaster(const Arg *arg);
static void incxkbrules(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static void kblayout(KeyboardGroup *kb);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
static void scenebuffersetopacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void solux_apply_tag_layout(Client *c, Monitor *m, uint32_t tags);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void solux_setlayout_on(Monitor *m, const Layout *lt);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, uint32_t newtags);
static void setopacityunfocus(const Arg *arg);
static void setopacityfocus(const Arg *arg);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void setxkbrules(const Arg *arg);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void dwindle(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void togglegaps(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void view_on_monitor(Monitor *m, uint32_t tags, int toggle_tagset);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static void winview(const Arg *a);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);
static void zoomcanvas(const Arg *arg);
static void movecanvas(const Arg *arg);
static void canvasdrag(const Arg *arg);
static void solux_canvas_apply(Monitor *m);
static void solux_canvas_apply_client(Client *c);
static void solux_canvas_x11_position(Client *c, int w, int h, int *x, int *y);
static void solux_canvas_x11_sync(Client *c);
static void solux_canvas_x11_raise(Client *c);
static void solux_canvas_x11_rebase_child(Client *c, Client *parent);
static void solux_canvas_frame(Monitor *m);
static void solux_inertia_reset(void);
static void solux_canvas_focus_client(Client *c);
static bool solux_canvas_active(Monitor *m);
static struct wlr_box solux_canvas_world_to_screen(Monitor *m, struct wlr_box world);
static void solux_canvas_screen_to_world(Monitor *m, double sx, double sy, double *wx, double *wy);
static void solux_canvas_refresh_pointer(void);
static void solux_canvas_start(Monitor *m, double x, double y, double zoomlevel);
static unsigned int solux_pertag_index(Monitor *m, unsigned int tag);
static void solux_dwindle_tree_free(DwindleNode *n);
static DwindleNode *solux_dwindle_leaf(DwindleNode *n, Client *c);
static DwindleNode *solux_dwindle_first_leaf(DwindleNode *n);
static void solux_dwindle_remove_leaf(DwindleNode **rootp, Client *c);
static void solux_dwindle_insert(DwindleNode **rootp, Client *newc, Client *focused);
static Client *solux_dwindle_mouse_client(Monitor *m);
static void solux_dwindle_recalc(DwindleNode *n, int gap);
static void solux_dwindle_tree_sync(Monitor *m);
static void solux_dwindle_tree_arrange(Monitor *m);
static void solux_canvas_save_pertag(Monitor *m);
static void solux_canvas_load_pertag(Monitor *m, unsigned int tag, bool animate);
static void solux_canvas_save_tag_geometries(Monitor *m, unsigned int tag);
static void solux_canvas_record_client_geom(Client *c);
static void solux_canvas_load_tag_geometries(Monitor *m, unsigned int tag);
static void solux_pertag_save_focus(Monitor *m);
static Client *solux_pertag_focus(Monitor *m);
static void iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
static void iter_xdg_scene_buffers_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
static void iter_xdg_scene_buffers_corner_radius(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
static int in_shadow_ignore_list(const char *str);
static void client_set_shadow_blur_sigma(Client *c, float blur_sigma);
static void client_refresh_shadow(Client *c);
static void client_apply_shadow_geometry(Client *c, int width, int height, double zoom);
static void update_client_corner_radius(Client *c);
static void update_client_rounded_borders(Client *c);
static void update_client_shadow_color(Client *c);
static void update_client_focus_decorations(Client *c, int focused, int urgent);
static void update_client_blur(Client *c);
static void update_buffer_corner_radius(Client *c, struct wlr_scene_buffer *buffer);
static void update_scenefx_runtime(void);
static void createforeigntoplevel(Client *c);
static void factivatenotify(struct wl_listener *listener, void *data);
static void fclosenotify(struct wl_listener *listener, void *data);
static void fdestroynotify(struct wl_listener *listener, void *data);
static void ffullscreennotify(struct wl_listener *listener, void *data);

static void solux_animation_client_resize(Client *c);
static void solux_animation_client_frame(Client *c);
static void monitor_stop_skip_frame_timer(Monitor *m);
static int monitor_skip_frame_timeout_callback(void *data);
static bool monitor_should_skip_frame(Monitor *m);
static void solux_animation_client_close(Client *c);
static void solux_animation_client_fadeout_frame(SoluxFadeClient *f);
static void solux_animation_init_curves(void);
static void solux_animation_reload_runtime(void);
static void solux_animation_relayout(Monitor *m);
static void solux_taganim_freeze(Monitor *m);
static bool solux_taganim_should_run(Monitor *m, unsigned int oldtag);
static void solux_taganim_begin(Monitor *m, uint32_t oldtagset, unsigned int oldtag);
static void solux_taganim_frame(Monitor *m);
static void solux_taganim_finish(Monitor *m);
static void solux_taganim_unclip(Client *c);
static void solux_canvas_update_decorations(Client *c, struct wlr_box visual, double zoom);
static struct wlr_box solux_layer_target_geometry(LayerSurface *l);

static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
static bool solux_pertag_restoring;
static bool solux_focus_restoring;
static Client *pointer_hover_client;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;


static int soluxctl_fd = -1;
static struct wl_event_source *soluxctl_source;
static char soluxctl_socket_path[PATH_MAX];

static int soluxctl_handle_fd(int fd, uint32_t mask, void *data);
static void soluxctl_init(void);
static void soluxctl_cleanup(void);
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients;
static struct wl_list fstack;
static struct wl_list fadeout_clients;
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;
static struct wlr_pointer_gestures_v1 *pointer_gestures;
static struct wl_list pointer_devices;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int current_kblayout = 0;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy;
static Monitor *canvas_drag_mon;
static double canvas_drag_cursor_x, canvas_drag_cursor_y;
static double canvas_drag_start_x, canvas_drag_start_y;

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;
static const float scenefx_transparent[4] = {0.1f, 0.1f, 0.1f, 0.0f};

static uint32_t swipe_fingers;
static double swipe_dx;
static double swipe_dy;
static uint32_t pinch_fingers;
static double pinch_scale;
static double scroll_accum;

static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener cursor_swipe_begin = {.notify = swipe_begin};
static struct wl_listener cursor_swipe_update = {.notify = swipe_update};
static struct wl_listener cursor_swipe_end = {.notify = swipe_end};
static struct wl_listener cursor_pinch_begin = {.notify = pinch_begin};
static struct wl_listener cursor_pinch_update = {.notify = pinch_update};
static struct wl_listener cursor_pinch_end = {.notify = pinch_end};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};

static unsigned int kblayout_idx = -1;

static struct zdwl_ipc_manager_v2_interface dwl_manager_implementation = {.release = dwl_ipc_manager_release, .get_output = dwl_ipc_manager_get_output};
static struct zdwl_ipc_output_v2_interface dwl_output_implementation = {.release = dwl_ipc_output_release, .set_tags = dwl_ipc_output_set_tags, .set_layout = dwl_ipc_output_set_layout, .set_client_tags = dwl_ipc_output_set_client_tags};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void minimizenotify(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
static void xwaylandready(struct wl_listener *listener, void *data);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
#endif

typedef struct {
	char *command;
} DnxAutostartCommand;

static DnxAutostartCommand *autostart_cmds;
static size_t autostart_cmds_len, autostart_cmds_cap;
static pid_t *autostart_pids;
static size_t autostart_len;

#include "parser/parserconf.h"

#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
static int sloppyfocus               = 1;
static int bypass_surface_visibility = 0;
static unsigned int borderpx         = 0;
static float rootcolor[]             = COLOR(0x000000ff);
static float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f};
static const char kblayout_file[] = "/tmp/dwl-keymap";

static char *tags[MAX_TAGS + 1] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", NULL
};
static size_t tagcount = 9;

static int log_level = WLR_ERROR;

static int gappx = 10;
static int smartgaps = 0;
static int gaps = 1;
static float scenefx_opacity_inactive = 1.0f;
static float scenefx_opacity_active = 1.0f;
static int scenefx_shadow = 1;
static int scenefx_shadow_only_floating = 0;
static float scenefx_shadow_color[4] = COLOR(0x00000080);
static float scenefx_shadow_color_focus[4] = COLOR(0x000000b0);
static int scenefx_shadow_blur_sigma = 20;
static int scenefx_shadow_blur_sigma_focus = 30;
static int scenefx_corner_radius = 10;
static int scenefx_corner_radius_only_floating = 0;
static const char *const scenefx_shadow_ignore_list[] = { NULL };

static int scenefx_blur = 1;
static int scenefx_blur_xray = 0;
static int scenefx_blur_ignore_transparent = 1;
static int scenefx_blur_radius = 5;
static int scenefx_blur_num_passes = 3;
static float scenefx_blur_noise = 0.02f;
static float scenefx_blur_brightness = 0.90f;
static float scenefx_blur_contrast = 0.90f;
static float scenefx_blur_saturation = 1.10f;

static int animations = 1;
static int animation_fade_in = 1;
static int animation_fade_out = 1;
static float zoom_initial_ratio = 0.30f;
static float zoom_end_ratio = 0.80f;
static float fadein_begin_opacity = 0.50f;
static float fadeout_begin_opacity = 0.50f;
static uint32_t animation_duration_move = 500;
static uint32_t animation_duration_open = 400;
static uint32_t animation_duration_close = 300;
static uint32_t animation_duration_focus = 0;
static uint32_t animation_duration_tag = 300;
static int tag_animation_vertical = 0;
static double animation_curve_move[4] = {0.46, 1.0, 0.29, 0.99};
static double animation_curve_open[4] = {0.46, 1.0, 0.29, 0.99};
static double animation_curve_close[4] = {0.46, 1.0, 0.29, 0.99};
static double animation_curve_focus[4] = {0.46, 1.0, 0.29, 0.99};
static double animation_curve_opafadein[4] = {0.46, 1.0, 0.29, 0.99};
static double animation_curve_opafadeout[4] = {0.5, 0.5, 0.5, 0.5};
static double animation_curve_tag[4] = {0.46, 1.0, 0.29, 0.99};
static char *animation_type_open;
static char *animation_type_close;

static char *dwindle_type;
static int dwindle_attach = 0;
static int dwindle_mirror = 0;
static int dwindle_mouse_client = 0;

static float urgentcolor[] = COLOR(0xff0000ff);
static float bordercolor[] = COLOR(0xff0000ff);
static float focuscolor[] = COLOR(0xff0000ff);
static float borders_focuscolor[] = COLOR(0xff0000ff);
static float borders_urgentcolor[] = COLOR(0xff0000ff);
static float bordere_focuscolor[] = COLOR(0xff0000ff);
static float bordere_urgentcolor[] = COLOR(0xff0000ff);
static unsigned int borderspx        = 2;
static unsigned int borderepx        = 2;
static unsigned int borderspx_offset = 1;
static unsigned int borderepx_negative_offset = 0;
static float borderscolor[]          = COLOR(0xffffffff);
static float borderecolor[]          = COLOR(0x000000ff);
static int border_color_type         = BrdOriginal;
static int borders_only_floating     = 0;


#define KEYBOARD_LAYOUT "us,ru"
#define KEYBOARD_OPTIONS "grp:win_space_toggle"

static const Rule default_rules[] = {
	{ NULL, 0, 0, 0, NULL },
};

static const Layout default_layouts[] = {
	{ "[ dwindle ]", dwindle },
	{ "[ tile ]",    tile },
	{ "[ float ]",   NULL },
	{ "[ monocle ]", monocle },
};

static Layout layouts[LENGTH(default_layouts)];
static unsigned char layout_symbol_owned[LENGTH(default_layouts)];

static size_t layouts_len = LENGTH(layouts);

static const MonitorRule default_monrules[] = {
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
};

static struct xkb_rule_names xkb_rules = {
	.layout = "us,ru",
	.options = "grp:win_space_toggle",
};

static int repeat_rate = 25;
static int repeat_delay = 600;

static int tap_to_click = 1;
static int tap_and_drag = 1;
static int drag_lock = 1;
static int natural_scrolling = 0;
static int disable_while_typing = 1;
static int left_handed = 0;
static int middle_button_emulation = 0;
static unsigned int swipe_min_threshold = 0;

static enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;


static enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;


static uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

static enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static double accel_speed = 0.0;

static enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

static uint32_t runtime_modkey = WLR_MODIFIER_LOGO;


#define MODKEY   (1u << 31)
#define MODSUPER WLR_MODIFIER_LOGO
#define MODALT   WLR_MODIFIER_ALT
#define MODCTRL  WLR_MODIFIER_CTRL
#define MODNONE  0

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }

static Rule *rules;
static size_t rules_len, rules_cap;
static MonitorRule *monrules;
static size_t monrules_len, monrules_cap;
static Key *keys;
static size_t keys_len, keys_cap;
static Button *buttons;
static size_t buttons_len, buttons_cap;
static Gesture *gestures;
static size_t gestures_len, gestures_cap;


#define TAGCOUNT (tagcount)

struct Pertag {
	unsigned int curtag, prevtag;
	int nmasters[MAX_TAGS + 1];
	float mfacts[MAX_TAGS + 1];
	unsigned int sellts[MAX_TAGS + 1];
	const Layout *ltidxs[MAX_TAGS + 1][2];
	double canvas_x[MAX_TAGS + 1];
	double canvas_y[MAX_TAGS + 1];
	double canvas_zoom[MAX_TAGS + 1];
	Client *focused[MAX_TAGS + 1];
	DwindleNode *dwindle_roots[MAX_TAGS + 1];
};

#include "ext_workspace/ext_workspace.h"
#include "client.h"
#define ISTILED(c) ((c) && !(c)->isfloating && !(c)->isfullscreen)
static bool is_scroller_layout(Monitor *m) { (void)m; return false; }

#include "animations/common.h"
#include "normalfloat/nfloat.h"
#include "animations/client.h"
#include "animations/tag.h"
#include "effects/decorations.h"
#include "canvas/canvas.h"
#include "animations/inertia.h"
#include "layouts/layouts.h"
#include "config/config_util.h"
#include "config/config_scalar.h"
#include "config/config_load.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "gestures/gestures.h"
#include "output/monitor.h"
#include "shell/layer_shell.h"
#include "shell/session_lock.h"
#include "shell/foreign_toplevel.h"
#include "ipc/dwl_ipc.h"
#include "ipc/ipc_applet.h"
#include "xwayland/xwayland.h"

void
applybounds(Client *c, struct wlr_box *bbox)
{
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	const char *appid, *title;
	uint32_t newtags = 0;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	if (c->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_app_id(c->foreign_toplevel, appid);
		wlr_foreign_toplevel_handle_v1_set_title(c->foreign_toplevel, title);
	}

	for (r = rules; r < rules + rules_len; r++) {
		if (!r->appid || !*r->appid || strcmp(appid, r->appid))
			continue;

		c->isfloating = r->isfloating;
		c->isfullscreen = r->isfullscreen;

		if (r->workspace > 0 && r->workspace <= tagcount)
			newtags = 1u << (r->workspace - 1);

		if (r->monitor && *r->monitor && strcmp(r->monitor, "-1")) {
			wl_list_for_each(m, &mons, link) {
				if (m->wlr_output && !strcmp(m->wlr_output->name, r->monitor)) {
					mon = m;
					break;
				}
			}
		}
		break;
	}

	c->isfloating |= client_is_float_type(c);
	if (mon != selmon || (newtags && !(newtags & mon->tagset[mon->seltags])))
		c->isnoopenanimation = 1;
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;


	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m) ||
				(m->taganim_active && c->taganim_role == TAGANIM_OUT));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	c = focustop(m);
	{
		Client *fc;
		int monitor_fullscreen = 0;

		wl_list_for_each(fc, &clients, link) {
			if (fc->mon == m && fc->isfullscreen && VISIBLEON(fc, m)) {
				monitor_fullscreen = 1;
				break;
			}
		}

		wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			c && c->isfullscreen);
		if (m->blur_layer)
			wlr_scene_node_set_enabled(&m->blur_layer->node,
			scenefx_blur && !monitor_fullscreen);
	}

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !c->shadow || !c->scene || !client_surface(c) || !client_surface(c)->mapped ||
				client_is_unmanaged(c) || !VISIBLEON(c, m) || c->taganim_role == TAGANIM_OUT)
			continue;
		if (client_shadow_wanted(c) != c->has_shadow_enabled)
			update_client_shadow_color(c);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);

	motionnotify(0, NULL, 0, 0, 0, 0);
	solux_canvas_apply(m);
	checkidleinhibitor(NULL);
}

void
movefloating(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	Client *newsel = NULL;
	int dist, newdist = INT_MAX;
	int sel_cx, sel_cy, c_cx, c_cy;

	 
	if (!sel || sel->isfullscreen || sel->isfloating)
		return;

	sel_cx = sel->geom.x + sel->geom.width / 2;
	sel_cy = sel->geom.y + sel->geom.height / 2;

	 
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, selmon) || c->isfloating || c == sel)
			continue;

		c_cx = c->geom.x + c->geom.width / 2;
		c_cy = c->geom.y + c->geom.height / 2;

		 
		if (arg->ui == 0 && sel_cx <= c_cx) continue;  
		if (arg->ui == 1 && sel_cx >= c_cx) continue;  
		if (arg->ui == 2 && sel_cy <= c_cy) continue;  
		if (arg->ui == 3 && sel_cy >= c_cy) continue;  

		dist = abs(sel_cx - c_cx) + abs(sel_cy - c_cy);
		if (dist < newdist) {
			newdist = dist;
			newsel = c;
		}
	}

	 
	if (newsel != NULL) {
		wl_list_remove(&sel->link);

		

		if (arg->ui == 0 || arg->ui == 2) {
			 
			wl_list_insert(newsel->link.prev, &sel->link);
		} else {
			 
			wl_list_insert(&newsel->link, &sel->link);
		}

		 
		arrange(selmon);
		focusclient(sel, 1);
	}
}


void
autostartexec(void)
{
	size_t i;

	if (!autostart_cmds_len)
		return;

	autostart_len = autostart_cmds_len;
	autostart_pids = calloc(autostart_len, sizeof(*autostart_pids));
	if (!autostart_pids)
		die("calloc autostart_pids:");

	for (i = 0; i < autostart_cmds_len; i++) {
		if (!autostart_cmds[i].command || !*autostart_cmds[i].command)
			continue;

		if ((autostart_pids[i] = fork()) < 0)
			die("fork autostart:");
		if (autostart_pids[i] == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", autostart_cmds[i].command, (char *)NULL);
			dprintf(STDERR_FILENO, "solux: /bin/sh -c %s: %s\n",
					autostart_cmds[i].command, strerror(errno));
			_exit(127);
		}
	}
}


void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void
cleanup(void)
{
	size_t i;

	cleanuplisteners();
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
	xwayland = NULL;
#endif
	wl_display_destroy_clients(dpy);

	for (i = 0; i < autostart_len; i++) {
		if (0 < autostart_pids[i]) {
			kill(autostart_pids[i], SIGTERM);
			waitpid(autostart_pids[i], NULL, 0);
		}
	}

	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	if (rules) {
		for (i = 0; i < rules_len; i++) {
			free(rules[i].appid);
			free(rules[i].monitor);
		}
		free(rules);
		rules = NULL;
	}

	wlr_xcursor_manager_destroy(cursor_mgr);

	destroykeyboardgroup(&kb_group->destroy, NULL);

	wlr_backend_destroy(backend);

	soluxctl_cleanup();
	wl_display_destroy(dpy);
	while (!wl_list_empty(&fadeout_clients)) {
		SoluxFadeClient *f = wl_container_of(fadeout_clients.next, f, link);
		wl_list_remove(&f->link);
		wlr_scene_node_for_each_buffer(&f->scene->node, fadeout_buffer_free_base, NULL);
		free(f);
	}
	wlr_scene_node_destroy(&scene->tree.node);

}


void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&cursor_swipe_begin.link);
	wl_list_remove(&cursor_swipe_update.link);
	wl_list_remove(&cursor_swipe_end.link);
	wl_list_remove(&cursor_pinch_begin.link);
	wl_list_remove(&cursor_pinch_update.link);
	wl_list_remove(&cursor_pinch_end.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
	wl_list_remove(&new_xwayland_surface.link);
	wl_list_remove(&xwayland_ready.link);
#endif
}


void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		nfloat_capture_spawn_canvas(c);

		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL, 0);

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	if (client_surface(c)->mapped && c->scene && c->scene_surface) {
		Client *focused = c->mon ? focustop(c->mon) : NULL;

		c->corner_radius = scenefx_corner_radius;
		update_client_rounded_borders(c);

		if (scenefx_shadow && !c->shadow) {
			c->shadow = wlr_scene_shadow_create(c->scene, 0, 0,
				c->corner_radius, scenefx_shadow_blur_sigma,
				scenefx_shadow_color);
			if (c->shadow)
				wlr_scene_node_lower_to_bottom(&c->shadow->node);
		}
		if (c->shadow)
			update_client_shadow_color(c);

		if (scenefx_blur && !c->blur) {
			c->blur = wlr_scene_blur_create(c->scene, 0, 0);
			if (c->blur)
				wlr_scene_node_place_below(&c->blur->node,
					&c->scene_surface->node);
		}
		if (c->blur) {
			wlr_scene_node_set_enabled(&c->blur->node, scenefx_blur);
			update_client_blur(c);
		}

		c->opacity = (c == focused)
			? scenefx_opacity_active : scenefx_opacity_inactive;
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
			iter_xdg_scene_buffers, c);

		if (c->mon && c->mon->blur_layer && scenefx_blur) {
			wlr_scene_optimized_blur_set_size(c->mon->blur_layer,
				c->mon->m.width, c->mon->m.height);
			wlr_scene_optimized_blur_mark_dirty(c->mon->blur_layer);
		}
	}

	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	if (type != LayerShell) {
		solux_canvas_popup_box(c, &box);
		if (c->canvas_buf_scale > 0.0f && client_surface(c)->current.viewport.has_dst)
			client_set_scale(popup->base->surface, c->canvas_buf_scale);
	}
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	wl_list_remove(&listener->link);
	free(listener);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}


void
createnotify(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;
	c->bws = borders_only_floating ? 0 : borderspx;
	c->bwe = borders_only_floating ? 0 : borderepx;
	c->opacity_unfocus = scenefx_opacity_inactive;
	c->opacity_focus = scenefx_opacity_active;
	c->opacity = c->opacity_unfocus;
	c->corner_radius = scenefx_corner_radius;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}


void
createpopup(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}


void
cyclelayout(const Arg *arg)
{
	 
	const Layout *current = selmon->lt[selmon->sellt];
	int i, n = (int)layouts_len;
	int dir;
	int next;

	 
	for (i = 0; i < n; i++) {
		if (current == &layouts[i])
			break;
	}

	 
	dir = (arg && arg->i) ? arg->i : 1;
	next = (i + dir + n) % n;

	 
	setlayout(&(const Arg){ .v = &layouts[next] });
}

void
cycletag(const Arg *arg)
{
	unsigned int tagset, current;
	int dir, next;

	if (!selmon)
		return;

	tagset = selmon->tagset[selmon->seltags] & TAGMASK;
	if (!tagset)
		return;

	for (current = 0; current < TAGCOUNT; current++) {
		if (tagset & (1u << current))
			break;
	}

	if (current >= TAGCOUNT)
		return;

	dir = (arg && arg->i) ? arg->i : 1;
	next = (int)current + dir;

	if (next < 0)
		next = (int)TAGCOUNT - 1;
	else if (next >= (int)TAGCOUNT)
		next = 0;

	view(&(const Arg){ .ui = 1u << next });
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}


void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}


void
destroynotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->minimize.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	{
		Monitor *m;
		wl_list_for_each(m, &mons, link) {
			unsigned int i;
			if (!m->pertag)
				continue;
			for (i = 0; i <= TAGCOUNT; i++)
				if (m->pertag->focused[i] == c)
					m->pertag->focused[i] = NULL;
		}
	}
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}
	if (nfloat_spawn_pending == c)
		nfloat_spawn_pending = NULL;
	if (pointer_hover_client == c)
		pointer_hover_client = NULL;
	free(c);
}


void
focusclient(Client *c, int lift)
{
	struct keyboard_group_device {
		struct wlr_keyboard *keyboard;
		struct wl_listener key;
		struct wl_listener modifiers;
		struct wl_listener keymap;
		struct wl_listener repeat_info;
		struct wl_listener destroy;
		struct wl_list link;
	};

	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;
	struct keyboard_group_device *device;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	struct wlr_keyboard_group *group = keyboard ? wlr_keyboard_group_from_wlr_keyboard(keyboard) : NULL;
	FILE *f, *f_title;

	if (locked)
		return;

	f = fopen("/tmp/solux_active_window", "w");
	if (f) {
		if (c) {
			const char *appid = client_get_appid(c);
			fprintf(f, "%s\n", appid ? appid : "unknown");
		} else {
			fprintf(f, "\n");
		}
		fclose(f);
	}

	f_title = fopen("/tmp/solux_active_window_title", "w");
	if (f_title) {
		if (c) {
			const char *title = client_get_title(c);
			fprintf(f_title, "%s\n", title ? title : "");
		} else {
			fprintf(f_title, "\n");
		}
		fclose(f_title);
	}

	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old) {
		solux_canvas_focus_client(c);
		return;
	}

	old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l);

	if (old_client_type == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;
		c->opacity = c->opacity_focus;

		if (!exclusive_focus && !seat->drag) {
			setclientborderstate(c, BorderFocus);
			update_client_focus_decorations(c, 1, 0);
		}
	}

	if (old && (!c || client_surface(c) != old)) {
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			setclientborderstate(old_c, BorderNormal);
			update_client_focus_decorations(old_c, 0, 0);
			client_activate_surface(old, 0);
			if (old_c->foreign_toplevel)
				wlr_foreign_toplevel_handle_v1_set_activated(old_c->foreign_toplevel, 0);
			old_c->opacity = old_c->opacity_unfocus;
		}
	}

	if (group) {
		device = wl_container_of(group->devices.next, device, link);
		if (device->keyboard)
			wlr_keyboard_notify_modifiers(device->keyboard,
					device->keyboard->modifiers.depressed,
					device->keyboard->modifiers.latched,
					device->keyboard->modifiers.locked,
					kblayout_idx
		);
	}

	if (!c) {
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	motionnotify(0, NULL, 0, 0, 0, 0);

	if (selmon)
		dwl_ipc_output_printstatus(selmon);  

	if (c->type == XDGShell) {
		client_notify_enter(client_surface(c), keyboard);
	}
	#ifdef XWAYLAND
		else if (c->type == X11) {
			wlr_seat_keyboard_notify_enter(seat, c->surface.xwayland->surface, NULL, 0, NULL);
		}
	#endif

	if (c->mon && c->mon->pertag)
		c->mon->pertag->focused[solux_pertag_index(c->mon, c->mon->pertag->curtag)] = c;

	if (c->type == XDGShell) {
		client_activate_surface(client_surface(c), 1);
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_set_activated(c->foreign_toplevel, 1);
	}
	#ifdef XWAYLAND
		else if (c->type == X11) {
			wlr_xwayland_surface_activate(c->surface.xwayland, 1);
		}
	#endif

	solux_canvas_focus_client(c);
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;
			if (VISIBLEON(c, selmon))
				break;
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;
			if (VISIBLEON(c, selmon))
				break;
		}
	}
	focusclient(c, 1);
}


void focusdir(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	int dist = INT_MAX;
	Client *newsel = NULL;
	int newdist = INT_MAX;

	if (!sel || sel->isfullscreen)
		return;
   wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, selmon))
      continue;

    if (arg->ui == 0 && sel->geom.x <= c->geom.x) {
      continue;
    }
    if (arg->ui == 1 && sel->geom.x >= c->geom.x) {
      continue;
    }
    if (arg->ui == 2 && sel->geom.y <= c->geom.y) {
      continue;
    }
    if (arg->ui == 3 && sel->geom.y >= c->geom.y) {
      continue;
    }

    dist=abs(sel->geom.x-c->geom.x)+abs(sel->geom.y-c->geom.y);
    if (dist < newdist){
      newdist = dist;
      newsel=c;
    }
  }
  if (newsel != NULL){
    focusclient(newsel, 1);
  }
}

Vector
position_of_box(const struct wlr_box *box)
{
	return (Vector){
		.x = box->x + box->width / 2,
		.y = box->y + box->height / 2,
	};
}

Vector
diff_of_vectors(Vector *a, Vector *b)
{
	return (Vector){
		.x = b->x - a->x,
		.y = b->y - a->y,
	};
}

const char *
direction_of_vector(Vector *vector)
{
	if (vector->x == 0 && vector->y == 0) return "";

	if (abs(vector->y) > abs(vector->x)) {
		return (vector->y > 0) ? "bottom" : "top";
	} else {
		return (vector->x > 0) ? "right" : "left";
	}
}

uint32_t
vector_length(Vector *vector)
{
	return (uint32_t)sqrt(vector->x * vector->x + vector->y * vector->y);
}

Client *
client_in_direction(const char *direction, const int *skipfloat)
{
	Client *cfocused = focustop(selmon);
	Vector cfocusedposition;
	Client *ctarget = NULL;
	double targetdistance = INFINITY;
	Client *c;

	if (!cfocused || cfocused->isfullscreen || (skipfloat && cfocused->isfloating))
		return NULL;

	cfocusedposition = position_of_box(&cfocused->geom);

	wl_list_for_each(c, &clients, link) {
		Vector cposition;
		Vector positiondiff;
		uint32_t distance;

		if (c == cfocused)
			continue;

		if (skipfloat && c->isfloating)
			continue;

		if (!VISIBLEON(c, selmon))
			continue;

		cposition = position_of_box(&c->geom);
		positiondiff = diff_of_vectors(&cfocusedposition, &cposition);

		if (strcmp(direction, direction_of_vector(&positiondiff)) != 0)
			continue;

		distance = vector_length(&positiondiff);

		 if (distance < targetdistance) {
			ctarget = c;
			targetdistance = distance;
		}
	}

	return ctarget;
}


void
wl_list_swap(struct wl_list *list1, struct wl_list *list2)
{
	struct wl_list *prev1, *next1, *prev2, *next2;
	struct wl_list temp;

	if (list1 == list2) {
		return;
	}

	prev1 = list1->prev;
	next1 = list1->next;

	prev2 = list2->prev;
	next2 = list2->next;

	prev1->next = list2;
	next1->prev = list2;
	prev2->next = list1;
	next2->prev = list1;

	temp = *list1;
	*list1 = *list2;
	*list2 = temp;
}

void
swapdir(const Arg *arg)
{
	Client *c = NULL;
	Client *cfocused;

	if (!selmon || !arg)
		return;

	if (arg->ui == 0)
		c = client_in_direction("left", (int *)1);
	if (arg->ui == 1)
		c = client_in_direction("right", (int *)1);
	if (arg->ui == 2)
		c = client_in_direction("top", (int *)1);
	if (arg->ui == 3)
		c = client_in_direction("bottom", (int *)1);

	if (!c || !(cfocused = focustop(selmon)) || c == cfocused)
		return;

	if (selmon->lt[selmon->sellt]->arrange == dwindle && dwindle_mouse_client) {
		unsigned int tag = solux_pertag_index(selmon, selmon->pertag->curtag);
		DwindleNode *root = selmon->pertag->dwindle_roots[tag];
		DwindleNode *a = solux_dwindle_leaf(root, cfocused);
		DwindleNode *b = solux_dwindle_leaf(root, c);

		if (!a || !b)
			return;

		a->client = c;
		b->client = cfocused;
		focusclient(cfocused, 1);
		arrange(selmon);
		return;
	}

	wl_list_swap(&cfocused->link, &c->link);
	arrange(selmon);
}

Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = fx_renderer_create(backend)))
		die("couldn't recreate SceneFX renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
handlesig(int signo)
{
	if (signo == SIGCHLD) {
		pid_t pid, *p, *lim;
		while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
			if (pid == child_pid)
				child_pid = -1;
			if (!(p = autostart_pids))
				continue;
			lim = &p[autostart_len];

			for (; p < lim; p++) {
				if (*p == pid) {
					*p = -1;
					break;
				}
			}
		}
	} else if (signo == SIGINT || signo == SIGTERM) {
		quit(NULL);
	}
}

void
incnmaster(const Arg *arg)
{
	if (!arg || !selmon)
		return;
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}


void kblayout(KeyboardGroup *kb);
static void kblayout_idle(void *data);


void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}


void
mapnotify(struct wl_listener *listener, void *data)
{
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;

	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	


	if (c->type == XDGShell) {
		c->scene_surface = wlr_scene_xdg_surface_create(c->scene, c->surface.xdg);
	} 
	#ifdef XWAYLAND
		else if (c->type == X11) {
			c->scene_surface = wlr_scene_subsurface_tree_create(c->scene, c->surface.xwayland->surface);
		}
	#endif
	else {
		c->scene_surface = wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	}
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	if (client_is_unmanaged(c)) {
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	solux_canvas_x11_rebase_child(c, client_get_parent(c));

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
		c->border[i]->node.data = c;

		c->borders[i] = wlr_scene_rect_create(c->scene, 0, 0, borderscolor);
		c->borders[i]->node.data = c;

		c->bordere[i] = wlr_scene_rect_create(c->scene, 0, 0, borderecolor);
		c->bordere[i]->node.data = c;
	}
	setclientborderstate(c, c->isurgent ? BorderUrgent : BorderNormal);

	if (scenefx_corner_radius > 0) {
		c->round_border = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
		c->round_borders = wlr_scene_rect_create(c->scene, 0, 0, borderscolor);
		c->round_bordere = wlr_scene_rect_create(c->scene, 0, 0, borderecolor);
		if (!c->round_border || !c->round_borders || !c->round_bordere) {
			if (c->round_border) wlr_scene_node_destroy(&c->round_border->node);
			if (c->round_borders) wlr_scene_node_destroy(&c->round_borders->node);
			if (c->round_bordere) wlr_scene_node_destroy(&c->round_bordere->node);
			c->round_border = NULL;
			c->round_borders = NULL;
			c->round_bordere = NULL;
		} else {
			c->round_border->node.data = c;
			c->round_borders->node.data = c;
			c->round_bordere->node.data = c;
			wlr_scene_node_place_above(&c->round_border->node, &c->scene_surface->node);
			wlr_scene_node_place_above(&c->round_borders->node, &c->scene_surface->node);
			wlr_scene_node_place_above(&c->round_bordere->node, &c->scene_surface->node);
		}
	}
	if (scenefx_shadow) {
		c->shadow = wlr_scene_shadow_create(c->scene, 0, 0,
				c->corner_radius, scenefx_shadow_blur_sigma, scenefx_shadow_color);
		wlr_scene_node_lower_to_bottom(&c->shadow->node);
	}
	if (scenefx_blur) {
		c->blur = wlr_scene_blur_create(c->scene, 0, 0);
		wlr_scene_node_place_below(&c->blur->node, &c->scene_surface->node);
	}
	wlr_scene_node_for_each_buffer(&c->scene_surface->node, iter_xdg_scene_buffers, c);

	createforeigntoplevel(c);

	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;
	c->float_geom = c->geom;
	c->float_geom_placed = false;
	c->pending = c->animation.initial =
		c->animation.current = c->animation.target = c->geom;

	if (clients.prev)
		wl_list_insert(clients.prev, &c->link);
	else
		wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon, p->tags);
	} else {
		applyrules(c);
	}


	nfloat_place_spawn_canvas(c, p);

	if (borders_only_floating) {
		c->bws = c->isfloating ? borderspx : 0;
		c->bwe = c->isfloating ? borderepx : 0;
	}

	c->is_pending_open_animation = animations && !c->isnoopenanimation &&
		!c->isfullscreen && !solux_animation_is(animation_type_open, "none");

#ifdef XWAYLAND
	if (c->type == X11 && c->is_pending_open_animation)
		resize(c, c->geom, 0);
#endif

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
			setfullscreen(w, 0);
	}

	if (!client_is_unmanaged(c) && c->mon && !c->isfullscreen &&
			VISIBLEON(c, c->mon) && solux_canvas_active(c->mon))
		nfloat_finish_spawn_canvas(c);
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	

	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized) {
#ifdef XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION
		if (wl_resource_get_version(c->surface.xdg->toplevel->resource)
				< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
#endif
			wlr_xdg_surface_schedule_configure(c->surface.xdg);
	}
}


void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}


void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}


void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	if (interact) {
		geo.width = MAX(MIN_WINDOW_WIDTH, geo.width);
		geo.height = MAX(MIN_WINDOW_HEIGHT, geo.height);
	}
	geo.width = MAX(1 + 2 * (int)c->bw, geo.width);
	geo.height = MAX(1 + 2 * (int)c->bw, geo.height);

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	if (!solux_canvas_active(c->mon) && VISIBLEON(c, c->mon))
		applybounds(c, bbox);

	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	wlr_scene_rect_set_size(c->borders[0], MAX(0, c->geom.width - 2 * (int)borderspx_offset), c->bws);
	wlr_scene_rect_set_size(c->borders[1], MAX(0, c->geom.width - 2 * (int)borderspx_offset), c->bws);
	wlr_scene_rect_set_size(c->borders[2], c->bws, MAX(0, c->geom.height - 2 * (int)c->bws - 2 * (int)borderspx_offset));
	wlr_scene_rect_set_size(c->borders[3], c->bws, MAX(0, c->geom.height - 2 * (int)c->bws - 2 * (int)borderspx_offset));
	wlr_scene_node_set_position(&c->borders[0]->node, borderspx_offset, borderspx_offset);
	wlr_scene_node_set_position(&c->borders[1]->node, borderspx_offset, c->geom.height - c->bws - borderspx_offset);
	wlr_scene_node_set_position(&c->borders[2]->node, borderspx_offset, c->bws + borderspx_offset);
	wlr_scene_node_set_position(&c->borders[3]->node, c->geom.width - c->bws - borderspx_offset, c->bws + borderspx_offset);

	wlr_scene_rect_set_size(c->bordere[0], MAX(0, c->geom.width - ((int)c->bw - (int)c->bwe) * 2 + (int)borderepx_negative_offset * 2), c->bwe);
	wlr_scene_rect_set_size(c->bordere[1], MAX(0, c->geom.width - ((int)c->bw - (int)c->bwe) * 2 + (int)borderepx_negative_offset * 2), c->bwe);
	wlr_scene_rect_set_size(c->bordere[2], c->bwe, MAX(0, c->geom.height - 2 * (int)c->bw + 2 * (int)borderepx_negative_offset));
	wlr_scene_rect_set_size(c->bordere[3], c->bwe, MAX(0, c->geom.height - 2 * (int)c->bw + 2 * (int)borderepx_negative_offset));
	wlr_scene_node_set_position(&c->bordere[0]->node, c->bw - c->bwe - borderepx_negative_offset, c->bw - c->bwe - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[1]->node, c->bw - c->bwe - borderepx_negative_offset, c->geom.height - c->bw + borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[2]->node, c->bw - c->bwe - borderepx_negative_offset, c->bw - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[3]->node, c->geom.width - c->bw + borderepx_negative_offset, c->bw - borderepx_negative_offset);

	if (!c->iskilling)
		c->pending = c->geom;

	if (c->isfloating && !c->isfullscreen && !c->iskilling) {
		c->float_geom = c->geom;
		c->float_geom_placed = true;
	}

	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	#ifdef XWAYLAND
		if (c->type == X11) {
			int x11x, x11y;

			solux_canvas_x11_position(c, c->geom.width - 2 * c->bw,
					c->geom.height - 2 * c->bw, &x11x, &x11y);
			wlr_xwayland_surface_configure(c->surface.xwayland, x11x, x11y, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
		}
	#endif
		client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
	update_client_rounded_borders(c);
	if (scenefx_shadow && c->shadow)
		client_refresh_shadow(c);

	if (scenefx_blur && c->blur) {
		int radius = c->corner_radius;
		if ((scenefx_corner_radius_only_floating && !c->isfloating) || c->isfullscreen)
			radius = 0;
		wlr_scene_blur_set_size(c->blur, c->geom.width, c->geom.height);
		wlr_scene_blur_set_corner_radius(c->blur, radius);
		wlr_scene_blur_set_should_only_blur_bottom_layer(c->blur, !scenefx_blur_xray);
		wlr_scene_blur_set_strength(c->blur, 1.0f);
		wlr_scene_blur_set_alpha(c->blur, 1.0f);
	}

	solux_animation_client_resize(c);
	if (c->mon && solux_canvas_active(c->mon) && c->isfloating && !c->isfullscreen) {
		solux_canvas_record_client_geom(c);
		solux_canvas_apply_client(c);
	}

}

static void
set_activation_env(void)
{
	if (!getenv("DBUS_SESSION_BUS_ADDRESS")) {
		fprintf(stderr, "solux: not updating dbus environment: DBUS_SESSION_BUS_ADDRESS not set\n");
		return;
	}
	spawn(&(Arg){.v = "dbus-update-activation-environment DISPLAY WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_TYPE"});
	spawn(&(Arg){.v = "systemctl --user import-environment DISPLAY WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_TYPE"});
}

void
run(char *startup_cmd)
{
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	set_activation_env();
	autostartexec();
	if (startup_cmd) {
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			close(STDIN_FILENO);
			setsid();
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			dprintf(STDERR_FILENO, "solux: startup command: %s\n", strerror(errno));
			_exit(127);
		}
	}

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	selmon = xytomon(cursor->x, cursor->y);

	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	

	wl_display_run(dpy);
}


static void
solux_apply_tag_layout(Client *c, Monitor *m, uint32_t tags)
{
	const Layout *targetlt = NULL;
	unsigned int tagidx, sellt;

	if (!c || !m || !m->pertag || !tags || (tags & (tags - 1)) != 0)
		return;

	tagidx = (unsigned int)__builtin_ctz(tags) + 1;
	if (tagidx > TAGCOUNT)
		return;
	sellt = m->pertag->sellts[tagidx];
	targetlt = m->pertag->ltidxs[tagidx][sellt];
	if (!targetlt || !targetlt->arrange || !c->isfloating || c->isfullscreen)
		return;

	c->isfloating = 0;
	if (borders_only_floating) {
		c->bws = 0;
		c->bwe = 0;
	}
	wlr_scene_node_reparent(&c->scene->node, layers[LyrTile]);
	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	int tiled_layout;
	bool canvas_was_active;
	int was_floating;
	struct wlr_box oldgeom, target;

	if (!c)
		return;

	tiled_layout = c->mon && c->mon->lt[c->mon->sellt] &&
		c->mon->lt[c->mon->sellt]->arrange;
	canvas_was_active = c->mon && solux_canvas_active(c->mon);
	oldgeom = c->geom;
	was_floating = c->isfloating;
	c->isfloating = floating;
	if (canvas_was_active && !c->isfloating && !c->isfullscreen && c->canvas_visualized)
		solux_canvas_restore_client(c);
	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);

	if (!c->mon || !client_surface(c)->mapped)
		return;

	if (borders_only_floating) {
		c->bws = c->isfloating ? borderspx : 0;
		c->bwe = c->isfloating ? borderepx : 0;
	}

	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating && tiled_layout ? LyrFloat : LyrTile]);

	if (tiled_layout && c->isfloating && !c->isfullscreen) {
		bool recenter = true;

		if (was_floating && c == grabc) {
			arrange(c->mon);
			return;
		}

		target = oldgeom;
		if (c->float_geom.width > 0 && c->float_geom.height > 0) {
			target = c->float_geom;
			if (target.width >= c->mon->w.width - (int)gappx) {
				target.width = c->mon->w.width * 9 / 10;
				c->float_geom_placed = false;
			}
			if (target.height >= c->mon->w.height - (int)gappx) {
				target.height = c->mon->w.height * 9 / 10;
				c->float_geom_placed = false;
			}
			if (c->float_geom_placed &&
					target.x + target.width / 2 >= c->mon->w.x &&
					target.x + target.width / 2 < c->mon->w.x + c->mon->w.width &&
					target.y + target.height / 2 >= c->mon->w.y &&
					target.y + target.height / 2 < c->mon->w.y + c->mon->w.height)
				recenter = false;
		}
		if (recenter) {
			target.x = c->mon->w.x + (c->mon->w.width - target.width) / 2;
			target.y = c->mon->w.y + (c->mon->w.height - target.height) / 2;
		}
		if (animations && !c->isnoanimation && !c->animation.running)
			c->animation.current = oldgeom;
		resize(c, target, 0);
		arrange(c->mon);
		return;
	}

	if (tiled_layout && !c->isfloating && !c->isfullscreen) {
		if (animations && !c->isnoanimation && !c->animation.running)
			c->animation.current = oldgeom;
		arrange(c->mon);
		return;
	}

	arrange(c->mon);
}
void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : borderpx;
	c->bws = fullscreen ? 0 : borderspx;
	c->bwe = fullscreen ? 0 : borderepx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		resize(c, c->prev, 0);
	}
	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, scenebuffersetopacity, c);
	arrange(c->mon);
}

static void
solux_animation_relayout(Monitor *m)
{
	Client *c;

	if (!m)
		return;

	if (!animations) {
		arrange(m);
		return;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !client_surface(c)->mapped || !c->scene ||
				client_is_unmanaged(c) || c->isfloating || c->isfullscreen)
			continue;
		if (!c->animation.running)
			c->animation.current = c->geom;
	}

	arrange(m);

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !client_surface(c)->mapped || !c->scene ||
				client_is_unmanaged(c) || c->isfloating || c->isfullscreen)
			continue;
		if (!c->animation.running &&
				!wlr_box_equal(&c->animation.current, &c->geom))
			solux_client_start_move(c, c->geom);
	}
}

static void
solux_setlayout_on(Monitor *m, const Layout *lt)
{
	const Layout *oldlt;
	Client *c;

	if (!m || !m->pertag)
		return;

	oldlt = m->lt[m->sellt];
	if (!lt || lt != oldlt)
		m->sellt = m->pertag->sellts[m->pertag->curtag] ^= 1;
	if (lt)
		m->lt[m->sellt] = m->pertag->ltidxs[m->pertag->curtag][m->sellt] = (Layout *)lt;

	

	if (!oldlt->arrange && m->lt[m->sellt]->arrange) {
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m || !VISIBLEON(c, m) || c->isfullscreen ||
					client_is_unmanaged(c))
				continue;
			c->canvas_saved_geom = c->geom;
			c->canvas_saved_floating = c->isfloating;
			c->has_canvas_saved_geom = true;
			if (m->pertag->curtag <= TAGCOUNT) {
				c->canvas_tag_geom[m->pertag->curtag] = c->geom;
				c->canvas_tag_geom_valid |= (uint32_t)1u << m->pertag->curtag;
			}

			if (c->canvas_visualized)
				solux_canvas_restore_client(c);
			c->isfloating = 0;
			if (borders_only_floating) {
				c->bws = 0;
				c->bwe = 0;
			}
			wlr_scene_node_reparent(&c->scene->node,
				layers[c->isfullscreen ? LyrFS : LyrTile]);
			update_client_corner_radius(c);
			update_client_shadow_color(c);
			update_client_blur(c);
			if (animations && !c->isnoanimation)
				c->animation.current = c->geom;
		}
	} else if (oldlt->arrange && !m->lt[m->sellt]->arrange) {
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m || !VISIBLEON(c, m) || c->isfullscreen ||
					client_is_unmanaged(c) || !c->has_canvas_saved_geom)
				continue;
			c->isfloating = c->canvas_saved_floating;
			if (animations && !c->isnoanimation && !c->animation.running)
				c->animation.current = c->geom;
			resize(c, c->canvas_saved_geom, 0);
			if (m->pertag->curtag <= TAGCOUNT) {
				c->canvas_tag_geom[m->pertag->curtag] = c->canvas_saved_geom;
				c->canvas_tag_geom_valid |= (uint32_t)1u << m->pertag->curtag;
			}
			c->has_canvas_saved_geom = false;
		}
	}

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));
	solux_animation_relayout(m);

	if (!m->lt[m->sellt]->arrange)
		solux_canvas_focus_client(focustop(m));

	printstatus();
	dwl_ipc_output_printstatus(m);
	ext_workspace_printstatus(m);
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	solux_setlayout_on(selmon, arg ? (const Layout *)arg->v : NULL);
}

void
setmfact(const Arg *arg)
{
	float f;

	

	if (!arg || !selmon || !selmon->lt[selmon->sellt] ||
			!selmon->lt[selmon->sellt]->arrange ||
			selmon->lt[selmon->sellt]->arrange == monocle)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	arrange(selmon);
}

void
setmon(Client *c, Monitor *m, uint32_t newtags)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	solux_taganim_unclip(c);
	c->mon = m;
	c->taganim_role = TAGANIM_NONE;
	c->prev = c->geom;

	if (oldmon) {
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_output_leave(c->foreign_toplevel, oldmon->wlr_output);
	 		arrange(oldmon);
		arrange(oldmon);
	}
	if (m) {
		resize(c, c->geom, 0);
		c->tags = newtags ? newtags : m->tagset[m->seltags];
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_output_enter(c->foreign_toplevel, m->wlr_output);
		c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
		c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
		setfullscreen(c, c->isfullscreen);
		setfloating(c, c->isfloating);
	}
	focusclient(focustop(selmon), 1);
}


void
setpsel(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}


void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);


	wlr_log_init(log_level, NULL);

	dpy = wl_display_create();
	workspaces_init();
	event_loop = wl_display_get_event_loop(dpy);
	soluxctl_init();

	

	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	load_dnx_config();
	apply_runtime_modkey();

	

	setenv("WLR_SCENE_DISABLE_DIRECT_SCANOUT", "1", 1);

	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	if (scenefx_blur)
		wlr_scene_set_blur_data(scene, scenefx_blur_num_passes, scenefx_blur_radius,
				scenefx_blur_noise, scenefx_blur_brightness,
				scenefx_blur_contrast, scenefx_blur_saturation);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	

	if (!(drw = fx_renderer_create(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	

	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	

	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_ext_data_control_manager_v1_create(dpy, 1);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(dpy);

	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	wl_list_init(&mons);
	wl_list_init(&pointer_devices);
	wl_signal_add(&backend->events.new_output, &new_output);

	

	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&fadeout_clients);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 3);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	

	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);
	setenv("XDG_CURRENT_DESKTOP", "solux", 1);

	

	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);
	wl_signal_add(&cursor->events.swipe_begin, &cursor_swipe_begin);
	wl_signal_add(&cursor->events.swipe_update, &cursor_swipe_update);
	wl_signal_add(&cursor->events.swipe_end, &cursor_swipe_end);
	wl_signal_add(&cursor->events.pinch_begin, &cursor_pinch_begin);
	wl_signal_add(&cursor->events.pinch_update, &cursor_pinch_update);
	wl_signal_add(&cursor->events.pinch_end, &cursor_pinch_end);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	

	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
            &new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	if (!seat)
		die("solux: could not create seat");
	pointer_gestures = wlr_pointer_gestures_v1_create(dpy);
	if (!pointer_gestures)
		die("solux: could not create pointer gestures protocol");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);


	wl_global_create(dpy, &zdwl_ipc_manager_v2_interface, 2, NULL, dwl_ipc_manager_bind);

	unsetenv("DISPLAY");
#ifdef XWAYLAND
	

	if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
		setenv("DISPLAY", xwayland->display_name, 1);
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);
		 
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif
}


void
spawn(const Arg *arg)
{
	const char *command = arg ? (const char *)arg->v : NULL;

	if (!command || !*command)
		return;

	if (fork() == 0) {
		close(STDIN_FILENO);
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		dprintf(STDERR_FILENO, "solux: /bin/sh -c %s: %s\n", command, strerror(errno));
		_exit(127);
	}
}


void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	uint32_t newtags;
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	newtags = arg->ui & TAGMASK;
	if (sel->mon == selmon && solux_canvas_active(selmon) && sel->isfloating)
		solux_canvas_record_client_geom(sel);
	sel->tags = newtags;
	solux_apply_tag_layout(sel, selmon, newtags);
	if (!VISIBLEON(sel, selmon)) {
		sel->animation.running = false;
		wlr_scene_node_set_enabled(&sel->scene->node, false);
		client_set_suspended(sel, true);
	}
	focusclient(focustop(selmon), 1);
	arrange(selmon);
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i), 0);
}


void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
togglegaps(const Arg *arg)
{
	selmon->gaps = !selmon->gaps;
	solux_animation_relayout(selmon);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	if (sel->mon == selmon && solux_canvas_active(selmon) && sel->isfloating)
		solux_canvas_record_client_geom(sel);
	sel->tags = newtags;
	if (!VISIBLEON(sel, selmon)) {
		sel->animation.running = false;
		wlr_scene_node_set_enabled(&sel->scene->node, false);
		client_set_suspended(sel, true);
	}
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	ext_workspace_printstatus(selmon);
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	size_t i;
	unsigned int oldtag;
	uint32_t anim_oldtagset;
	bool anim_on;
	if (!selmon || !(newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK)))
		return;

	oldtag = selmon->pertag->curtag;
	anim_oldtagset = selmon->tagset[selmon->seltags];
	solux_taganim_freeze(selmon);
	solux_canvas_save_pertag(selmon);
	if (selmon->lt[selmon->sellt] && !selmon->lt[selmon->sellt]->arrange)
		solux_canvas_save_tag_geometries(selmon, oldtag);

	if (newtagset == (uint32_t)~0) {
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = 0;
	} else if (selmon->pertag->curtag == 0 ||
			!(newtagset & 1u << (selmon->pertag->curtag - 1))) {
		selmon->pertag->prevtag = selmon->pertag->curtag;
		for (i = 0; !(newtagset & 1u << i); i++) ;
		selmon->pertag->curtag = i + 1;
	}

	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
	selmon->tagset[selmon->seltags] = newtagset;

	anim_on = solux_taganim_should_run(selmon, oldtag);
	solux_canvas_load_pertag(selmon, selmon->pertag->curtag, !anim_on);
	solux_taganim_begin(selmon, anim_oldtagset, oldtag);
	arrange(selmon);
	if (selmon->lt[selmon->sellt] && !selmon->lt[selmon->sellt]->arrange)
		solux_canvas_load_tag_geometries(selmon, selmon->pertag->curtag);
	solux_pertag_restoring = true;
	focusclient(focustop(selmon), 1);
	solux_pertag_restoring = false;
	ext_workspace_printstatus(selmon);
}


void
unmapnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, unmap);
	solux_animation_client_close(c);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			solux_focus_restoring = true;
			focusclient(focustop(selmon), 1);
			solux_focus_restoring = false;
		}
	} else {
		wl_list_remove(&c->link);
		setmon(c, NULL, 0);
		wl_list_remove(&c->flink);
	}

	if (c->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_destroy(c->foreign_toplevel);
		c->foreign_toplevel = NULL;
	}

	wlr_scene_node_destroy(&c->scene->node);
	motionnotify(0, NULL, 0, 0, 0, 0);

	if (selmon)
		dwl_ipc_output_printstatus(selmon);
}


void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(selmon))
		printstatus();
	if (c->foreign_toplevel) {
		const char *title;
		if (!(title = client_get_title(c)))
			title = "broken";
		wlr_foreign_toplevel_handle_v1_set_title(c->foreign_toplevel, title);
	}
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;

	if (client_surface(c)->mapped) {
		setclientborderstate(c, BorderUrgent);
		update_client_focus_decorations(c, 1, 1);
	}
}

static void
view_on_monitor(Monitor *m, uint32_t tags, int toggle_tagset)
{
	size_t i;
	uint32_t newtags;
	unsigned int oldtag, newtag;
	uint32_t anim_oldtagset;
	bool anim_on;

	if (!m || !m->pertag)
		return;

	oldtag = m->pertag->curtag;
	anim_oldtagset = m->tagset[m->seltags];
	solux_taganim_freeze(m);
	solux_canvas_save_pertag(m);
	if (m->lt[m->sellt] && !m->lt[m->sellt]->arrange)
		solux_canvas_save_tag_geometries(m, oldtag);
	solux_pertag_save_focus(m);
	newtags = tags & TAGMASK;

	if (!newtags) {
		m->seltags ^= 1;
		newtag = m->pertag->prevtag;
		m->pertag->prevtag = oldtag;
		m->pertag->curtag = newtag;
	} else {
		if (!toggle_tagset && newtags == m->tagset[m->seltags])
			return;

		if (toggle_tagset)
			m->seltags ^= 1;
		m->tagset[m->seltags] = newtags;

		m->pertag->prevtag = oldtag;
		if (newtags == TAGMASK) {
			newtag = 0;
		} else {
			for (i = 0; i < TAGCOUNT; i++)
				if (newtags & (1u << i))
					break;
			newtag = i + 1;
		}
		m->pertag->curtag = newtag;
	}

	m->nmaster = m->pertag->nmasters[m->pertag->curtag];
	m->mfact = m->pertag->mfacts[m->pertag->curtag];
	m->sellt = m->pertag->sellts[m->pertag->curtag];
	m->lt[m->sellt] = m->pertag->ltidxs[m->pertag->curtag][m->sellt];
	m->lt[m->sellt ^ 1] = m->pertag->ltidxs[m->pertag->curtag][m->sellt ^ 1];

	anim_on = solux_taganim_should_run(m, oldtag);
	solux_canvas_load_pertag(m, m->pertag->curtag, !anim_on);
	solux_taganim_begin(m, anim_oldtagset, oldtag);
	arrange(m);
	if (m->lt[m->sellt] && !m->lt[m->sellt]->arrange)
		solux_canvas_load_tag_geometries(m, m->pertag->curtag);
	if (m == selmon) {
		solux_pertag_restoring = true;
		focusclient(solux_pertag_focus(m), 1);
		solux_pertag_restoring = false;
	}
}

void
view(const Arg *arg)
{
	if (!selmon || !arg)
		return;
	view_on_monitor(selmon, arg->ui, 1);
	printstatus();
}


void
winview(const Arg *a) {
	Arg b = {0};
	Client *sel = focustop(selmon);
	if(!sel)
		return;
	b.ui = sel -> tags;
	view(&b);
	return;
}


void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	if (&c->link == &clients)
		return;

	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}


int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:c:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'c')
			dnx_config_override = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("solux " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	if (dnx_config_override && access(dnx_config_override, R_OK) != 0) {
		die("Solux: configuration file '%s' does not exist or cannot be read", dnx_config_override);
	}

	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-c config] [-s startup command]", argv[0]);
}
