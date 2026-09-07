/*
 * See LICENSE file for copyright and license details.
 */
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
#include "ext-prot/wlr_ext_workspace_v1.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
// #include <wlr/types/wlr_scene_xwayland.h>
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

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))

/* Minimum size used during interactive window resize.
 * Change these two values if you want a different minimum size. */
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

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11 }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { ClkClient, ClkRoot }; /* clicks */

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
	void (*func)(const Arg *);
	Arg arg;
} Button;

typedef struct Pertag Pertag;
typedef struct Monitor Monitor;
typedef struct {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
	struct wlr_scene_rect *borders[4]; /* top, bottom, left, right */
	struct wlr_scene_rect *bordere[4]; /* top, bottom, left, right */
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
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
	uint32_t resize; /* configure serial of a pending resize */
	unsigned int kblayout_idx;
} Client;

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
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods; /* invalid if nsyms == 0 */
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must keep this field first */
	unsigned int type; /* LayerShell */

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
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
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
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
	/* Raw %monrules layout value; resolved after %layouts is loaded. */
	char *ltname;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
	float opacity_focus;
	float opacity_unfocus;
	int monitor;
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

/* function declarations */
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
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void spiral(Monitor *mon);
static void rightspiral(Monitor *mon);
static void rightdwindle(Monitor *mon);
static void righttile(Monitor *m);
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
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
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
static void createforeigntoplevel(Client *c);
static void factivatenotify(struct wl_listener *listener, void *data);
static void fclosenotify(struct wl_listener *listener, void *data);
static void fdestroynotify(struct wl_listener *listener, void *data);
static void ffullscreennotify(struct wl_listener *listener, void *data);

/* variables */
static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
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
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
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
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;



/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
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
	char **argv;
	size_t argc;
} DnxAutostartCommand;

static DnxAutostartCommand *autostart_cmds;
static size_t autostart_cmds_len, autostart_cmds_cap;
static pid_t *autostart_pids;
static size_t autostart_len;

/* configuration, allows nested code to access above variables */
#include "parsednx.h"

/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static int sloppyfocus               = 1;  /* focus follows mouse */
static int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static unsigned int borderpx         = 0;  /* border pixel of windows */
static float rootcolor[]             = COLOR(0x000000ff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */
static const char kblayout_file[] = "/tmp/dwl-keymap";

/* tagging */
#define MAX_TAGS 31
static char *tags[MAX_TAGS + 1] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", NULL
};
static size_t tagcount = 9;

/* logging */
static int log_level = WLR_ERROR;

/* nszmak patches */
static int gappx = 10;
static int smartgaps = 0;
static int gaps = 1;
static float default_opacity_unfocus = 0.99f;
static float default_opacity_focus = 1.0f;

static float urgentcolor[] = COLOR(0xff0000ff);
static float bordercolor[] = COLOR(0xff0000ff);
static float focuscolor[] = COLOR(0xff0000ff);
static float borders_focuscolor[] = COLOR(0xff0000ff);
static float borders_urgentcolor[] = COLOR(0xff0000ff);
static float bordere_focuscolor[] = COLOR(0xff0000ff);
static float bordere_urgentcolor[] = COLOR(0xff0000ff);
static unsigned int borderspx        = 2;
static unsigned int borderepx        = 2;  /* width of the border that start from inside the windows */
static unsigned int borderspx_offset = 1;  /* offset of the border that start from outside the windows */
static unsigned int borderepx_negative_offset = 0; /* offset of the border that start from inside the windows */
static float borderscolor[]          = COLOR(0xffffffff); /* color of the border that start from outside the windows */
static float borderecolor[]          = COLOR(0x000000ff); /* color of the border that start from inside the windows */
static int border_color_type         = BrdOriginal; /* borders to be colored (focuscolor, urgentcolor) */
static int borders_only_floating     = 0;


#define KEYBOARD_LAYOUT "us,ru"
#define KEYBOARD_OPTIONS "grp:win_space_toggle"

static const Rule default_rules[] = {
	/* app_id             title       tags mask     isfloating   monitor */
	{ "Gimp_EXAMPLE",     NULL,       0,            1,           1.00f,        0.20f,           -1 },
	{ "firefox_EXAMPLE",  NULL,       1 << 8,       0,           1.00f,        1.00f,           -1 },
    /* default/example rule: can be changed but cannot be eliminated; at least one rule must exist */
};

/* layout(s) */
static const Layout default_layouts[] = {
	/* symbol     arrange function */
	{ "[ dwindle ]",      dwindle },
	{ "[ spiral ]",       spiral },
	{ "[ tile ]",         tile },
	{ "[ rightdwindle ]", rightdwindle },
	{ "[ rightspiral ]",  rightspiral },
	{ "[ righttile ]",    righttile },
	{ "[ float ]",        NULL },    /* no layout function means floating behavior */
	{ "[ monocle ]",      monocle },
};

static Layout layouts[LENGTH(default_layouts)];
static unsigned char layout_symbol_owned[LENGTH(default_layouts)];

/* Number of layouts currently active.  This may be smaller than the
 * built-in storage when %layouts in config.dnx overrides the defaults. */
static size_t layouts_len = LENGTH(layouts);

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients due to
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899 */
static const MonitorRule default_monrules[] = {
   /* name        mfact  nmaster scale layout       rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard */
static struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.layout = "us,ru",
	.options = "grp:win_space_toggle",
};

static int repeat_rate = 25;
static int repeat_delay = 600;

/* Trackpad */
static int tap_to_click = 1;
static int tap_and_drag = 1;
static int drag_lock = 1;
static int natural_scrolling = 0;
static int disable_while_typing = 1;
static int left_handed = 0;
static int middle_button_emulation = 0;


static enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;



static enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;



static uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Runtime configurable primary modifier: super (logo) by default, alt optional. */
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

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char *termcmd[33] = { NULL };
static char *menucmd[33] = { NULL };
static char *fmcmd[33] = { NULL };
static char *suspendcmd[33] = { NULL };
static char *screencmd[33] = { NULL };
static char *screen1cmd[33] = { NULL };
static char *hlcmd[33] = { NULL };

/* Key and button bindings are loaded exclusively from config.dnx. */





static Rule *rules;
static size_t rules_len, rules_cap;
static MonitorRule *monrules;
static size_t monrules_len, monrules_cap;
static Key *keys;
static size_t keys_len, keys_cap;
static Button *buttons;
static size_t buttons_len, buttons_cap;

static void
dnx_oom(void)
{
	fprintf(stderr, "dwl: out of memory while growing DNX configuration\n");
	exit(EXIT_FAILURE);
}

static void *
dnx_realloc_array(void *ptr, size_t *cap, size_t len, size_t item_size)
{
	size_t newcap = *cap ? *cap : 16;
	void *p;

	while (newcap < len) {
		if (newcap > SIZE_MAX / 2)
			newcap = len;
		else
			newcap *= 2;
		if (newcap < len && newcap == SIZE_MAX)
			dnx_oom();
	}
	if (newcap > SIZE_MAX / item_size)
		dnx_oom();
	p = realloc(ptr, newcap * item_size);
	if (!p)
		dnx_oom();
	*cap = newcap;
	return p;
}

static void
dnx_init_dynamic_config(void)
{
	rules_cap = LENGTH(default_rules);
	rules = calloc(rules_cap ? rules_cap : 1, sizeof(*rules));
	if (!rules)
		dnx_oom();
	memcpy(rules, default_rules, LENGTH(default_rules) * sizeof(*rules));
	rules_len = LENGTH(default_rules);

	monrules_cap = LENGTH(default_monrules);
	monrules = calloc(monrules_cap ? monrules_cap : 1, sizeof(*monrules));
	if (!monrules)
		dnx_oom();
	memcpy(monrules, default_monrules, LENGTH(default_monrules) * sizeof(*monrules));
	monrules_len = LENGTH(default_monrules);

	/* Keys/buttons come exclusively from config.dnx. */
	keys = NULL;
	keys_len = keys_cap = 0;
	buttons = NULL;
	buttons_len = buttons_cap = 0;
}

#define TAGCOUNT (tagcount)

/* attempt to encapsulate suck into one file */
#include "client.h"

struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	int nmasters[MAX_TAGS + 1]; /* number of windows in master area */
	float mfacts[MAX_TAGS + 1]; /* mfacts per tag */
	unsigned int sellts[MAX_TAGS + 1]; /* selected layouts */
	const Layout *ltidxs[MAX_TAGS + 1][2]; /* matrix of tags and layouts indexes  */
};

#include "ext-prot/ext-workspace.h"

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
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
	/* rule matching */
	const char *appid, *title;
	uint32_t newtags = 0;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	if (c->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_app_id(c->foreign_toplevel, appid);
		wlr_foreign_toplevel_handle_v1_set_title(c->foreign_toplevel, title);
	}

	for (r = rules; r < rules + rules_len; r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			c->opacity_unfocus = r->opacity_unfocus;
			c->opacity_focus = r->opacity_focus;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
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
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
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

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
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
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;


	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
assignkeymap(struct wlr_keyboard *keyboard) {
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &xkb_rules,
					XKB_KEYMAP_COMPILE_NO_FLAGS);

	

	if (!keymap) {
		struct xkb_rule_names fallback = xkb_rules;
		fallback.layout = "us";
		keymap = xkb_keymap_new_from_names(context, &fallback,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	if (!keymap)
		die("failed to compile fallback keymap");

	wlr_keyboard_set_keymap(keyboard, keymap);

	/* Restore selected layout group after rebuilding keymap */
	if (keyboard->xkb_state && current_kblayout < xkb_keymap_num_layouts(keyboard->keymap))
		xkb_state_update_mask(keyboard->xkb_state,
			keyboard->modifiers.depressed,
			keyboard->modifiers.latched,
			keyboard->modifiers.locked,
			0, 0, current_kblayout);

	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
}

static void
reload_runtime_keymap(void)
{
	

	struct wlr_keyboard *keyboard;
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	xkb_layout_index_t group;
	uint32_t depressed, latched, locked;

	

	struct keyboard_group_device {
		struct wlr_keyboard *keyboard;
		struct wl_listener key;
		struct wl_listener modifiers;
		struct wl_listener keymap;
		struct wl_listener repeat_info;
		struct wl_listener destroy;
		struct wl_list link;
	} *device;

	if (!kb_group || !kb_group->wlr_group)
		return;

	keyboard = &kb_group->wlr_group->keyboard;
	if (!keyboard)
		return;

	

	group = 0;
	if (keyboard->xkb_state)
		group = xkb_state_serialize_layout(
			keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
	if (group == XKB_LAYOUT_INVALID)
		group = 0;

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context)
		die("failed to create xkb context");

	keymap = xkb_keymap_new_from_names(context, &xkb_rules,
			XKB_KEYMAP_COMPILE_NO_FLAGS);

	

	if (!keymap) {
		struct xkb_rule_names fallback = xkb_rules;
		fallback.layout = "us";
		keymap = xkb_keymap_new_from_names(context, &fallback,
				XKB_KEYMAP_COMPILE_NO_FLAGS);
	}

	if (!keymap)
		die("failed to compile fallback keymap");

	if (xkb_keymap_num_layouts(keymap) <= 0) {
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
		die("new keymap has no layouts");
	}

	if (group >= xkb_keymap_num_layouts(keymap))
		group = 0;

	/*
	 * Preserve only the ordinary modifier bits.  Do NOT copy the old XKB
	 * state object and do NOT use the old keymap anywhere.
	 */
	depressed = keyboard->modifiers.depressed;
	latched = keyboard->modifiers.latched;
	locked = keyboard->modifiers.locked;

	/*
	 * 1. Install the new keymap on the synthetic/group keyboard.
	 */
	wlr_keyboard_set_keymap(keyboard, keymap);

	

	wlr_keyboard_notify_modifiers(keyboard, depressed, latched, locked, 0);

	if (keyboard->xkb_state)
		xkb_state_update_mask(keyboard->xkb_state,
			depressed, latched, locked, 0, 0, group);

	keyboard->modifiers.group = group;

	wlr_keyboard_set_repeat_info(keyboard, repeat_rate, repeat_delay);

	

	wl_list_for_each(device, &kb_group->wlr_group->devices, link) {
		struct wlr_keyboard *tkb = device->keyboard;

		if (!tkb)
			continue;

		wlr_keyboard_set_keymap(tkb, keyboard->keymap);
		wlr_keyboard_notify_modifiers(tkb,
			depressed,
			latched,
			locked,
			0);
	}

	/*
	 * 4. The group keyboard is the keyboard exposed by the seat.
	 */
	wlr_seat_set_keyboard(seat, keyboard);
	wlr_seat_keyboard_notify_modifiers(seat, &keyboard->modifiers);

	current_kblayout = group;
	kblayout_idx = (unsigned int)-1;
	kblayout(kb_group);

	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
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

	/* Start every DNX %autostart entry independently and in parallel. */
	for (i = 0; i < autostart_cmds_len; i++) {
		if (!autostart_cmds[i].argc || !autostart_cmds[i].argv || !autostart_cmds[i].argv[0])
			continue;

		if ((autostart_pids[i] = fork()) < 0)
			die("fork autostart:");
		if (autostart_pids[i] == 0) {
			setsid();
			execvp(autostart_cmds[i].argv[0], autostart_cmds[i].argv);
			dprintf(STDERR_FILENO, "dwl: execvp %s: %s\n",
					autostart_cmds[i].argv[0], strerror(errno));
			_exit(127);
		}
	}

}

void
buttonpress(struct wl_listener *listener, void *data)
{
	unsigned int click;
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	click = ClkRoot;
	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	if (c)
		click = ClkClient;

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		/* Change focus if the button was pressed over a client. */
		if (click == ClkClient && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < buttons + buttons_len; b++) {
			uint32_t want = b->mod;
			if (want & MODKEY)
				want = (want & ~MODKEY) | runtime_modkey;

			if (CLEANMASK(mods) == CLEANMASK(want) && event->button == b->button &&
					click == b->click && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	/* TODO: allow usage of scroll wheel for mousebindings, it can be implemented
	 * by checking the event's orientation and the delta of the event */
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
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

	/* kill child processes */
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
	wlr_xcursor_manager_destroy(cursor_mgr);

	destroykeyboardgroup(&kb_group->destroy, NULL);

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(backend);

	soluxctl_cleanup();
	wl_display_destroy(dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&scene->tree.node);

}

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
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	free(m->pertag);
	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
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

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		/* Temporarily set the layer's current state to pending
		 * so that we can easily arrange it */
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		

		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	/* mark a pending resize as completed */
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
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);

	kblayout(kb_group);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	assignkeymap(&group->wlr_group->keyboard);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
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
		fprintf(stderr, "dwl: failed to apply monitor rule to %s\n",
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
	}

	ext_workspace_createmon(m);
	ext_workspace_printstatus(m);

	

	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	

	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;
	c->bws = borders_only_floating ? 0 : borderspx;
	c->bwe = borders_only_floating ? 0 : borderepx;
	//c->kblayout_idx = kb ? kb->modifiers.group : 0;
	/* Set default opacity*/
	c->opacity_unfocus = default_opacity_unfocus;
	c->opacity_focus = default_opacity_focus;
	c->opacity = default_opacity_unfocus;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
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
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focusclient(focustop(selmon), 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focusclient(focustop(selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
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
	free(c);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
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
	arrange(monitor);
	printstatus();
}

void
dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset)
{
	DwlIpcOutput *ipc_output;
	Monitor *monitor;
	unsigned int newtags = tagmask & TAGMASK;

	ipc_output = wl_resource_get_user_data(resource);
	if (!ipc_output)
		return;
	monitor = ipc_output->mon;

	if (!newtags || newtags == monitor->tagset[monitor->seltags])
		return;
	if (toggle_tagset)
		monitor->seltags ^= 1;

	monitor->tagset[monitor->seltags] = newtags;
	if (selmon == monitor)
		focusclient(focustop(monitor), 1);
	arrange(monitor);
	printstatus();
}

void
dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
setclientborderstate(Client *c, int state)
{
	const float *original = bordercolor;
	const float *start = borderscolor;
	const float *end = borderecolor;
	const float *active_original = bordercolor;
	const float *active_start = borderscolor;
	const float *active_end = borderecolor;
	int i;

	if (!c || !c->scene)
		return;

	switch (state) {
	case BorderFocus:
		active_original = focuscolor;
		active_start = borders_focuscolor;
		active_end = bordere_focuscolor;
		break;
	case BorderUrgent:
		active_original = urgentcolor;
		active_start = borders_urgentcolor;
		active_end = bordere_urgentcolor;
		break;
	case BorderNormal:
	default:
		break;
	}

	/*
	 * Restore all three layers before applying the active state. This also
	 * makes a runtime change of border_color_type safe during config reload.
	 */
	for (i = 0; i < 4; i++) {
		wlr_scene_rect_set_color(c->border[i], original);
		wlr_scene_rect_set_color(c->borders[i], start);
		wlr_scene_rect_set_color(c->bordere[i], end);
	}

	switch (border_color_type) {
	case BrdStart:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->borders[i], active_start);
		break;
	case BrdEnd:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->bordere[i], active_end);
		break;
	case BrdStartEnd:
		for (i = 0; i < 4; i++) {
			wlr_scene_rect_set_color(c->borders[i], active_start);
			wlr_scene_rect_set_color(c->bordere[i], active_end);
		}
		break;
	case BrdOriginal:
	default:
		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->border[i], active_original);
		break;
	}
}

void
focusclient(Client *c, int lift)
{
	/* Copied from wlroots/types/wlr_keyboard_group.c */
	struct keyboard_group_device {
		struct wlr_keyboard *keyboard;
		struct wl_listener key;
		struct wl_listener modifiers;
		struct wl_listener keymap;
		struct wl_listener repeat_info;
		struct wl_listener destroy;
		struct wl_list link; // wlr_keyboard_group.devices
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

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;
		c->opacity = c->opacity_focus;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag)
			setclientborderstate(c, BorderFocus);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with its color
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			setclientborderstate(old_c, BorderNormal);
			client_activate_surface(old, 0);
			if (old_c->foreign_toplevel)
				wlr_foreign_toplevel_handle_v1_set_activated(old_c->foreign_toplevel, 0);
			old_c->opacity = old_c->opacity_unfocus;
		}
	}

	/* Update keyboard layout */
	if (group) {
		// Update the first real device, because kb or group->kb is not a real
		// keyboard and its effective layout gets overwritten
		device = wl_container_of(group->devices.next, device, link);
		if (device->keyboard)
			wlr_keyboard_notify_modifiers(device->keyboard,
					device->keyboard->modifiers.depressed,
					device->keyboard->modifiers.latched,
					device->keyboard->modifiers.locked,
					//c ? c->kblayout_idx : 0
					kblayout_idx
		);
	}

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	if (selmon)
		dwl_ipc_output_printstatus(selmon);  

	/* Have a client, so focus its top-level wlr_surface */
	if (c->type == XDGShell) {
		client_notify_enter(client_surface(c), keyboard);
	}
	#ifdef XWAYLAND
		else if (c->type == X11) {
			wlr_seat_keyboard_notify_enter(seat, c->surface.xwayland->surface, NULL, 0, NULL);
		}
	#endif

	/* Activate the new client */
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
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

void
fibonacci(Monitor *mon, int s)
{
	unsigned int i = 0, n = 0;
	int nx, ny, nw, nh;
	unsigned int e = mon->gaps;
	const int minsize = MAX(1 + 2 * (int)borderpx,
			2 * (int)borderspx + 2 * (int)borderspx_offset);
	Client *c;

	 
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, mon) && !c->isfloating && !c->isfullscreen)
			n++;

	if (n == 0)
		return;

	if (smartgaps >= 0 && (unsigned int)smartgaps == n)
		e = 0;

	const int split_min = 2 * minsize + (int)gappx * e;

	

	nx = mon->w.x + (int)gappx * e;
	ny = mon->w.y + (int)gappx * e;
	nw = MAX(0, mon->w.width - 2 * (int)gappx * e);
	nh = MAX(0, mon->w.height - 2 * (int)gappx * e);

	wl_list_for_each(c, &clients, link) {
		 
		if (VISIBLEON(c, mon) && !c->isfloating && !c->isfullscreen) {
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
			i++;
		}
	}
}

void
spiral(Monitor *mon)
{
	fibonacci(mon, 0);
}

void
dwindle(Monitor *mon)
{
	fibonacci(mon, 1);
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

void
rightspiral(Monitor *mon)
{
	fibonacci(mon, 0);
	mirror_layout_horizontally(mon);
}

void
rightdwindle(Monitor *mon)
{
	fibonacci(mon, 1);
	mirror_layout_horizontally(mon);
}

void focusdir(const Arg *arg)
{
	/* Focus the left, right, up, down client relative to the current focused client on selmon */
	Client *c, *sel = focustop(selmon);
	int dist = INT_MAX;
	Client *newsel = NULL;
	int newdist = INT_MAX;

	if (!sel || sel->isfullscreen)
		return;
   wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, selmon))
      continue; /* skip non visible windows */

    if (arg->ui == 0 && sel->geom.x <= c->geom.x) {
      /* Client isn't on our left */
      continue;
    }
    if (arg->ui == 1 && sel->geom.x >= c->geom.x) {
      /* Client isn't on our right */
      continue;
    }
    if (arg->ui == 2 && sel->geom.y <= c->geom.y) {
      /* Client isn't above us */
      continue;
    }
    if (arg->ui == 3 && sel->geom.y >= c->geom.y) {
      /* Client isn't below us */
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
	// A zero length vector has no direction
	if (vector->x == 0 && vector->y == 0) return "";

	if (abs(vector->y) > abs(vector->x)) {
		// Careful: We are operating in a Y-inverted coordinate system.
		return (vector->y > 0) ? "bottom" : "top";
	} else {
		return (vector->x > 0) ? "right" : "left";
	}
}

uint32_t
vector_length(Vector *vector)
{
	// Euclidean distance formula
	return (uint32_t)sqrt(vector->x * vector->x + vector->y * vector->y);
}

// Spatial direction, based on focused client position.
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
		// No need to swap the same list
		return;
	}

	// Get the lists before and after list1
	prev1 = list1->prev;
	next1 = list1->next;

	// Get the lists before and after list2
	prev2 = list2->prev;
	next2 = list2->next;

	// Update the next and previous pointers of adjacent lists
	prev1->next = list2;
	next1->prev = list2;
	prev2->next = list1;
	next2->prev = list1;

	// Swap the next and previous pointers of the lists to actually swap them
	temp = *list1;
	*list1 = *list2;
	*list2 = temp;
}

void
swapdir(const Arg *arg)
{
	Client *c = NULL;
	Client *cfocused;

	if (arg->ui == 0)
		c = client_in_direction("left", (int *)1);
	if (arg->ui == 1)
		c = client_in_direction("right", (int *)1);
	if (arg->ui == 2)
		c = client_in_direction("top", (int *)1);
	if (arg->ui == 3)
		c = client_in_direction("bottom", (int *)1);

	if (c == NULL)
		return;

	cfocused = focustop(selmon);
	wl_list_swap(&cfocused->link, &c->link);
	arrange(selmon);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
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
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't recreate renderer");

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

void
incxkbrules(const Arg *arg)
{
	{
		unsigned int nlayouts = 1;
		char *p = xkb_rules.layout;
		if (p) {
			for (; *p; p++)
				if (*p == ',')
					nlayouts++;
		}
		if (nlayouts)
			current_kblayout = (current_kblayout + arg->i + nlayouts) % nlayouts;
	}
	assignkeymap(&kb_group->wlr_group->keyboard);

	/* This path changes the XKB group without a keyboard event. Publish it
	 * immediately so status consumers see the new layout even on the root. */
	kblayout(kb_group);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

void kblayout(KeyboardGroup *kb);
static void kblayout_idle(void *data);

static void
kblayout_idle(void *data)
{
	KeyboardGroup *kb = data;
	if (kb && kb->wlr_group)
		kblayout(kb);
}

void
kblayout(KeyboardGroup *kb)
{
	unsigned int idx;
	const char *layout_name;
	char tmp[PATH_MAX];
	FILE *f;

	if (!kb || !kb->wlr_group || !kb->wlr_group->keyboard.keymap)
		return;

	/* Always use XKB's effective group. current_kblayout is only the group
	 * selected by dwl itself; Alt+Shift/Win+Space can change XKB directly. */
	idx = kb->wlr_group->keyboard.modifiers.group;
	if (idx >= xkb_keymap_num_layouts(kb->wlr_group->keyboard.keymap))
		idx = 0;

	kblayout_idx = idx;
	layout_name = xkb_keymap_layout_get_name(
		kb->wlr_group->keyboard.keymap, idx);

	/* Publish atomically so soluxctl never reads a truncated/empty file. */
	if (*kblayout_file &&
		snprintf(tmp, sizeof(tmp), "%s.tmp", kblayout_file) < (int)sizeof(tmp) &&
		(f = fopen(tmp, "w"))) {
		fprintf(f, "%s\n", layout_name ? layout_name : "unknown");
		if (fclose(f) == 0)
			rename(tmp, kblayout_file);
		else
			remove(tmp);
	}
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	

	const Key *k;
	for (k = keys; k < keys + keys_len; k++) {
		uint32_t want = k->mod;
		if (want & MODKEY)
			want = (want & ~MODKEY) | runtime_modkey;
		if (CLEANMASK(mods) == CLEANMASK(want)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	size_t i;
	int handled = 0;
	const xkb_keysym_t *syms;
	int nsyms;
	xkb_layout_index_t layout;

	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	 
	uint32_t keycode = event->keycode + 8;

	 
	struct xkb_keymap *keymap = xkb_state_get_keymap(group->wlr_group->keyboard.xkb_state);

	 
	xkb_level_index_t level = xkb_state_key_get_level(group->wlr_group->keyboard.xkb_state, keycode, 0);

	 
	nsyms = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, level, &syms);

	uint32_t mods_pressed = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	 
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (locked) {
			 
		} else {
			 
			for (i = 0; i < keys_len; i++) {
				int match = 0;
				for (int s = 0; s < nsyms; s++) {
					if (syms[s] == keys[i].keysym) {
						match = 1;
						break;
					}
				}

				uint32_t want = keys[i].mod;
				if (want & MODKEY)
					want = (want & ~MODKEY) | runtime_modkey;

				if (match && CLEANMASK(want) == CLEANMASK(mods_pressed) && keys[i].func) {
					keys[i].func(&keys[i].arg);
					handled = 1;
					break; /* one physical key combination = one action */
				}
			}
		}
	}

	if (!handled) {
		 
		wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}

	

	wl_event_loop_add_idle(event_loop, kblayout_idle, group);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);

	/* XKB may update the effective group after this listener. Defer the
	 * read until the current event has completed. */
	wl_event_loop_add_idle(event_loop, kblayout_idle, group);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
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

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
		c->border[i]->node.data = c;

		c->borders[i] = wlr_scene_rect_create(c->scene, 0, 0, borderscolor);
		c->borders[i]->node.data = c;

		c->bordere[i] = wlr_scene_rect_create(c->scene, 0, 0, borderecolor);
		c->bordere[i]->node.data = c;
	}
	setclientborderstate(c, c->isurgent ? BorderUrgent : BorderNormal);

	createforeigntoplevel(c);

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists. */
	if (clients.prev)
		// tile at the bottom
		wl_list_insert(clients.prev, &c->link);
	else
		wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Set initial monitor, tags, floating status, and focus:
	 * we always consider floating, clients that have parent and thus
	 * we set the same tags and monitor as its parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon, p->tags);
	} else {
		applyrules(c);
	}

	if (borders_only_floating) {
		c->bws = c->isfloating ? borderspx : 0;
		c->bwe = c->isfloating ? borderepx : 0;
	}
	

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	

	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
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
motionabsolute(struct wl_listener *listener, void *data)
{
	

	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		resize(grabc, (struct wlr_box){.x = (int)round(cursor->x) - grabcx, .y = (int)round(cursor->y) - grabcy,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)round(cursor->x) - grabc->geom.x, .height = (int)round(cursor->y) - grabc->geom.y}, 1);
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	

	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	/* Interactive move/resize is available only in the floating layout. */
	if (!grabc->mon || grabc->mon->lt[grabc->mon->sellt]->arrange)
		return;

	/* The floating layout already permits free geometry changes. */
	setfloating(grabc, 1);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		grabcx = (int)round(cursor->x) - grabc->geom.x;
		grabcy = (int)round(cursor->y) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		wlr_cursor_warp_closest(cursor, NULL,
				grabc->geom.x + grabc->geom.width,
				grabc->geom.y + grabc->geom.height);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
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
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
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

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
rendermon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;
	unsigned int tiled_clients = 0;
	int fastpath = 0;

	wl_list_for_each(c, &clients, link) {
		if (!client_is_rendered_on_mon(c, m) || client_is_stopped(c))
			continue;
		if (!c->isfloating)
			tiled_clients++;
	}

	fastpath = tiled_clients > 12;

	wl_list_for_each(c, &clients, link) {
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, scenebuffersetopacity, c);

		if (!fastpath && c->resize && !c->isfloating &&
			client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
			goto skip;
	}

	wlr_scene_output_commit(m->scene_output, NULL);

skip:
	/* Let clients know a frame has been rendered */
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
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
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	/* Keep an interactively resized window from becoming too small.
	 * These limits are intentionally applied only to interactive resizing,
	 * so tiled layouts can still calculate their geometry normally. */
	if (interact) {
		geo.width = MAX(MIN_WINDOW_WIDTH, geo.width);
		geo.height = MAX(MIN_WINDOW_HEIGHT, geo.height);
	}
	geo.width = MAX(1 + 2 * (int)c->bw, geo.width);
	geo.height = MAX(1 + 2 * (int)c->bw, geo.height);

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	applybounds(c, bbox);

	/* Update scene-graph, including borders */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	wlr_scene_rect_set_size(c->borders[0], c->geom.width - 2 * borderspx_offset, c->bws);
	wlr_scene_rect_set_size(c->borders[1], c->geom.width - 2 * borderspx_offset, c->bws);
	wlr_scene_rect_set_size(c->borders[2], c->bws, c->geom.height - 2 * c->bws - 2 * borderspx_offset);
	wlr_scene_rect_set_size(c->borders[3], c->bws, c->geom.height - 2 * c->bws - 2 * borderspx_offset);
	wlr_scene_node_set_position(&c->borders[0]->node, borderspx_offset, borderspx_offset);
	wlr_scene_node_set_position(&c->borders[1]->node, borderspx_offset, c->geom.height - c->bws - borderspx_offset);
	wlr_scene_node_set_position(&c->borders[2]->node, borderspx_offset, c->bws + borderspx_offset);
	wlr_scene_node_set_position(&c->borders[3]->node, c->geom.width - c->bws - borderspx_offset, c->bws + borderspx_offset);

	wlr_scene_rect_set_size(c->bordere[0], c->geom.width - (c->bw - c->bwe) * 2 + borderepx_negative_offset * 2, c->bwe);
	wlr_scene_rect_set_size(c->bordere[1], c->geom.width - (c->bw - c->bwe) * 2 + borderepx_negative_offset * 2, c->bwe);
	wlr_scene_rect_set_size(c->bordere[2], c->bwe, c->geom.height - 2 * c->bw + 2 * borderepx_negative_offset);
	wlr_scene_rect_set_size(c->bordere[3], c->bwe, c->geom.height - 2 * c->bw + 2 * borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[0]->node, c->bw - c->bwe - borderepx_negative_offset, c->bw - c->bwe - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[1]->node, c->bw - c->bwe - borderepx_negative_offset, c->geom.height - c->bw + borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[2]->node, c->bw - c->bwe - borderepx_negative_offset, c->bw - borderepx_negative_offset);
	wlr_scene_node_set_position(&c->bordere[3]->node, c->geom.width - c->bw + borderepx_negative_offset, c->bw - borderepx_negative_offset);

	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	#ifdef XWAYLAND
		if (c->type == X11) {
			wlr_xwayland_surface_configure(c->surface.xwayland, c->geom.x, c->geom.y, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
		}
	#endif
		client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

void
run(char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that the socket exists and the backend is started, run the startup command */
	autostartexec();
	if (startup_cmd) {
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			close(STDIN_FILENO);
			setsid();
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			dprintf(STDERR_FILENO, "dwl: startup command: %s\n", strerror(errno));
			_exit(127);
		}
	}

	/* Mark stdout as non-blocking to avoid the startup script
	 * causing dwl to freeze when a user neither closes stdin
	 * nor consumes standard input in his startup script */

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	

	wl_display_run(dpy);
}

void
scenebuffersetopacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	Client *c = data;
	/* xdg-popups are children of Client.scene, we do not have to worry about
	 * messing with them. */
	wlr_scene_buffer_set_opacity(buffer, c->isfullscreen ? 1 : c->opacity);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	

	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	c->isfloating = floating;
	if (!c->mon || !client_surface(c)->mapped)
		return;

	if (borders_only_floating) {
		c->bws = c->isfloating ? borderspx : 0;
		c->bwe = c->isfloating ? borderepx : 0;
	}

	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating && c->mon->lt[c->mon->sellt]->arrange ? LyrFloat : LyrTile]);
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
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev, 0);
	}
	wlr_scene_node_for_each_buffer(&c->scene_surface->node, scenebuffersetopacity, c);
	arrange(c->mon);
}

void
setlayout(const Arg *arg)
{
	const Layout *oldlt;
	Client *c;

	if (!selmon)
		return;

	oldlt = selmon->lt[selmon->sellt];
	if (!arg || !arg->v || arg->v != oldlt)
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;

	

	if (!oldlt->arrange && selmon->lt[selmon->sellt]->arrange) {
		wl_list_for_each(c, &clients, link) {
			if (c->mon == selmon && VISIBLEON(c, selmon) && c->isfloating && !c->isfullscreen)
				setfloating(c, 0);
		}
	}

	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof(selmon->ltsymbol));
	arrange(selmon);
	printstatus();
}

/* arg > 1.0 will set mfact absolutely */
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
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon) {
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_output_leave(c->foreign_toplevel, oldmon->wlr_output);
	 		arrange(oldmon);
		arrange(oldmon);
	}
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_output_enter(c->foreign_toplevel, m->wlr_output);
		c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
		c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
		setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
		setfloating(c, c->isfloating);
	}
	focusclient(focustop(selmon), 1);
}


void
setopacityunfocus(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;

	sel->opacity_unfocus += arg->f;
	if (sel->opacity_unfocus > 1.0)
		sel->opacity_unfocus = 1.0f;

	if (sel->opacity_unfocus < 0.1)
		sel->opacity_unfocus = 0.1f;

	wlr_scene_node_for_each_buffer(&sel->scene_surface->node, scenebuffersetopacity, sel);
}

void
setopacityfocus(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;

	sel->opacity_focus += arg->f;
	if (sel->opacity_focus > 1.0)
		sel->opacity_focus = 1.0f;

	if (sel->opacity_focus < 0.1)
		sel->opacity_focus = 0.1f;

	/* Change opacity from current client */
	sel->opacity = sel->opacity_focus;

	wlr_scene_node_for_each_buffer(&sel->scene_surface->node, scenebuffersetopacity, sel);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}


#define DEF_CFG "default/config.dnx"
#define USR_CFG "~/.config/solux/config.dnx"
#define SOLUX_STANDARD_CFG "/etc/solux/config.dnx"

static const char *dnx_config_override;

static char *dnx_strdup(const char *s)
{
	size_t n;
	char *p;

	if (!s)
		return NULL;
	n = strlen(s) + 1;
	p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

static char *dnx_strdup_or_null(const char *s)
{
	return dnx_strdup(s);
}

static size_t dnx_tags_owned;

static void
dnx_reset_tags(void)
{
	static const char *default_tags[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9"
	};
	size_t i;

	for (i = 0; i < dnx_tags_owned; i++)
		free(tags[i]);

	for (i = 0; i <= MAX_TAGS; i++)
		tags[i] = NULL;

	for (i = 0; i < LENGTH(default_tags); i++) {
		tags[i] = dnx_strdup(default_tags[i]);
		if (!tags[i])
			dnx_oom();
	}
	dnx_tags_owned = LENGTH(default_tags);
	tagcount = LENGTH(default_tags);
}

static char *dnx_unquote_value(const char *s)
{
	char *p, *r;
	size_t n;

	if (!s)
		return NULL;
	p = dnx_strdup(s);
	if (!p)
		return NULL;
	n = strlen(p);
	if (n >= 2 && ((p[0] == '"' && p[n - 1] == '"') ||
			(p[0] == '\'' && p[n - 1] == '\''))) {
		p[n - 1] = '\0';
		r = p + 1;
		memmove(p, r, strlen(r) + 1);
	}
	return p;
}

static uint32_t
dnx_hex_color(const char *s, uint32_t fallback)
{
	char *end;
	unsigned long v;

	if (!s)
		return fallback;
	while (isspace((unsigned char)*s))
		s++;
	if (!strncasecmp(s, "0x", 2))
		s += 2;
	else if (*s == '#')
		s++;
	if (!*s)
		return fallback;
	errno = 0;
	v = strtoul(s, &end, 16);
	if (errno || end == s)
		return fallback;
	while (isspace((unsigned char)*end))
		end++;
	if (*end)
		return fallback;
	return (uint32_t)v;
}

static void
dnx_set_rgba(float dst[4], const char *s)
{
	float v[4];
	int n = 0;
	const char *p = s;
	char *end;

	if (!s)
		return;
	while (n < 4) {
		while (isspace((unsigned char)*p) || *p == ',')
			p++;
		if (!*p)
			break;
		v[n] = strtof(p, &end);
		if (end == p)
			break;
		p = end;
		if (*p == 'f' || *p == 'F')
			p++;
		n++;
	}
	if (n == 4) {
		for (int i = 0; i < 4; i++)
			dst[i] = v[i];
	}
}

static void
dnx_set_hex_rgba(float dst[4], const char *s)
{
	uint32_t c = dnx_hex_color(s, 0);
	dst[0] = ((c >> 24) & 0xff) / 255.0f;
	dst[1] = ((c >> 16) & 0xff) / 255.0f;
	dst[2] = ((c >> 8) & 0xff) / 255.0f;
	dst[3] = (c & 0xff) / 255.0f;
}

static int
dnx_is_null(const char *s)
{
	return !s || !strcasecmp(s, "null") || !strcasecmp(s, "none");
}

static int
dnx_layout_index(const char *s)
{
	char *end;
	long n;
	char name[128];
	const char *p, *q;
	size_t len;

	if (!s)
		return -1;

	/* Numeric layout indexes are accepted as before. */
	n = strtol(s, &end, 0);
	if (end != s && !*end && n >= 0 && n < (long)layouts_len)
		return (int)n;

	

	p = s;
	while (isspace((unsigned char)*p))
		p++;
	q = p + strlen(p);
	while (q > p && isspace((unsigned char)q[-1]))
		q--;
	if (q > p && *p == '[' && q[-1] == ']') {
		p++;
		q--;
		while (q > p && isspace((unsigned char)q[-1]))
			q--;
		while (*p && isspace((unsigned char)*p))
			p++;
	}
	len = (size_t)(q - p);
	if (len >= sizeof(name))
		len = sizeof(name) - 1;
	memcpy(name, p, len);
	name[len] = '\0';

	for (size_t i = 0; i < layouts_len; i++) {
		if (layouts[i].symbol && !strcasecmp(layouts[i].symbol, s))
			return (int)i;

		/* Match the normalized name against the displayed symbol too. */
		if (layouts[i].symbol) {
			const char *sp = layouts[i].symbol;
			while (isspace((unsigned char)*sp))
				sp++;
			if (*sp == '[') {
				sp++;
				while (isspace((unsigned char)*sp))
					sp++;
			}
			q = sp + strlen(sp);
			while (q > sp && isspace((unsigned char)q[-1]))
				q--;
			if (q > sp && q[-1] == ']')
				q--;
			while (q > sp && isspace((unsigned char)q[-1]))
				q--;
			len = (size_t)(q - sp);
			if (len == strlen(name) && !strncasecmp(sp, name, len))
				return (int)i;
		}
	}

	return -1;
}

static void (*dnx_arrange_for_name(const char *name))(Monitor *)
{
	if (dnx_is_null(name) || !strcasecmp(name, "float"))
		return NULL;
	if (!strcasecmp(name, "dwindle")) return dwindle;
	if (!strcasecmp(name, "spiral")) return spiral;
	if (!strcasecmp(name, "tile")) return tile;
	if (!strcasecmp(name, "rightdwindle")) return rightdwindle;
	if (!strcasecmp(name, "rightspiral")) return rightspiral;
	if (!strcasecmp(name, "righttile")) return righttile;
	if (!strcasecmp(name, "monocle")) return monocle;
	return NULL;
}

static enum wl_output_transform
dnx_transform(const char *s)
{
	if (!s) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "WL_OUTPUT_TRANSFORM_NORMAL")) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "NORMAL")) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "90")) return WL_OUTPUT_TRANSFORM_90;
	if (!strcasecmp(s, "180")) return WL_OUTPUT_TRANSFORM_180;
	if (!strcasecmp(s, "270")) return WL_OUTPUT_TRANSFORM_270;
	if (!strcasecmp(s, "FLIPPED")) return WL_OUTPUT_TRANSFORM_FLIPPED;
	if (!strcasecmp(s, "FLIPPED_90")) return WL_OUTPUT_TRANSFORM_FLIPPED_90;
	if (!strcasecmp(s, "FLIPPED_180")) return WL_OUTPUT_TRANSFORM_FLIPPED_180;
	if (!strcasecmp(s, "FLIPPED_270")) return WL_OUTPUT_TRANSFORM_FLIPPED_270;
	return WL_OUTPUT_TRANSFORM_NORMAL;
}

static uint32_t
dnx_mods(const char *s)
{
	uint32_t m = 0;
	char *tmp, *p, *save;

	if (!s || !*s || !strcasecmp(s, "NONE"))
		return 0;
	tmp = dnx_strdup(s);
	if (!tmp)
		return 0;
	for (p = strtok_r(tmp, "|+,", &save); p; p = strtok_r(NULL, "|+,", &save)) {
		while (isspace((unsigned char)*p)) p++;
		if (!strcasecmp(p, "MODKEY"))
			m |= MODKEY;
		else if (!strcasecmp(p, "MODSUPER") || !strcasecmp(p, "SUPER"))
			m |= MODSUPER;
		else if (!strcasecmp(p, "LOGO"))
			m |= MODSUPER;
		else if (!strcasecmp(p, "MODALT"))
			m |= MODALT;
		else if (!strcasecmp(p, "MODCTRL"))
			m |= MODCTRL;
		else if (!strcasecmp(p, "MODNONE"))
			m |= MODNONE;
		else if (!strcasecmp(p, "SHIFT"))
			m |= WLR_MODIFIER_SHIFT;
		else if (!strcasecmp(p, "CTRL") || !strcasecmp(p, "CONTROL"))
			m |= WLR_MODIFIER_CTRL;
		else if (!strcasecmp(p, "ALT") || !strcasecmp(p, "MOD1"))
			m |= WLR_MODIFIER_ALT;
		else if (!strcasecmp(p, "MOD2"))
			m |= WLR_MODIFIER_MOD2;
		else if (!strcasecmp(p, "MOD3"))
			m |= WLR_MODIFIER_MOD3;
		else if (!strcasecmp(p, "MOD4"))
			m |= WLR_MODIFIER_LOGO;
		else if (!strcasecmp(p, "MOD5"))
			m |= WLR_MODIFIER_MOD5;
		else
			m |= (uint32_t)strtoul(p, NULL, 0);
	}
	free(tmp);
	return m;
}

static xkb_keysym_t
dnx_keysym(const char *s)
{
	xkb_keysym_t k;
	char *tmp;

	if (!s) return XKB_KEY_NoSymbol;
	tmp = dnx_unquote_value(s);
	if (!tmp) return XKB_KEY_NoSymbol;
	if (!strncasecmp(tmp, "XKB_KEY_", 8))
		memmove(tmp, tmp + 8, strlen(tmp + 8) + 1);
	k = xkb_keysym_from_name(tmp, XKB_KEYSYM_CASE_INSENSITIVE);
	if (k == XKB_KEY_NoSymbol) {
		char *end;
		unsigned long n = strtoul(tmp, &end, 0);
		if (end != tmp && !*end)
			k = (xkb_keysym_t)n;
	}
	free(tmp);
	return k;
}



typedef struct {
	char *name;
	char **argv;
	size_t argc;
	int source;
} DnxCommand;

static DnxCommand *dnx_commands;
static size_t dnx_commands_len, dnx_commands_cap;

static int
dnx_is_builtin_command(const char *name)
{
	static const char *const names[] = {
		"termcmd", "menucmd", "fmcmd", "suspendcmd",
		"screencmd", "screen1cmd", "hlcmd"
	};
	size_t i;

	if (!name)
		return 0;
	for (i = 0; i < LENGTH(names); i++)
		if (!strcasecmp(name, names[i]))
			return 1;
	return 0;
}

static DnxCommand *
dnx_find_command(const char *name)
{
	size_t i;

	if (!name)
		return NULL;
	for (i = 0; i < dnx_commands_len; i++)
		if (!strcasecmp(dnx_commands[i].name, name))
			return &dnx_commands[i];
	return NULL;
}

static void
dnx_command_register(const DnxEntry *e)
{
	DnxCommand *cmd;
	size_t i, oldargc;

	if (!e || e->type != DNX_LIST || !e->key || dnx_is_builtin_command(e->key))
		return;

	cmd = dnx_find_command(e->key);
	if (!cmd) {
		if (dnx_commands_len >= dnx_commands_cap) {
			dnx_commands = dnx_realloc_array(dnx_commands, &dnx_commands_cap,
					dnx_commands_len + 1, sizeof(*dnx_commands));
		}
		cmd = &dnx_commands[dnx_commands_len++];
		memset(cmd, 0, sizeof(*cmd));
		cmd->name = dnx_strdup(e->key);
		if (!cmd->name)
			dnx_oom();
	}

	

	if (e->source == 1 && cmd->source != 1) {
		for (i = 0; i < cmd->argc; i++)
			free(cmd->argv[i]);
		free(cmd->argv);
		cmd->argv = NULL;
		cmd->argc = 0;
		cmd->source = 1;
	} else if (!cmd->argv) {
		cmd->source = e->source;
	}

	oldargc = cmd->argc;
	if (e->count > SIZE_MAX - oldargc - 1)
		dnx_oom();

	cmd->argv = realloc(cmd->argv, (oldargc + e->count + 1) * sizeof(*cmd->argv));
	if (!cmd->argv)
		dnx_oom();

	for (i = 0; i < e->count; i++) {
		cmd->argv[oldargc + i] = dnx_strdup_or_null(e->items[i]);
		if (!cmd->argv[oldargc + i])
			dnx_oom();
	}
	cmd->argc = oldargc + e->count;
	cmd->argv[cmd->argc] = NULL;
}

static char **
dnx_command_for_name(const char *name)
{
	DnxCommand *cmd;

	if (!name)
		return NULL;

	cmd = dnx_find_command(name);
	if (cmd)
		return cmd->argv;

	if (!strcasecmp(name, "termcmd")) return termcmd;
	if (!strcasecmp(name, "menucmd")) return menucmd;
	if (!strcasecmp(name, "fmcmd")) return fmcmd;
	if (!strcasecmp(name, "suspendcmd")) return suspendcmd;
	if (!strcasecmp(name, "screencmd")) return screencmd;
	if (!strcasecmp(name, "screen1cmd")) return screen1cmd;
	if (!strcasecmp(name, "hlcmd")) return hlcmd;
	return NULL;
}

static int dnx_collecting_commands;

static int
dnx_collect_commands_cb(const DnxEntry *e, void *userdata)
{
	(void)userdata;
	if (e && e->type == DNX_LIST && e->key && !dnx_is_builtin_command(e->key) &&
			strcasecmp(e->key, "tags"))
		dnx_command_register(e);
	return 0;
}

static void
dnx_set_command(char **dst, size_t cap, const DnxEntry *e)
{
	size_t n = e->count;
	if (n >= cap) n = cap - 1;
	for (size_t i = 0; i < n; i++)
		dst[i] = dnx_strdup_or_null(e->items[i]);
	dst[n] = NULL;
}

static int
dnx_bool_value(const char *s, int fallback)
{
	if (!s)
		return fallback;
	if (!strcasecmp(s, "1") || !strcasecmp(s, "true") ||
			!strcasecmp(s, "yes") || !strcasecmp(s, "on"))
		return 1;
	if (!strcasecmp(s, "0") || !strcasecmp(s, "false") ||
			!strcasecmp(s, "no") || !strcasecmp(s, "off"))
		return 0;
	return atoi(s) != 0;
}

static void
dnx_load_scalar(const DnxEntry *e)
{
	const char *k = e->key;
	const char *v = e->value;

	if (!k || !v) return;


	/* appearance */
	if (!strcasecmp(k, "sloppyfocus")) sloppyfocus = atoi(v);
	else if (!strcasecmp(k, "bypass_surface_visibility")) bypass_surface_visibility = atoi(v);
	else if (!strcasecmp(k, "borderpx")) borderpx = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "rootcolor")) dnx_set_hex_rgba(rootcolor, v);
	else if (!strcasecmp(k, "fullscreen_bg")) dnx_set_rgba(fullscreen_bg, v);
	else if (!strcasecmp(k, "log_level")) {
		if (!strcasecmp(v, "WLR_ERROR")) log_level = WLR_ERROR;
		else if (!strcasecmp(v, "WLR_DEBUG")) log_level = WLR_DEBUG;
		else if (!strcasecmp(v, "WLR_INFO")) log_level = WLR_INFO;
		else if (!strcasecmp(v, "WLR_SILENT")) log_level = WLR_SILENT;
		else log_level = atoi(v);
	}

	/* modifier key */
	else if (!strcasecmp(k, "modkey")) {
		if (!strcasecmp(v, "alt"))
			runtime_modkey = WLR_MODIFIER_ALT;
		else
			runtime_modkey = WLR_MODIFIER_LOGO;
	}

	/* gaps */
	else if (!strcasecmp(k, "gappx")) gappx = atoi(v);
	else if (!strcasecmp(k, "smartgaps")) smartgaps = atoi(v);
	else if (!strcasecmp(k, "gaps")) gaps = atoi(v);
	else if (!strcasecmp(k, "default_opacity_unfocus")) default_opacity_unfocus = strtof(v, NULL);
	else if (!strcasecmp(k, "default_opacity_focus")) default_opacity_focus = strtof(v, NULL);

	/* borders */
	else if (!strcasecmp(k, "urgentcolor")) dnx_set_hex_rgba(urgentcolor, v);
	else if (!strcasecmp(k, "bordercolor")) dnx_set_hex_rgba(bordercolor, v);
	else if (!strcasecmp(k, "focuscolor")) dnx_set_hex_rgba(focuscolor, v);
	else if (!strcasecmp(k, "borders_focuscolor")) dnx_set_hex_rgba(borders_focuscolor, v);
	else if (!strcasecmp(k, "borders_urgentcolor")) dnx_set_hex_rgba(borders_urgentcolor, v);
	else if (!strcasecmp(k, "bordere_focuscolor")) dnx_set_hex_rgba(bordere_focuscolor, v);
	else if (!strcasecmp(k, "bordere_urgentcolor")) dnx_set_hex_rgba(bordere_urgentcolor, v);
	else if (!strcasecmp(k, "borderspx")) borderspx = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderepx")) borderepx = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderspx_offset")) borderspx_offset = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderepx_negative_offset")) borderepx_negative_offset = (unsigned)strtoul(v, NULL, 0);
	else if (!strcasecmp(k, "borderscolor")) dnx_set_hex_rgba(borderscolor, v);
	else if (!strcasecmp(k, "borderecolor")) dnx_set_hex_rgba(borderecolor, v);
	else if (!strcasecmp(k, "border_color_type")) {
		if (!strcasecmp(v, "BrdOriginal")) border_color_type = BrdOriginal;
		else if (!strcasecmp(v, "BrdStart")) border_color_type = BrdStart;
		else if (!strcasecmp(v, "BrdEnd")) border_color_type = BrdEnd;
		else if (!strcasecmp(v, "BrdStartEnd")) border_color_type = BrdStartEnd;
		else border_color_type = atoi(v);
	}
	else if (!strcasecmp(k, "borders_only_floating")) borders_only_floating = atoi(v);

	/* input */
	else if (!strcasecmp(k, "keyboard_rules")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.rules=x; }
	else if (!strcasecmp(k, "keyboard_model")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.model=x; }
	else if (!strcasecmp(k, "keyboard_layout")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.layout=x; }
	else if (!strcasecmp(k, "keyboard_variant")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.variant=x; }
	else if (!strcasecmp(k, "keyboard_options")) { char *x=dnx_unquote_value(v); if(x) xkb_rules.options=x; }
	else if (!strcasecmp(k, "repeat_rate")) repeat_rate = atoi(v);
	else if (!strcasecmp(k, "repeat_delay")) repeat_delay = atoi(v);
	else if (!strcasecmp(k, "tap_to_click")) tap_to_click = dnx_bool_value(v, tap_to_click);
	else if (!strcasecmp(k, "tap_and_drag")) tap_and_drag = dnx_bool_value(v, tap_and_drag);
	else if (!strcasecmp(k, "drag_lock")) drag_lock = dnx_bool_value(v, drag_lock);
	else if (!strcasecmp(k, "natural_scrolling")) natural_scrolling = dnx_bool_value(v, natural_scrolling);
	else if (!strcasecmp(k, "disable_while_typing")) disable_while_typing = dnx_bool_value(v, disable_while_typing);
	else if (!strcasecmp(k, "left_handed")) left_handed = dnx_bool_value(v, left_handed);
	else if (!strcasecmp(k, "middle_button_emulation")) middle_button_emulation = dnx_bool_value(v, middle_button_emulation);
	else if (!strcasecmp(k, "scroll_method")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_NO_SCROLL")) scroll_method=LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_2FG")) scroll_method=LIBINPUT_CONFIG_SCROLL_2FG;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_EDGE")) scroll_method=LIBINPUT_CONFIG_SCROLL_EDGE;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN")) scroll_method=LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
		else scroll_method=(enum libinput_config_scroll_method)atoi(v);
	}
	else if (!strcasecmp(k, "click_method")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_NONE")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_NONE;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER")) click_method=LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
		else click_method=(enum libinput_config_click_method)atoi(v);
	}
	else if (!strcasecmp(k, "send_events_mode")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_ENABLED")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_DISABLED")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE")) send_events_mode=LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
		else send_events_mode=(uint32_t)strtoul(v,NULL,0);
	}
	else if (!strcasecmp(k, "accel_profile")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT")) accel_profile=LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE")) accel_profile=LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		else accel_profile=(enum libinput_config_accel_profile)atoi(v);
	}
	else if (!strcasecmp(k, "accel_speed")) accel_speed = strtod(v, NULL);
	else if (!strcasecmp(k, "button_map")) {
		if (!strcasecmp(v, "LIBINPUT_CONFIG_TAP_MAP_LRM")) button_map=LIBINPUT_CONFIG_TAP_MAP_LRM;
		else if (!strcasecmp(v, "LIBINPUT_CONFIG_TAP_MAP_LMR")) button_map=LIBINPUT_CONFIG_TAP_MAP_LMR;
		else button_map=(enum libinput_config_tap_button_map)atoi(v);
	}
	else if (!strcasecmp(k, "monitor_name")) monrules[0].name = dnx_is_null(v) ? NULL : dnx_unquote_value(v);
	else if (!strcasecmp(k, "mfact")) monrules[0].mfact = strtof(v,NULL);
	else if (!strcasecmp(k, "nmaster")) monrules[0].nmaster = atoi(v);
	else if (!strcasecmp(k, "scale")) monrules[0].scale = strtof(v,NULL);
	else if (!strcasecmp(k, "layout")) {
		int li = dnx_layout_index(v);
		if (li >= 0) monrules[0].lt = &layouts[li];
	}
	else if (!strcasecmp(k, "transform")) monrules[0].rr = dnx_transform(v);
	else if (!strcasecmp(k, "x")) monrules[0].x = atoi(v);
	else if (!strcasecmp(k, "y")) monrules[0].y = atoi(v);

#undef I
#undef B
#undef F
}

static void
dnx_list_append(char **dst, size_t cap, size_t *n, const DnxEntry *e)
{
	char *value;

	if (!e || !e->count || *n + 1 >= cap)
		return;
	value = dnx_strdup(e->items[0]);
	if (!value)
		return;
	dst[*n] = value;
	(*n)++;
	dst[*n] = NULL;
}

static void
dnx_list_append_const(const char **dst, size_t cap, size_t *n, const DnxEntry *e)
{
	char *value;

	if (!e || !e->count || *n + 1 >= cap)
		return;
	value = dnx_strdup(e->items[0]);
	if (!value)
		return;
	dst[*n] = value;
	(*n)++;
	dst[*n] = NULL;
}

static void
dnx_autostart_append(const DnxEntry *e)
{
	DnxAutostartCommand *cmd;
	size_t i;

	if (!e || e->type != DNX_TABLE || !e->count || !e->items[0])
		return;

	if (autostart_cmds_len >= autostart_cmds_cap) {
		autostart_cmds = dnx_realloc_array(autostart_cmds, &autostart_cmds_cap,
				autostart_cmds_len + 1, sizeof(*autostart_cmds));
	}

	cmd = &autostart_cmds[autostart_cmds_len++];
	memset(cmd, 0, sizeof(*cmd));
	cmd->argc = e->count;
	cmd->argv = calloc(cmd->argc + 1, sizeof(*cmd->argv));
	if (!cmd->argv)
		dnx_oom();

	for (i = 0; i < cmd->argc; i++) {
		cmd->argv[i] = dnx_strdup_or_null(e->items[i]);
		if (!cmd->argv[i])
			dnx_oom();
	}
	cmd->argv[cmd->argc] = NULL;
}

static void
dnx_autostart_clear(void)
{
	size_t i, j;

	for (i = 0; i < autostart_cmds_len; i++) {
		for (j = 0; j < autostart_cmds[i].argc; j++)
			free(autostart_cmds[i].argv[j]);
		free(autostart_cmds[i].argv);
	}
	free(autostart_cmds);
	autostart_cmds = NULL;
	autostart_cmds_len = autostart_cmds_cap = 0;
}

static size_t dnx_tag_n;
static size_t dnx_cmd_n[8];
static int dnx_user_list_started[8];
static int dnx_user_table_started[5];
static int dnx_user_autostart_started;
static size_t dnx_rules_n, dnx_layouts_n, dnx_monrules_n, dnx_keys_n, dnx_buttons_n;

static void
dnx_reset_parser_state(void)
{
	dnx_tag_n = 0;
	memset(dnx_cmd_n, 0, sizeof(dnx_cmd_n));
	memset(dnx_user_list_started, 0, sizeof(dnx_user_list_started));
	memset(dnx_user_table_started, 0, sizeof(dnx_user_table_started));
	dnx_user_autostart_started = 0;
	dnx_rules_n = dnx_layouts_n = dnx_monrules_n = dnx_keys_n = dnx_buttons_n = 0;
}

static int
dnx_count_user_entries_cb(const DnxEntry *e, void *userdata)
{
	size_t *count = userdata;

	if (e->source == 1)
		(*count)++;
	return 0;
}

static int
dnx_entry_cb(const DnxEntry *e, void *userdata)
{
	(void)userdata;

	if (e->type == DNX_SCALAR) {
		dnx_load_scalar(e);
		return 0;
	}

	if (e->type == DNX_LIST) {
		int ci = -1;
		char **dst = NULL;
		size_t cap = 0;
		size_t *n = NULL;

		if (!strcasecmp(e->key, "tags")) {
			if (e->source == 1 && !dnx_user_list_started[0]) {
				size_t ti;
				for (ti = 0; ti < dnx_tags_owned; ti++)
					free(tags[ti]);
				for (ti = 0; ti <= MAX_TAGS; ti++)
					tags[ti] = NULL;
				dnx_tags_owned = 0;
				dnx_tag_n = 0;
				dnx_user_list_started[0] = 1;
			}
			dnx_list_append(tags, MAX_TAGS + 1, &dnx_tag_n, e);
			if (dnx_tag_n > dnx_tags_owned)
				dnx_tags_owned = dnx_tag_n;
		} else if (!strcasecmp(e->key, "termcmd")) {
			ci = 0; dst = termcmd; cap = LENGTH(termcmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "menucmd")) {
			ci = 1; dst = menucmd; cap = LENGTH(menucmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "fmcmd")) {
			ci = 2; dst = fmcmd; cap = LENGTH(fmcmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "suspendcmd")) {
			ci = 3; dst = suspendcmd; cap = LENGTH(suspendcmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "screencmd")) {
			ci = 4; dst = screencmd; cap = LENGTH(screencmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "screen1cmd")) {
			ci = 5; dst = screen1cmd; cap = LENGTH(screen1cmd); n = &dnx_cmd_n[ci];
		} else if (!strcasecmp(e->key, "hlcmd")) {
			ci = 6; dst = hlcmd; cap = LENGTH(hlcmd); n = &dnx_cmd_n[ci];
		}

		if (dst) {
			if (e->source == 1 && !dnx_user_list_started[ci]) {
				*n = 0;
				dst[0] = NULL;
				dnx_user_list_started[ci] = 1;
			}
			dnx_list_append(dst, cap, n, e);
		} else if (dnx_collecting_commands) {
			/* Custom *name command.  It was pre-collected so key definitions
			 * can safely refer to commands appearing later in the file. */
			dnx_command_register(e);
		}
		return 0;
	}

	if (e->type != DNX_TABLE)
		return 0;

	if (!strcasecmp(e->key, "autostart") && e->count >= 1) {
		if (e->source == 1 && !dnx_user_autostart_started) {
			dnx_autostart_clear();
			dnx_user_autostart_started = 1;
		}
		dnx_autostart_append(e);
	} else if (!strcasecmp(e->key, "rules") && e->count >= 7) {
		/* dnx_rules_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[0]) {
			dnx_rules_n = 0; rules_len = 0; dnx_user_table_started[0] = 1;
		}
		if (dnx_rules_n >= rules_cap) rules = dnx_realloc_array(rules, &rules_cap, dnx_rules_n + 1, sizeof(*rules));
		{
			rules[dnx_rules_n].id = dnx_is_null(e->items[0]) ? NULL : dnx_strdup(e->items[0]);
			rules[dnx_rules_n].title = dnx_is_null(e->items[1]) ? NULL : dnx_strdup(e->items[1]);
			rules[dnx_rules_n].tags = (uint32_t)strtoul(e->items[2], NULL, 0);
			rules[dnx_rules_n].isfloating = atoi(e->items[3]);
			rules[dnx_rules_n].opacity_focus = strtof(e->items[4], NULL);
			rules[dnx_rules_n].opacity_unfocus = strtof(e->items[5], NULL);
			rules[dnx_rules_n].monitor = atoi(e->items[6]);
			rules_len = ++dnx_rules_n;
		}
	} else if (!strcasecmp(e->key, "layouts") && e->count >= 2) {
		/* dnx_layouts_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[1]) {
			dnx_layouts_n = 0;
			layouts_len = 0;
			dnx_user_table_started[1] = 1;
		}
		if (dnx_layouts_n < LENGTH(layouts)) {
			if (layout_symbol_owned[dnx_layouts_n])
				free((char *)layouts[dnx_layouts_n].symbol);
			layouts[dnx_layouts_n].symbol = dnx_strdup(e->items[0]);
			layouts[dnx_layouts_n].arrange = dnx_arrange_for_name(e->items[1]);
			layout_symbol_owned[dnx_layouts_n] = 1;
			dnx_layouts_n++;
			layouts_len = dnx_layouts_n;
		}
	} else if ((!strcasecmp(e->key, "monrules") || !strcasecmp(e->key, "monitors")) && e->count >= 8) {
		/* dnx_monrules_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[2]) {
			dnx_monrules_n = 0; monrules_len = 0; dnx_user_table_started[2] = 1;
		}
		if (dnx_monrules_n >= monrules_cap) monrules = dnx_realloc_array(monrules, &monrules_cap, dnx_monrules_n + 1, sizeof(*monrules));
		{
			memset(&monrules[dnx_monrules_n], 0, sizeof(monrules[dnx_monrules_n]));
			monrules[dnx_monrules_n].name = dnx_is_null(e->items[0]) ? NULL : dnx_strdup(e->items[0]);
			monrules[dnx_monrules_n].mfact = strtof(e->items[1], NULL);
			monrules[dnx_monrules_n].nmaster = atoi(e->items[2]);
			monrules[dnx_monrules_n].scale = strtof(e->items[3], NULL);
			/* Keep the exact value from %monrules.  %layouts may appear later
			 * in config.dnx, so resolve the layout only after the whole config
			 * has been parsed. */
			monrules[dnx_monrules_n].ltname = dnx_is_null(e->items[4]) ? NULL : dnx_strdup(e->items[4]);
			{
				int li = dnx_layout_index(e->items[4]);
				monrules[dnx_monrules_n].lt = (li >= 0) ? &layouts[li] : &layouts[0];
			}
			monrules[dnx_monrules_n].rr = dnx_transform(e->items[5]);
			monrules[dnx_monrules_n].x = atoi(e->items[6]);
			monrules[dnx_monrules_n].y = atoi(e->items[7]);
			monrules_len = ++dnx_monrules_n;
		}
	} else if (!strcasecmp(e->key, "keys") && e->count >= 3) {
		/* dnx_keys_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[3]) {
			dnx_keys_n = 0; keys_len = 0; dnx_user_table_started[3] = 1;
		}
		if (dnx_keys_n >= keys_cap) keys = dnx_realloc_array(keys, &keys_cap, dnx_keys_n + 1, sizeof(*keys));
		{
			const char *arg = e->count >= 4 ? e->items[3] : NULL;
			const char *argtype = e->count >= 5 ? e->items[4] : NULL;
			memset(&keys[dnx_keys_n], 0, sizeof(keys[dnx_keys_n]));
			keys[dnx_keys_n].mod = dnx_mods(e->items[0]);
			keys[dnx_keys_n].keysym = dnx_keysym(e->items[1]);

			if (!strcasecmp(e->items[2], "spawn")) keys[dnx_keys_n].func = spawn;
			else if (!strcasecmp(e->items[2], "view")) keys[dnx_keys_n].func = view;
			else if (!strcasecmp(e->items[2], "toggleview")) keys[dnx_keys_n].func = toggleview;
			else if (!strcasecmp(e->items[2], "tag")) keys[dnx_keys_n].func = tag;
			else if (!strcasecmp(e->items[2], "toggletag")) keys[dnx_keys_n].func = toggletag;
			else if (!strcasecmp(e->items[2], "setlayout")) keys[dnx_keys_n].func = setlayout;
			else if (!strcasecmp(e->items[2], "setmfact")) keys[dnx_keys_n].func = setmfact;
			else if (!strcasecmp(e->items[2], "incnmaster")) keys[dnx_keys_n].func = incnmaster;
			else if (!strcasecmp(e->items[2], "focusstack")) keys[dnx_keys_n].func = focusstack;
			else if (!strcasecmp(e->items[2], "focusdir")) keys[dnx_keys_n].func = focusdir;
			else if (!strcasecmp(e->items[2], "swapdir")) keys[dnx_keys_n].func = swapdir;
			else if (!strcasecmp(e->items[2], "focusmon")) keys[dnx_keys_n].func = focusmon;
			else if (!strcasecmp(e->items[2], "tagmon")) keys[dnx_keys_n].func = tagmon;
			else if (!strcasecmp(e->items[2], "cyclelayout")) keys[dnx_keys_n].func = cyclelayout;
			else if (!strcasecmp(e->items[2], "togglefloating")) keys[dnx_keys_n].func = togglefloating;
			else if (!strcasecmp(e->items[2], "togglefullscreen")) keys[dnx_keys_n].func = togglefullscreen;
			else if (!strcasecmp(e->items[2], "togglegaps")) keys[dnx_keys_n].func = togglegaps;
			else if (!strcasecmp(e->items[2], "setopacityunfocus")) keys[dnx_keys_n].func = setopacityunfocus;
			else if (!strcasecmp(e->items[2], "setopacityfocus")) keys[dnx_keys_n].func = setopacityfocus;
			else if (!strcasecmp(e->items[2], "killclient")) keys[dnx_keys_n].func = killclient;
			else if (!strcasecmp(e->items[2], "zoom")) keys[dnx_keys_n].func = zoom;
			else if (!strcasecmp(e->items[2], "quit")) keys[dnx_keys_n].func = quit;
			else if (!strcasecmp(e->items[2], "reload_config")) keys[dnx_keys_n].func = reload_config;
			else if (!strcasecmp(e->items[2], "chvt")) keys[dnx_keys_n].func = chvt;
			else if (!strcasecmp(e->items[2], "moveresize")) keys[dnx_keys_n].func = moveresize;
			else return 0;

			if (!arg || !*arg || !strcasecmp(arg, "NONE")) {
				keys[dnx_keys_n].arg.i = 0;
			} else if (!strcasecmp(e->items[2], "spawn")) {
				keys[dnx_keys_n].arg.v = dnx_command_for_name(arg);
			} else if (!strcasecmp(e->items[2], "setlayout")) {
				int li = dnx_layout_index(arg);
				keys[dnx_keys_n].arg.v = (li >= 0) ? &layouts[li] : &layouts[0];
			} else if (argtype && !strcasecmp(argtype, "float")) {
				keys[dnx_keys_n].arg.f = strtof(arg, NULL);
			} else if (argtype && (!strcasecmp(argtype, "ui") || !strcasecmp(argtype, "uint"))) {
				keys[dnx_keys_n].arg.ui = (!strcasecmp(arg, "~0") || !strcasecmp(arg, "ALL"))
					? ~0u : (uint32_t)strtoul(arg, NULL, 0);
			} else if (argtype && !strcasecmp(argtype, "int")) {
				keys[dnx_keys_n].arg.i = atoi(arg);
			} else if (!strcasecmp(e->items[2], "focusdir") ||
					!strcasecmp(e->items[2], "swapdir") ||
					!strcasecmp(e->items[2], "view") ||
					!strcasecmp(e->items[2], "toggleview") ||
					!strcasecmp(e->items[2], "tag") ||
					!strcasecmp(e->items[2], "toggletag") ||
					!strcasecmp(e->items[2], "chvt")) {
				keys[dnx_keys_n].arg.ui = (!strcasecmp(arg, "~0") || !strcasecmp(arg, "ALL"))
					? ~0u : (uint32_t)strtoul(arg, NULL, 0);
			} else if (!strcasecmp(e->items[2], "focusmon") ||
					!strcasecmp(e->items[2], "tagmon") ||
					!strcasecmp(e->items[2], "incnmaster") ||
					!strcasecmp(e->items[2], "focusstack") ||
					!strcasecmp(e->items[2], "cyclelayout") ||
					!strcasecmp(e->items[2], "moveresize")) {
				keys[dnx_keys_n].arg.i = atoi(arg);
			} else {
				char *end;
				float f = strtof(arg, &end);
				if (*end == 'f' || *end == 'F')
					keys[dnx_keys_n].arg.f = f;
				else
					keys[dnx_keys_n].arg.i = atoi(arg);
			}
			keys_len = ++dnx_keys_n;
		}
	} else if (!strcasecmp(e->key, "buttons") && e->count >= 4) {
		/* dnx_buttons_n tracks this table across parser entries. */
		if (e->source == 1 && !dnx_user_table_started[4]) {
			dnx_buttons_n = 0; buttons_len = 0; dnx_user_table_started[4] = 1;
		}
		if (dnx_buttons_n >= buttons_cap) buttons = dnx_realloc_array(buttons, &buttons_cap, dnx_buttons_n + 1, sizeof(*buttons));
		{
			memset(&buttons[dnx_buttons_n], 0, sizeof(buttons[dnx_buttons_n]));
			if (!strcasecmp(e->items[0], "ClkClient")) buttons[dnx_buttons_n].click = ClkClient;
			else if (!strcasecmp(e->items[0], "ClkRoot")) buttons[dnx_buttons_n].click = ClkRoot;
			else return 0;

			buttons[dnx_buttons_n].mod = dnx_mods(e->items[1]);
			if (!strcasecmp(e->items[2], "BTN_LEFT")) buttons[dnx_buttons_n].button = BTN_LEFT;
			else if (!strcasecmp(e->items[2], "BTN_RIGHT")) buttons[dnx_buttons_n].button = BTN_RIGHT;
			else if (!strcasecmp(e->items[2], "BTN_MIDDLE")) buttons[dnx_buttons_n].button = BTN_MIDDLE;
			else buttons[dnx_buttons_n].button = (unsigned)strtoul(e->items[2], NULL, 0);

			if (!strcasecmp(e->items[3], "spawn")) buttons[dnx_buttons_n].func = spawn;
			else if (!strcasecmp(e->items[3], "setlayout")) buttons[dnx_buttons_n].func = setlayout;
			else if (!strcasecmp(e->items[3], "zoom")) buttons[dnx_buttons_n].func = zoom;
			else if (!strcasecmp(e->items[3], "moveresize")) buttons[dnx_buttons_n].func = moveresize;
			else if (!strcasecmp(e->items[3], "togglefloating")) buttons[dnx_buttons_n].func = togglefloating;
			else if (!strcasecmp(e->items[3], "view")) buttons[dnx_buttons_n].func = view;
			else if (!strcasecmp(e->items[3], "toggleview")) buttons[dnx_buttons_n].func = toggleview;
			else if (!strcasecmp(e->items[3], "tag")) buttons[dnx_buttons_n].func = tag;
			else if (!strcasecmp(e->items[3], "toggletag")) buttons[dnx_buttons_n].func = toggletag;
			else return 0;

			if (e->count >= 5 && !strcasecmp(e->items[3], "spawn"))
				buttons[dnx_buttons_n].arg.v = dnx_command_for_name(e->items[4]);
			else if (e->count >= 5 && !strcasecmp(e->items[3], "setlayout")) {
				int li = dnx_layout_index(e->items[4]);
				buttons[dnx_buttons_n].arg.v = (li >= 0) ? &layouts[li] : &layouts[0];
			} else if (e->count >= 5) {
				buttons[dnx_buttons_n].arg.i = atoi(e->items[4]);
			}
			buttons_len = ++dnx_buttons_n;
		}
	}
	return 0;
}
static const char *
dnx_default_config_path(void)
{
	static char path[PATH_MAX];
	const char *env = getenv("SOLUX_DNX_DEFAULT");
	char exe[PATH_MAX];
	ssize_t n;

	if (env && *env && access(env, R_OK) == 0)
		return env;
	if (access(DEF_CFG, R_OK) == 0)
		return DEF_CFG;
	if (access("/etc/solux/default/config.dnx", R_OK) == 0)
		return "/etc/solux/default/config.dnx";
	if (access("/usr/local/share/solux/default/config.dnx", R_OK) == 0)
		return "/usr/local/share/solux/default/config.dnx";
	if (access("/usr/share/solux/default/config.dnx", R_OK) == 0)
		return "/usr/share/solux/default/config.dnx";

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		char *slash;
		exe[n] = '\0';
		slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			snprintf(path, sizeof(path), "%s/default/config.dnx", exe);
			if (access(path, R_OK) == 0)
				return path;
			snprintf(path, sizeof(path), "%s/../default/config.dnx", exe);
			if (access(path, R_OK) == 0)
				return path;
		}
	}

	/* Keep the canonical path in the diagnostic even when it is absent. */
	return DEF_CFG;
}



static const Key builtin_tty_keys[] = {
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F1,  chvt, { .ui = 1  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F2,  chvt, { .ui = 2  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F3,  chvt, { .ui = 3  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F4,  chvt, { .ui = 4  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F5,  chvt, { .ui = 5  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F6,  chvt, { .ui = 6  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F7,  chvt, { .ui = 7  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F8,  chvt, { .ui = 8  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F9,  chvt, { .ui = 9  } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F10, chvt, { .ui = 10 } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F11, chvt, { .ui = 11 } },
	{ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F12, chvt, { .ui = 12 } },
};

static void
dnx_add_builtin_tty_keys(void)
{
	size_t i, j;

	for (i = 0; i < LENGTH(builtin_tty_keys); i++) {
		int overridden = 0;

		/* A config.dnx entry for the same combination overrides the
		 * built-in TTY binding, regardless of which command it uses. */
		for (j = 0; j < keys_len; j++) {
			if (keys[j].mod == builtin_tty_keys[i].mod &&
				keys[j].keysym == builtin_tty_keys[i].keysym) {
				overridden = 1;
				break;
			}
		}

		if (overridden)
			continue;

		if (keys_len >= keys_cap)
			keys = dnx_realloc_array(keys, &keys_cap, keys_len + 1, sizeof(*keys));
		keys[keys_len++] = builtin_tty_keys[i];
	}
}

static void
dnx_resolve_monrule_layouts(void)
{
	size_t i;

	for (i = 0; i < monrules_len; i++) {
		if (!monrules[i].ltname)
			continue;

		{
			int li = dnx_layout_index(monrules[i].ltname);
			monrules[i].lt = (li >= 0) ? &layouts[li] : &layouts[0];
		}
	}
}

static void
load_dnx_config(void)
{
	const char *defcfg = dnx_default_config_path();
	const char *usrcfg = dnx_config_override ? dnx_config_override : USR_CFG;
	char usercfg[PATH_MAX];
	const char *home;

	/* If the normal per-user config is missing, use the system default. */
	if (!dnx_config_override) {
		home = getenv("HOME");
		if (home && *home) {
			snprintf(usercfg, sizeof(usercfg), "%s/.config/solux/config.dnx", home);
			if (access(usercfg, R_OK) == 0)
				usrcfg = usercfg;
			else
				usrcfg = SOLUX_STANDARD_CFG;
		} else {
			usrcfg = SOLUX_STANDARD_CFG;
		}
	}

	dnx_reset_tags();

	memcpy(layouts, default_layouts, sizeof(layouts));
	memset(layout_symbol_owned, 0, sizeof(layout_symbol_owned));
	layouts_len = LENGTH(default_layouts);
	dnx_reset_parser_state();
	dnx_init_dynamic_config();
	/* An explicitly supplied config that yields no DNX entries is treated as
	 * invalid.  Check it before applying anything, so the fallback does not
	 * leave partially loaded configuration behind. */
	if (dnx_config_override && strcmp(usrcfg, SOLUX_STANDARD_CFG) != 0) {
		size_t user_entries = 0;
		dnx_foreach_ff(defcfg, usrcfg, dnx_count_user_entries_cb, &user_entries);
		if (user_entries == 0) {
			fprintf(stderr,
					"dwl: configuration file '%s' could not be parsed; using standard configuration '%s'\n",
					usrcfg, SOLUX_STANDARD_CFG);
			dnx_config_override = SOLUX_STANDARD_CFG;
			usrcfg = SOLUX_STANDARD_CFG;
		}
	}

	

	dnx_collecting_commands = 1;
	dnx_foreach_ff(defcfg, usrcfg, dnx_collect_commands_cb, NULL);
	dnx_collecting_commands = 0;

	/* Second pass: apply the actual configuration. */
	int loaded = dnx_foreach_ff(defcfg, usrcfg, dnx_entry_cb, NULL);

	/* %monrules can legally appear before %layouts. Resolve its layout names
	 * now, after every %layouts entry from config.dnx has been loaded. */
	dnx_resolve_monrule_layouts();

	if (!loaded)
		fprintf(stderr, "dwl: no config.dnx found (tried %s and %s); using built-in defaults\n",
			defcfg, usrcfg);

	tagcount = dnx_tag_n > 0 ? dnx_tag_n : 9;

	/* Keep the built-in TTY shortcuts as a fallback when config.dnx does
	 * not define those key combinations. */
	dnx_add_builtin_tty_keys();
}

static void
reload_config(const Arg *arg)
{
	Monitor *m;
	DnxCommand *cmd;
	size_t i, j;
	float old_default_opacity_unfocus = default_opacity_unfocus;
	float old_default_opacity_focus = default_opacity_focus;

	(void)arg;

	

	for (i = 0; i < dnx_commands_len; i++) {
		cmd = &dnx_commands[i];
		free(cmd->name);
		for (j = 0; j < cmd->argc; j++)
			free(cmd->argv[j]);
		free(cmd->argv);
	}
	free(dnx_commands);
	dnx_commands = NULL;
	dnx_commands_len = dnx_commands_cap = 0;

	/* Autostart is configuration state; reloading must NOT execute it again. */
	dnx_autostart_clear();

	/*
	 * Recreate the dynamic tables from their built-in defaults.  The parser
	 * then applies config.dnx on top of them exactly as it does at startup.
	 */
	free(rules);
	if (monrules) {
		for (i = 0; i < monrules_len; i++)
			free(monrules[i].ltname);
	}
	free(monrules);
	free(keys);
	free(buttons);
	rules = NULL; monrules = NULL; keys = NULL; buttons = NULL;
	rules_len = rules_cap = 0;
	monrules_len = monrules_cap = 0;
	keys_len = keys_cap = 0;
	buttons_len = buttons_cap = 0;

	/* Reset XKB rules before parsing the new config. Without this, old
	 * keyboard_options may survive reload and get combined with the new one. */
	xkb_rules = (struct xkb_rule_names) {
		.layout = "us,ru",
		.options = "grp:win_space_toggle",
	};

	for (i = 0; i < LENGTH(layouts); i++) {
		if (layout_symbol_owned[i])
			free((char *)layouts[i].symbol);
	}
	memcpy(layouts, default_layouts, sizeof(layouts));
	memset(layout_symbol_owned, 0, sizeof(layout_symbol_owned));
	layouts_len = LENGTH(default_layouts);

	load_dnx_config();

	

	{
		Client *c;
		Client *focused;

		wl_list_for_each(c, &clients, link) {
			if (!client_surface(c)->mapped || !c->scene || client_is_unmanaged(c))
				continue;

			if (c->opacity_unfocus == old_default_opacity_unfocus)
				c->opacity_unfocus = default_opacity_unfocus;
			if (c->opacity_focus == old_default_opacity_focus)
				c->opacity_focus = default_opacity_focus;

			focused = c->mon ? focustop(c->mon) : NULL;
			c->opacity = (c == focused) ? c->opacity_focus : c->opacity_unfocus;
			wlr_scene_node_for_each_buffer(&c->scene_surface->node,
					scenebuffersetopacity, c);
		}
	}

	

	{
		Client *c;
		Client *focused = focustop(selmon);

		wl_list_for_each(c, &clients, link) {
			if (!client_surface(c)->mapped || !c->scene || client_is_unmanaged(c))
				continue;

			c->bw = c->isfullscreen ? 0 : borderpx;
			if (c->isfullscreen) {
				c->bws = 0;
				c->bwe = 0;
			} else if (borders_only_floating && !c->isfloating) {
				c->bws = 0;
				c->bwe = 0;
			} else {
				c->bws = borderspx;
				c->bwe = borderepx;
			}

			if (c->isurgent)
				setclientborderstate(c, BorderUrgent);
			else if (c == focused)
				setclientborderstate(c, BorderFocus);
			else
				setclientborderstate(c, BorderNormal);

			resize(c, c->geom, 0);
		}
	}

	/* Apply modkey and keyboard changes from config.dnx without restarting dwl. */
	apply_runtime_modkey();
	reload_runtime_keymap();

	/*
	 * A reload may remove or reorder %layouts.  Never leave a monitor or
	 * per-tag layout pointer referring to a layout outside the active list.
	 */
	if (!layouts_len)
		return;

	/* Apply monitor rules to all existing outputs before recalculating geometry. */
	wl_list_for_each(m, &mons, link)
		apply_monitor_rule(m);
	updatemons(NULL, NULL);

	wl_list_for_each(m, &mons, link) {
		Client *c;

		/* Apply the current config.dnx gaps setting to existing monitors. */
		m->gaps = gaps;

		/* Apply a changed tag count to already-running monitor/client state. */
		m->tagset[0] &= TAGMASK;
		m->tagset[1] &= TAGMASK;
		if (!m->tagset[0])
			m->tagset[0] = 1;
		if (!m->tagset[1])
			m->tagset[1] = 1;

		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			c->tags &= TAGMASK;
			if (!c->tags)
				c->tags = m->tagset[m->seltags];
		}

		if (m->pertag) {
			if (m->pertag->curtag > TAGCOUNT)
				m->pertag->curtag = 1;
			if (m->pertag->prevtag > TAGCOUNT)
				m->pertag->prevtag = 1;
		}

		for (i = 0; i < 2; i++) {
			if (m->lt[i] < layouts || m->lt[i] >= layouts + layouts_len)
				m->lt[i] = &layouts[0];
		}
		if (m->pertag) {
			for (i = 0; i <= TAGCOUNT; i++) {
				for (j = 0; j < 2; j++) {
					if (m->pertag->ltidxs[i][j] < layouts ||
							m->pertag->ltidxs[i][j] >= layouts + layouts_len)
						m->pertag->ltidxs[i][j] = &layouts[0];
				}
				if (m->pertag->sellts[i] > 1)
					m->pertag->sellts[i] = 0;
			}
		}
		arrange(m);
		dwl_ipc_output_printstatus(m);
		ext_workspace_printstatus(m);
	}
	fprintf(stderr, "dwl: config.dnx reloaded\n");
}

static void
apply_runtime_modkey(void)
{
	/* MODKEY is resolved at match time; never rewrite bindings here. */
}

static int
soluxctl_write_reply(int fd, const char *reply)
{
	size_t len = strlen(reply);
	ssize_t n = write(fd, reply, len);
	return n == (ssize_t)len ? 0 : -1;
}

static int
soluxctl_handle_fd(int fd, uint32_t mask, void *data)
{
	int cfd;
	char buf[4096];
	ssize_t n;
	char *end;

	(void)data;
	if (!(mask & WL_EVENT_READABLE))
		return 0;

	cfd = accept(fd, NULL, NULL);
	if (cfd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return 0;
	}

	n = read(cfd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		close(cfd);
		return 0;
	}
	buf[n] = '\0';

	/* Accept exactly one line; never execute arbitrary shell commands. */
	end = strpbrk(buf, "\r\n");
	if (end)
		*end = '\0';

	if (!strcasecmp(buf, "RECONFIG")) {
		Arg arg = {0};
		reload_config(&arg);
		soluxctl_write_reply(cfd, "OK\n");
	} else if (!strcasecmp(buf, "KBLAYOUT NEXT")) {
		Arg arg = {.i = 1};
		incxkbrules(&arg);
		soluxctl_write_reply(cfd, "OK\n");
	} else if (!strcasecmp(buf, "PING")) {
		soluxctl_write_reply(cfd, "OK\n");
	} else {
		soluxctl_write_reply(cfd, "ERR unknown command\n");
	}

	close(cfd);
	return 0;
}

static void
soluxctl_init(void)
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	struct sockaddr_un addr;
	mode_t oldmask;

	if (!runtime || !*runtime)
		runtime = "/tmp";
	if (!wayland_display || !*wayland_display)
		wayland_display = "wayland-0";

	if (snprintf(soluxctl_socket_path, sizeof(soluxctl_socket_path),
			"%s/soluxctl-%s.sock", runtime, wayland_display) >= (int)sizeof(soluxctl_socket_path))
		die("soluxctl socket path is too long");

	soluxctl_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (soluxctl_fd < 0)
		die("soluxctl: socket: %s", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", soluxctl_socket_path);

	/* Only the current user may talk to the control socket. */
	unlink(addr.sun_path);
	oldmask = umask(0077);
	if (bind(soluxctl_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		umask(oldmask);
		close(soluxctl_fd);
		soluxctl_fd = -1;
		die("soluxctl: bind %s: %s", addr.sun_path, strerror(errno));
	}
	umask(oldmask);

	if (chmod(addr.sun_path, 0600) < 0) {
		unlink(addr.sun_path);
		close(soluxctl_fd);
		soluxctl_fd = -1;
		die("soluxctl: chmod: %s", strerror(errno));
	}

	if (listen(soluxctl_fd, 16) < 0)
		die("soluxctl: listen: %s", strerror(errno));

	if (fcntl(soluxctl_fd, F_SETFL, O_NONBLOCK) < 0)
		die("soluxctl: fcntl: %s", strerror(errno));

	soluxctl_source = wl_event_loop_add_fd(event_loop, soluxctl_fd,
			WL_EVENT_READABLE, soluxctl_handle_fd, NULL);
	if (!soluxctl_source)
		die("soluxctl: wl_event_loop_add_fd failed");
}

static void
soluxctl_cleanup(void)
{
	if (soluxctl_source) {
		wl_event_source_remove(soluxctl_source);
		soluxctl_source = NULL;
	}
	if (soluxctl_fd >= 0) {
		close(soluxctl_fd);
		soluxctl_fd = -1;
	}
	if (*soluxctl_socket_path) {
		unlink(soluxctl_socket_path);
		soluxctl_socket_path[0] = '\0';
	}
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

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	dpy = wl_display_create();
	workspaces_init();
	event_loop = wl_display_get_event_loop(dpy);
	soluxctl_init();

	

	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	/* Load DNX only after the basic Wayland/wlroots objects exist.  The parser
	 * only fills configuration storage here; runtime objects are created below. */
	load_dnx_config();
	apply_runtime_modkey();

	

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	

	if (!(drw = wlr_renderer_autocreate(backend)))
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

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
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

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(dpy);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	

	wl_list_init(&clients);
	wl_list_init(&fstack);

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

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	

	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	

	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

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

	/* Make sure XWayland clients don't connect to the parent X server,
	 * e.g when running in the x11 backend or the wayland backend and the
	 * compositor has Xwayland support */
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
setxkbrules(const Arg *arg)
{
	current_kblayout = arg->i;
	assignkeymap(&kb_group->wlr_group->keyboard);
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		close(STDIN_FILENO);
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}


void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	sel->tags = arg->ui & TAGMASK;
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

void
righttile(Monitor *m)
{
	/* Keep tile() as the single source of truth for sizing, gaps and
	 * smartgaps; mirror its tiled clients horizontally afterwards. */
	tile(m);
	mirror_layout_horizontally(m);
}

/*
*/




void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
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
	arrange(selmon);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	sel->tags = newtags;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	ext_workspace_printstatus(selmon);
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	size_t i;
	if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
		return;

	if (newtagset == (uint32_t)~0) {
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = 0;
	}

	/* test if the user did not select the same tag */
	if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
		selmon->pertag->prevtag = selmon->pertag->curtag;
		for (i = 0; !(newtagset & 1 << i); i++) ;
		selmon->pertag->curtag = i + 1;
	}

	/* apply settings for this view */
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

	selmon->tagset[selmon->seltags] = newtagset;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	ext_workspace_printstatus(selmon);
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
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

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
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

	if (client_surface(c)->mapped)
		setclientborderstate(c, BorderUrgent);
}

void
view(const Arg *arg)
{
	size_t i, tmptag;

	if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & ~0) {
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
		selmon->pertag->prevtag = selmon->pertag->curtag;

		if (arg->ui == TAGMASK)
			selmon->pertag->curtag = 0;
		else {
			for (i = 0; !(arg->ui & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}
	} else {
		tmptag = selmon->pertag->prevtag;
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = tmptag;
	}

	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

	focusclient(focustop(selmon), 1);
	arrange(selmon);

	printstatus();
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
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

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	struct wlr_scene_surface *scene_surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			scene_surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (!scene_surface) continue;
			surface = scene_surface->surface;
		}
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}

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
	c->bw = client_is_unmanaged(c) ? 0 : borderpx;

	

	c->opacity_unfocus = default_opacity_unfocus;
	c->opacity_focus = default_opacity_focus;
	c->opacity = c->opacity_unfocus;

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

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-c config] [-s startup command]", argv[0]);
}
