/* Unstable wlroots interface: no API guarantees. */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_EXT_WORKSPACE_V1_H
#define WLR_TYPES_WLR_EXT_WORKSPACE_V1_H

#include <wayland-protocols/ext-workspace-v1-enum.h>
#include <wayland-server-core.h>

struct wlr_output;

enum wlr_ext_workspace_v1_request_type {
	WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE,
	WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE,
	WLR_EXT_WORKSPACE_V1_REQUEST_DEACTIVATE,
	WLR_EXT_WORKSPACE_V1_REQUEST_ASSIGN,
	WLR_EXT_WORKSPACE_V1_REQUEST_REMOVE,
};

struct wlr_ext_workspace_v1_request {
	enum wlr_ext_workspace_v1_request_type type;
	struct wl_list link;
	union {
		struct {
			char *name;
			struct wlr_ext_workspace_group_handle_v1
				*group;
		} create_workspace;
		struct {
			struct wlr_ext_workspace_handle_v1 *workspace;
		} activate;
		struct {
			struct wlr_ext_workspace_handle_v1 *workspace;
		} deactivate;
		struct {
			struct wlr_ext_workspace_handle_v1 *workspace;
			struct wlr_ext_workspace_group_handle_v1
				*group;
		} assign;
		struct {
			struct wlr_ext_workspace_handle_v1 *workspace;
		} remove;
	};
};

struct wlr_ext_workspace_v1_commit_event {
	struct wl_list *requests;
};

struct wlr_ext_workspace_manager_v1 {
	struct wl_global *global;
	struct wl_list groups;
	struct wl_list workspaces;

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
	} events;

	void *data;

	struct {
		struct wl_list resources;
		struct wl_event_source *idle_source;
		struct wl_event_loop *event_loop;
		struct wl_listener display_destroy;
	};
};

struct wlr_ext_workspace_group_handle_v1 {
	struct wlr_ext_workspace_manager_v1 *manager;
	uint32_t caps;
	struct {
		struct wl_signal destroy;
	} events;

	struct wl_list link;

	void *data;

	struct {
		struct wl_list outputs;
		struct wl_list resources;
	};
};

struct wlr_ext_workspace_handle_v1 {
	struct wlr_ext_workspace_manager_v1 *manager;
	struct wlr_ext_workspace_group_handle_v1 *group;
	char *id;
	char *name;
	struct wl_array coordinates;
	uint32_t caps;
	uint32_t state;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_list link;

	void *data;

	struct {
		struct wl_list resources;
	};
};

struct wlr_ext_workspace_manager_v1 *
wlr_ext_workspace_manager_v1_create(struct wl_display *display,
									uint32_t version);

struct wlr_ext_workspace_group_handle_v1 *
wlr_ext_workspace_group_handle_v1_create(
	struct wlr_ext_workspace_manager_v1 *manager, uint32_t caps);
void wlr_ext_workspace_group_handle_v1_destroy(
	struct wlr_ext_workspace_group_handle_v1 *group);

void wlr_ext_workspace_group_handle_v1_output_enter(
	struct wlr_ext_workspace_group_handle_v1 *group, struct wlr_output *output);
void wlr_ext_workspace_group_handle_v1_output_leave(
	struct wlr_ext_workspace_group_handle_v1 *group, struct wlr_output *output);

struct wlr_ext_workspace_handle_v1 *
wlr_ext_workspace_handle_v1_create(struct wlr_ext_workspace_manager_v1 *manager,
								   const char *id, uint32_t caps);
void wlr_ext_workspace_handle_v1_destroy(
	struct wlr_ext_workspace_handle_v1 *workspace);

void wlr_ext_workspace_handle_v1_set_group(
	struct wlr_ext_workspace_handle_v1 *workspace,
	struct wlr_ext_workspace_group_handle_v1 *group);
void wlr_ext_workspace_handle_v1_set_name(
	struct wlr_ext_workspace_handle_v1 *workspace, const char *name);
void wlr_ext_workspace_handle_v1_set_coordinates(
	struct wlr_ext_workspace_handle_v1 *workspace, const uint32_t *coords,
	size_t coords_len);
void wlr_ext_workspace_handle_v1_set_active(
	struct wlr_ext_workspace_handle_v1 *workspace, bool enabled);
void wlr_ext_workspace_handle_v1_set_urgent(
	struct wlr_ext_workspace_handle_v1 *workspace, bool enabled);
void wlr_ext_workspace_handle_v1_set_hidden(
	struct wlr_ext_workspace_handle_v1 *workspace, bool enabled);

#endif
