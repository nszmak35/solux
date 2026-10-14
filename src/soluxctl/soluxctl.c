#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>

#define KBLAYOUT_FILE "/tmp/dwl-keymap"

#include "dwl-ipc-unstable-v2-client-protocol.h"

#define POLL_MS 250
#define INITIAL_CAP 8

enum mode {
    MODE_NONE,
    MODE_WORKSPACE,
    MODE_LAYOUT,
    MODE_FOCUS,
    MODE_MONITOR,
    MODE_ALL
};

struct taginfo {
    uint32_t state;
    uint32_t clients;
    uint32_t focused;
};

struct monitor;

struct state {
    char **tags;
    size_t ntags;
    size_t tagcap;

    char **layouts;
    size_t nlayouts;
    size_t layoutcap;

    struct monitor **monitors;
    size_t nmonitors;
    size_t monitorcap;

    uint32_t tag_amount;
    char *kblayout;
    enum mode mode;
    const char *workspace;
    int dirty;
};

struct monitor {
    struct state *state;
    struct wl_output *wl_output;
    struct zdwl_ipc_output_v2 *ipc_output;
    uint32_t registry_name;

    struct taginfo *tags;
    size_t ntags;

    int active;
    uint32_t layout_index;
    char *layout_symbol;
    char *title;
    char *appid;
    uint32_t fullscreen;
    uint32_t floating;
    uint32_t visibility;
};

struct ipc {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zdwl_ipc_manager_v2 *manager;
    struct state *state;
};

static const char *progname;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) die("out of memory");
    return q;
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s ? s : "");
    if (!p) die("out of memory");
    return p;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

static char *socket_path(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *display = getenv("WAYLAND_DISPLAY");
    static char path[PATH_MAX];

    if (!runtime || !*runtime)
        runtime = "/tmp";
    if (!display || !*display)
        display = "wayland-0";

    snprintf(path, sizeof(path), "%s/soluxctl-%s.sock", runtime, display);
    return xstrdup(path);
}

static void json_string(const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");

    putchar('"');
    while (*p) {
        switch (*p) {
        case '\\': fputs("\\\\", stdout); break;
        case '"':  fputs("\\\"", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*p < 0x20)
                printf("\\u%04x", (unsigned)*p);
            else
                putchar(*p);
        }
        p++;
    }
    putchar('"');
}

static void state_string(char **dst, const char *value)
{
    free(*dst);
    *dst = xstrdup(value ? value : "");
}

static void monitor_free(struct monitor *m)
{
    if (!m)
        return;
    free(m->tags);
    free(m->layout_symbol);
    free(m->title);
    free(m->appid);
    free(m);
}

static void state_free(struct state *s)
{
    size_t i;

    for (i = 0; i < s->ntags; i++)
        free(s->tags[i]);
    free(s->tags);

    for (i = 0; i < s->nlayouts; i++)
        free(s->layouts[i]);
    free(s->layouts);

    for (i = 0; i < s->nmonitors; i++)
        monitor_free(s->monitors[i]);
    free(s->monitors);

    free(s->kblayout);
    memset(s, 0, sizeof(*s));
}

static void add_tag_name(struct state *s, const char *name)
{
    if (s->ntags == s->tagcap) {
        s->tagcap = s->tagcap ? s->tagcap * 2 : INITIAL_CAP;
        s->tags = xrealloc(s->tags, s->tagcap * sizeof(*s->tags));
    }
    s->tags[s->ntags++] = xstrdup(name);
}

static void add_layout_name(struct state *s, const char *name)
{
    if (s->nlayouts == s->layoutcap) {
        s->layoutcap = s->layoutcap ? s->layoutcap * 2 : INITIAL_CAP;
        s->layouts = xrealloc(s->layouts, s->layoutcap * sizeof(*s->layouts));
    }
    s->layouts[s->nlayouts++] = xstrdup(name);
}

static struct monitor *add_monitor(struct state *s, struct wl_output *output,
                                   uint32_t registry_name)
{
    struct monitor *m;

    if (s->nmonitors == s->monitorcap) {
        s->monitorcap = s->monitorcap ? s->monitorcap * 2 : INITIAL_CAP;
        s->monitors = xrealloc(s->monitors,
                               s->monitorcap * sizeof(*s->monitors));
    }

    m = calloc(1, sizeof(*m));
    if (!m)
        die("out of memory");

    m->state = s;
    m->wl_output = output;
    m->registry_name = registry_name;
    m->layout_index = UINT32_MAX;
    s->monitors[s->nmonitors++] = m;
    return m;
}

static struct monitor *find_monitor_by_ipc(struct state *s,
                                            struct zdwl_ipc_output_v2 *output)
{
    size_t i;
    for (i = 0; i < s->nmonitors; i++)
        if (s->monitors[i]->ipc_output == output)
            return s->monitors[i];
    return NULL;
}

static struct monitor *active_monitor(const struct state *s)
{
    size_t i;

    for (i = 0; i < s->nmonitors; i++)
        if (s->monitors[i]->active)
            return s->monitors[i];

    return s->nmonitors ? s->monitors[0] : NULL;
}

static void read_kblayout(struct state *s)
{
    char buf[1024];
    FILE *f;

    f = fopen(KBLAYOUT_FILE, "r");
    if (f && fgets(buf, sizeof(buf), f))
        state_string(&s->kblayout, trim(buf));
    else
        state_string(&s->kblayout, "");

    if (f)
        fclose(f);
}

static int control_request(const char *command, char *out, size_t outsz,
                           char *err, size_t errsz)
{
    struct sockaddr_un addr;
    struct timeval tv;
    char buf[4096];
    size_t len = 0;
    char *path;
    char *body;
    ssize_t n;
    int fd;

    if (out && outsz)
        out[0] = '\0';

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(err, errsz, "cannot create control socket: %s", strerror(errno));
        return -1;
    }

    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    path = socket_path();
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path) >=
        (int)sizeof(addr.sun_path)) {
        free(path);
        close(fd);
        snprintf(err, errsz, "control socket path is too long");
        return -1;
    }
    free(path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(err, errsz, "cannot connect to solux: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (dprintf(fd, "%s\n", command) < 0) {
        snprintf(err, errsz, "cannot send command: %s", strerror(errno));
        close(fd);
        return -1;
    }

    while (len < sizeof(buf) - 1) {
        n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    close(fd);

    if (!len) {
        snprintf(err, errsz, "solux closed the control connection");
        return -1;
    }
    buf[len] = '\0';

    if (strncmp(buf, "OK", 2) != 0) {
        snprintf(err, errsz, "solux: %s", trim(buf));
        return -1;
    }

    body = strchr(buf, '\n');
    if (out && outsz) {
        if (body)
            snprintf(out, outsz, "%s", body + 1);
        else
            out[0] = '\0';
    }
    return 0;
}

static int control_command(const char *command)
{
    char err[512];

    if (control_request(command, NULL, 0, err, sizeof(err)) < 0)
        die("%s", err);
    return 0;
}

static void load_tag_names(struct state *s)
{
    char out[4096];
    char err[256];
    char *line;
    char *save = NULL;
    size_t i = 0;

    if (control_request("TAGS", out, sizeof(out), err, sizeof(err)) < 0)
        return;

    for (line = strtok_r(out, "\n", &save); line && i < s->ntags;
         line = strtok_r(NULL, "\n", &save), i++) {
        if (!*line)
            continue;
        free(s->tags[i]);
        s->tags[i] = xstrdup(line);
    }
}

static void monitor_json(const struct monitor *m, const struct state *s)
{
    size_t i;
    int first;

    putchar('{');
    fputs("\"id\":", stdout);
    printf("%u", m->registry_name);
    fputs(",\"active\":", stdout);
    fputs(m->active ? "true" : "false", stdout);

    fputs(",\"layout\":", stdout);
    json_string(m->layout_symbol);

    fputs(",\"layout_index\":", stdout);
    if (m->layout_index == UINT32_MAX)
        fputs("null", stdout);
    else
        printf("%u", m->layout_index);

    fputs(",\"fullscreen\":", stdout);
    fputs(m->fullscreen ? "true" : "false", stdout);
    fputs(",\"floating\":", stdout);
    fputs(m->floating ? "true" : "false", stdout);
    fputs(",\"visibility\":", stdout);
    fputs(m->visibility ? "true" : "false", stdout);

    fputs(",\"workspaces\":[", stdout);
    first = 1;
    for (i = 0; i < s->ntags && i < m->ntags; i++) {
        const struct taginfo *t = &m->tags[i];
        if (!first) putchar(',');
        first = 0;
        putchar('{');
        fputs("\"name\":", stdout);
        json_string(s->tags[i]);
        fputs(",\"active\":", stdout);
        fputs((t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE) ? "true" : "false", stdout);
        fputs(",\"urgent\":", stdout);
        fputs((t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT) ? "true" : "false", stdout);
        fputs(",\"windows\":", stdout);
        printf("%u", t->clients);
        fputs(",\"focused\":", stdout);
        fputs(t->focused ? "true" : "false", stdout);
        putchar('}');
    }
    putchar(']');

    fputs(",\"focus\":", stdout);
    if (m->title && *m->title) {
        putchar('{');
        fputs("\"title\":", stdout);
        json_string(m->title);
        fputs(",\"appid\":", stdout);
        json_string(m->appid);
        putchar('}');
    } else {
        fputs("null", stdout);
    }

    putchar('}');
}

static void print_workspace_watch(const struct monitor *m, const struct state *s,
                                  const char *workspace)
{
    size_t i;

    for (i = 0; i < s->ntags && i < m->ntags; i++) {
        if (!strcmp(s->tags[i], workspace)) {
            const struct taginfo *t = &m->tags[i];

            putchar('{');
            fputs("\"workspace\":", stdout);
            json_string(workspace);
            fputs(",\"active\":", stdout);
            fputs((t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE) ? "true" : "false", stdout);
            fputs(",\"urgent\":", stdout);
            fputs((t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT) ? "true" : "false", stdout);
            fputs(",\"windows\":", stdout);
            printf("%u", t->clients);
            fputs(",\"focused_window\":", stdout);

            if (t->focused && m->title && *m->title) {
                putchar('{');
                fputs("\"title\":", stdout);
                json_string(m->title);
                fputs(",\"appid\":", stdout);
                json_string(m->appid);
                putchar('}');
            } else {
                fputs("null", stdout);
            }

            putchar('}');
            putchar('\n');
            fflush(stdout);
            return;
        }
    }

    die("workspace '%s' does not exist", workspace);
}

static void print_all(const struct state *s)
{
    size_t i;
    struct monitor *active = active_monitor(s);

    putchar('{');
    fputs("\"keyboard_layout\":", stdout);
    json_string(s->kblayout);

    fputs(",\"monitor\":", stdout);
    if (active)
        monitor_json(active, s);
    else
        fputs("null", stdout);

    fputs(",\"monitors\":[", stdout);
    for (i = 0; i < s->nmonitors; i++) {
        if (i) putchar(',');
        monitor_json(s->monitors[i], s);
    }
    fputs("]", stdout);

    putchar('}');
    putchar('\n');
    fflush(stdout);
}

static void emit_mode(struct state *s)
{
    struct monitor *m = active_monitor(s);

    if (!m)
        return;

    switch (s->mode) {
    case MODE_LAYOUT:
        puts(m->layout_symbol ? m->layout_symbol : "");
        fflush(stdout);
        break;

    case MODE_FOCUS:
        putchar('{');
        fputs("\"title\":", stdout);
        json_string(m->title);
        fputs(",\"appid\":", stdout);
        json_string(m->appid);
        fputs(",\"fullscreen\":", stdout);
        fputs(m->fullscreen ? "true" : "false", stdout);
        fputs(",\"floating\":", stdout);
        fputs(m->floating ? "true" : "false", stdout);
        puts("}");
        fflush(stdout);
        break;

    case MODE_MONITOR:
        monitor_json(m, s);
        putchar('\n');
        fflush(stdout);
        break;

    case MODE_ALL:
        print_all(s);
        break;

    case MODE_WORKSPACE:
        if (s->workspace)
            print_workspace_watch(m, s, s->workspace);
        break;

    default:
        break;
    }
}

static void output_active(void *data, struct zdwl_ipc_output_v2 *output,
                          uint32_t active)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        m->active = active != 0;
}

static void output_tag(void *data, struct zdwl_ipc_output_v2 *output,
                       uint32_t tag, uint32_t state, uint32_t clients,
                       uint32_t focused)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);

    if (!m || tag >= m->ntags)
        return;

    m->tags[tag].state = state;
    m->tags[tag].clients = clients;
    m->tags[tag].focused = focused;
}

static void output_layout(void *data, struct zdwl_ipc_output_v2 *output,
                          uint32_t layout)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);

    if (!m)
        return;

    m->layout_index = layout;
    if (layout < s->nlayouts)
        state_string(&m->layout_symbol, s->layouts[layout]);
}

static void output_title(void *data, struct zdwl_ipc_output_v2 *output,
                         const char *title)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        state_string(&m->title, title);
}

static void output_appid(void *data, struct zdwl_ipc_output_v2 *output,
                         const char *appid)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        state_string(&m->appid, appid);
}

static void output_layout_symbol(void *data,
                                  struct zdwl_ipc_output_v2 *output,
                                  const char *layout)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        state_string(&m->layout_symbol, layout);
}

static void output_fullscreen(void *data, struct zdwl_ipc_output_v2 *output,
                              uint32_t value)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        m->fullscreen = value;
}

static void output_floating(void *data, struct zdwl_ipc_output_v2 *output,
                            uint32_t value)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        m->floating = value;
}

static void output_visibility(void *data,
                              struct zdwl_ipc_output_v2 *output)
{
    struct state *s = data;
    struct monitor *m = find_monitor_by_ipc(s, output);
    if (m)
        m->visibility = !m->visibility;
}

static void output_frame(void *data, struct zdwl_ipc_output_v2 *output)
{
    struct state *s = data;
    (void)output;
    s->dirty = 1;
}

static const struct zdwl_ipc_output_v2_listener output_listener = {
    .toggle_visibility = output_visibility,
    .active = output_active,
    .tag = output_tag,
    .layout = output_layout,
    .title = output_title,
    .appid = output_appid,
    .layout_symbol = output_layout_symbol,
    .frame = output_frame,
    .fullscreen = output_fullscreen,
    .floating = output_floating,
};

static void manager_tags(void *data, struct zdwl_ipc_manager_v2 *manager,
                         uint32_t amount)
{
    struct state *s = data;
    size_t i;

    (void)manager;

    s->tag_amount = amount;

    for (i = 0; i < s->nmonitors; i++) {
        free(s->monitors[i]->tags);
        s->monitors[i]->tags = calloc(amount, sizeof(struct taginfo));
        if (!s->monitors[i]->tags)
            die("out of memory");
        s->monitors[i]->ntags = amount;
    }

    while (s->ntags < amount) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", s->ntags + 1);
        add_tag_name(s, buf);
    }
}

static void manager_layout(void *data, struct zdwl_ipc_manager_v2 *manager,
                           const char *name)
{
    struct state *s = data;
    (void)manager;

    add_layout_name(s, name);
}

static const struct zdwl_ipc_manager_v2_listener manager_listener = {
    .tags = manager_tags,
    .layout = manager_layout,
};

static void bind_monitor(struct ipc *i, struct monitor *m)
{
    m->ipc_output = zdwl_ipc_manager_v2_get_output(i->manager, m->wl_output);
    if (!m->ipc_output)
        die("cannot create IPC output object");
    zdwl_ipc_output_v2_add_listener(m->ipc_output, &output_listener, i->state);
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct ipc *i = data;

    if (!strcmp(interface, zdwl_ipc_manager_v2_interface.name)) {
        if (!i->manager)
            i->manager = wl_registry_bind(registry, name,
                                          &zdwl_ipc_manager_v2_interface,
                                          version < 2 ? version : 2);
        return;
    }

    if (!strcmp(interface, "wl_output")) {
        uint32_t v = version < 4 ? version : 4;
        struct wl_output *output =
            wl_registry_bind(registry, name, &wl_output_interface, v);
        struct monitor *m = add_monitor(i->state, output, name);

        if (i->manager && i->state->tag_amount) {
            m->tags = calloc(i->state->tag_amount, sizeof(struct taginfo));
            if (!m->tags)
                die("out of memory");
            m->ntags = i->state->tag_amount;
            bind_monitor(i, m);
        }
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    struct ipc *i = data;
    size_t n;

    (void)registry;

    for (n = 0; n < i->state->nmonitors; n++) {
        if (i->state->monitors[n]->registry_name == name) {
            monitor_free(i->state->monitors[n]);
            memmove(&i->state->monitors[n],
                    &i->state->monitors[n + 1],
                    (i->state->nmonitors - n - 1) *
                    sizeof(*i->state->monitors));
            i->state->nmonitors--;
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int ipc_connect(struct ipc *i, struct state *s)
{
    size_t n;

    memset(i, 0, sizeof(*i));
    i->state = s;

    i->display = wl_display_connect(NULL);
    if (!i->display)
        return -1;

    i->registry = wl_display_get_registry(i->display);
    wl_registry_add_listener(i->registry, &registry_listener, i);

    if (wl_display_roundtrip(i->display) < 0)
        return -1;

    if (!i->manager)
        return -1;

    zdwl_ipc_manager_v2_add_listener(i->manager, &manager_listener, s);

    if (wl_display_roundtrip(i->display) < 0)
        return -1;

    for (n = 0; n < s->nmonitors; n++)
        bind_monitor(i, s->monitors[n]);

    if (wl_display_roundtrip(i->display) < 0)
        return -1;

    load_tag_names(s);

    return 0;
}

static void ipc_disconnect(struct ipc *i)
{
    if (i->display)
        wl_display_disconnect(i->display);
    memset(i, 0, sizeof(*i));
}

static int tag_index(struct state *s, const char *name)
{
    size_t i;

    for (i = 0; i < s->ntags; i++)
        if (!strcmp(s->tags[i], name))
            return (int)i;

    return -1;
}

static void set_workspace(const char *name)
{
    struct state s = {0};
    struct ipc i;
    struct monitor *m;
    int index;

    if (ipc_connect(&i, &s) < 0)
        die("dwl IPC is unavailable");

    index = tag_index(&s, name);
    if (index < 0)
        die("workspace '%s' does not exist", name);

    if ((unsigned)index >= 32)
        die("workspace '%s' cannot be selected: tag index %d exceeds IPC tag limit",
            name, index);

    m = active_monitor(&s);
    if (!m || !m->ipc_output)
        die("no active monitor");

    /* Select exactly this tag/workspace on the active monitor. */
    zdwl_ipc_output_v2_set_tags(m->ipc_output, 1u << index, 0);

    if (wl_display_flush(i.display) < 0 && errno != EAGAIN)
        die("cannot send workspace request: %s", strerror(errno));

    wl_display_roundtrip(i.display);
    ipc_disconnect(&i);
    state_free(&s);
}

static void set_layout(const char *name)
{
    char cmd[4200];

    if (name && *name)
        snprintf(cmd, sizeof(cmd), "SET-LAYOUT %s", name);
    else
        snprintf(cmd, sizeof(cmd), "SET-LAYOUT");

    control_command(cmd);
}

static void set_kblayout(void)
{
    control_command("KBLAYOUT NEXT");
}

static void watch_ipc(enum mode mode, const char *workspace)
{
    struct state s = {0};
    struct ipc i;
    int fd;

    s.mode = mode;
    s.workspace = workspace;

    if (ipc_connect(&i, &s) < 0)
        die("dwl IPC is unavailable");

    read_kblayout(&s);

    fd = wl_display_get_fd(i.display);

    /* Initial snapshot. */
    s.dirty = 0;
    wl_display_roundtrip(i.display);
    emit_mode(&s);
    s.dirty = 0;

    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int r = poll(&pfd, 1, POLL_MS);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            die("poll: %s", strerror(errno));
        }

        if (r > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP))) {
            if (wl_display_dispatch(i.display) < 0)
                break;
        }

        if (mode == MODE_ALL) {
            char *old = s.kblayout ? xstrdup(s.kblayout) : xstrdup("");
            read_kblayout(&s);
            if (strcmp(old, s.kblayout ? s.kblayout : ""))
                s.dirty = 1;
            free(old);
        }

        if (s.dirty) {
            emit_mode(&s);
            s.dirty = 0;
        }
    }

    state_free(&s);
    ipc_disconnect(&i);
}

static void watch_kblayout(void)
{
    char last[1024] = "";
    char buf[1024];

    for (;;) {
        FILE *f = fopen(KBLAYOUT_FILE, "r");

        if (f && fgets(buf, sizeof(buf), f)) {
            char *v = trim(buf);
            if (strcmp(v, last)) {
                snprintf(last, sizeof(last), "%s", v);
                puts(v);
                fflush(stdout);
            }
        }

        if (f)
            fclose(f);

        usleep(POLL_MS * 1000);
    }
}

static void usage(void)
{
    puts("soluxctl - inspect and control Solux");
    puts("");
    puts("Usage:");
    puts("  soluxctl help");
    puts("  soluxctl reconfig");
    puts("  soluxctl set-workspace <name_tag>");
    puts("  soluxctl set-layout [layout]   (no argument: next layout from the config)");
    puts("  soluxctl set-kblayout          (next keyboard layout, e.g. us -> ru -> us)");
    puts("  soluxctl watch-layout");
    puts("  soluxctl watch-kblayout");
    puts("  soluxctl watch-monitor");
    puts("  soluxctl watch-workspace <name_tag>");
    puts("  soluxctl watch-focus");
    puts("  soluxctl watch-all");
    puts("");

}

int main(int argc, char **argv)
{
    const char *cmd;

    progname = argv[0];

    if (argc < 2) {
        usage();
        return 1;
    }

    cmd = argv[1];

    if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") ||
        !strcmp(cmd, "--help")) {
        usage();
        return 0;
    }

    if (!strcmp(cmd, "reconfig")) {
        control_command("RECONFIG");
        return 0;
    }

    if (!strcmp(cmd, "set-workspace")) {
        if (argc != 3)
            die("usage: soluxctl set-workspace <name_tag>");
        if (!argv[2][0])
            die("workspace name is empty");
        set_workspace(argv[2]);
        return 0;
    }

    if (!strcmp(cmd, "set-kblayout")) {
        if (argc != 2)
            die("usage: soluxctl set-kblayout");
        set_kblayout();
        return 0;
    }

    if (!strcmp(cmd, "watch-kblayout")) {
        watch_kblayout();
        return 0;
    }

    if (!strcmp(cmd, "watch-layout")) {
        watch_ipc(MODE_LAYOUT, NULL);
        return 0;
    }

    if (!strcmp(cmd, "set-layout")) {
        char value[4096] = "";
        char *v;
        size_t i;

        if (argc == 2) {
            set_layout(NULL);
            return 0;
        }

        for (i = 2; i < (size_t)argc; i++) {
            if (i > 2)
                strncat(value, " ", sizeof(value) - strlen(value) - 1);
            strncat(value, argv[i], sizeof(value) - strlen(value) - 1);
        }

        if (strpbrk(value, "\r\n"))
            die("layout name must not contain a newline");

        v = trim(value);
        if (!*v)
            die("layout name is empty");

        set_layout(v);
        return 0;
    }

    if (!strcmp(cmd, "watch-monitor")) {
        watch_ipc(MODE_MONITOR, NULL);
        return 0;
    }

    if (!strcmp(cmd, "watch-focus")) {
        watch_ipc(MODE_FOCUS, NULL);
        return 0;
    }

    if (!strcmp(cmd, "watch-all")) {
        watch_ipc(MODE_ALL, NULL);
        return 0;
    }

    if (!strcmp(cmd, "watch-workspace")) {
        if (argc != 3)
            die("usage: soluxctl watch-workspace <name_tag>");
        watch_ipc(MODE_WORKSPACE, argv[2]);
        return 0;
    }

    die("unknown command '%s' (try 'soluxctl help')", cmd);
    return 1;
}
