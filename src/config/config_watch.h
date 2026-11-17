/* Automatic config reload: watches config.py with inotify and calls
 * reload_config() right after the file has been saved.
 *
 * The directory containing the file is watched (not the file itself), so
 * editors that save through a temporary file + rename (vim, nano, kate, ...)
 * are noticed too. Symbolic links are followed by hand (no realpath(), which
 * is not available with -D_POSIX_C_SOURCE=200809L). */
#ifndef SOLUX_CONFIG_CONFIG_WATCH_H
#define SOLUX_CONFIG_CONFIG_WATCH_H

#include <string.h>
#include <sys/inotify.h>

#define SOLUX_CFGWATCH_MAX 16
#define SOLUX_CFGWATCH_DELAY_MS 120
#define SOLUX_CFGWATCH_LINK_DEPTH 8

typedef struct {
	int wd;
	char name[NAME_MAX + 1];
} SoluxCfgWatchEntry;

static int solux_cfgwatch_fd = -1;
static struct wl_event_source *solux_cfgwatch_fd_source;
static struct wl_event_source *solux_cfgwatch_timer;
static SoluxCfgWatchEntry solux_cfgwatch_entries[SOLUX_CFGWATCH_MAX];
static int solux_cfgwatch_n;

static void solux_cfgwatch_rebuild(void);

/* Splits path into directory (dir) and file name (name). */
static int
solux_cfgwatch_split(const char *path, char *dir, size_t dirsz, char *name, size_t namesz)
{
	const char *slash;
	size_t dlen, nlen;

	if (!path || !*path)
		return 0;

	slash = strrchr(path, '/');
	if (!slash) {
		if (dirsz < 2 || strlen(path) >= namesz)
			return 0;
		strcpy(dir, ".");
		strcpy(name, path);
		return 1;
	}

	dlen = (size_t)(slash - path);
	nlen = strlen(slash + 1);
	if (nlen == 0 || nlen >= namesz)
		return 0;
	if (dlen == 0) {
		if (dirsz < 2)
			return 0;
		strcpy(dir, "/");
	} else {
		if (dlen >= dirsz)
			return 0;
		memcpy(dir, path, dlen);
		dir[dlen] = '\0';
	}
	strcpy(name, slash + 1);
	return 1;
}

static void
solux_cfgwatch_add(const char *path)
{
	char dir[PATH_MAX], name[NAME_MAX + 1];
	int wd, i;

	if (solux_cfgwatch_fd < 0 || solux_cfgwatch_n >= SOLUX_CFGWATCH_MAX)
		return;
	if (!solux_cfgwatch_split(path, dir, sizeof(dir), name, sizeof(name)))
		return;

	/* Watching the same directory twice returns the same descriptor. */
	wd = inotify_add_watch(solux_cfgwatch_fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO);
	if (wd < 0)
		return;

	for (i = 0; i < solux_cfgwatch_n; i++)
		if (solux_cfgwatch_entries[i].wd == wd &&
		    !strcmp(solux_cfgwatch_entries[i].name, name))
			return;

	solux_cfgwatch_entries[solux_cfgwatch_n].wd = wd;
	strcpy(solux_cfgwatch_entries[solux_cfgwatch_n].name, name);
	solux_cfgwatch_n++;
}

/* Watches path and, if it is a symbolic link, every file it points to. */
static void
solux_cfgwatch_add_follow(const char *path)
{
	char cur[PATH_MAX], target[PATH_MAX], dir[PATH_MAX], name[NAME_MAX + 1];
	int depth;

	if (!path || !*path || strlen(path) >= sizeof(cur))
		return;
	strcpy(cur, path);

	for (depth = 0; depth < SOLUX_CFGWATCH_LINK_DEPTH; depth++) {
		struct stat st;
		ssize_t n;

		solux_cfgwatch_add(cur);

		if (lstat(cur, &st) != 0 || !S_ISLNK(st.st_mode))
			return;
		n = readlink(cur, target, sizeof(target) - 1);
		if (n <= 0)
			return;
		target[n] = '\0';

		if (target[0] == '/') {
			strcpy(cur, target);
		} else {
			char joined[PATH_MAX];

			if (!solux_cfgwatch_split(cur, dir, sizeof(dir), name, sizeof(name)))
				return;
			if (snprintf(joined, sizeof(joined), "%s/%s", dir, target) >= (int)sizeof(joined))
				return;
			strcpy(cur, joined);
		}
	}
}

static void
solux_cfgwatch_clear(void)
{
	int i;

	for (i = 0; i < solux_cfgwatch_n; i++)
		inotify_rm_watch(solux_cfgwatch_fd, solux_cfgwatch_entries[i].wd);
	solux_cfgwatch_n = 0;
}

static void
solux_cfgwatch_rebuild(void)
{
	char usercfg[PATH_MAX];
	const char *home;

	if (solux_cfgwatch_fd < 0)
		return;

	solux_cfgwatch_clear();

	solux_cfgwatch_add_follow(dnx_default_config_path());

	if (dnx_config_override) {
		solux_cfgwatch_add_follow(dnx_config_override);
		return;
	}

	home = getenv("HOME");
	if (home && *home &&
	    snprintf(usercfg, sizeof(usercfg), "%s/.config/solux/config.py", home) < (int)sizeof(usercfg))
		solux_cfgwatch_add_follow(usercfg);
	solux_cfgwatch_add_follow(SOLUX_STANDARD_CFG);
}

static int
solux_cfgwatch_timer_cb(void *data)
{
	Arg arg = {0};

	(void)data;
	reload_config(&arg);
	/* The file may now be a different symlink target, or the user config may
	 * have just appeared; refresh what is being watched. */
	solux_cfgwatch_rebuild();
	return 0;
}

static int
solux_cfgwatch_fd_cb(int fd, uint32_t mask, void *data)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	int changed = 0;

	(void)data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
		return 0;

	for (;;) {
		ssize_t len = read(fd, buf, sizeof(buf));
		char *p;

		if (len <= 0)
			break;

		for (p = buf; p < buf + len; ) {
			const struct inotify_event *ev = (const struct inotify_event *)p;
			int i;

			p += sizeof(struct inotify_event) + ev->len;
			if (!ev->len || !(ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)))
				continue;
			for (i = 0; i < solux_cfgwatch_n; i++) {
				if (solux_cfgwatch_entries[i].wd == ev->wd &&
				    !strcmp(solux_cfgwatch_entries[i].name, ev->name)) {
					changed = 1;
					break;
				}
			}
		}
	}

	/* Debounce: several events for one save collapse into a single reload. */
	if (changed && solux_cfgwatch_timer)
		wl_event_source_timer_update(solux_cfgwatch_timer, SOLUX_CFGWATCH_DELAY_MS);
	return 0;
}

static void
solux_cfgwatch_init(void)
{
	if (!event_loop || solux_cfgwatch_fd >= 0)
		return;

	solux_cfgwatch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (solux_cfgwatch_fd < 0) {
		fprintf(stderr, "solux: config auto-reload disabled (inotify_init1 failed)\n");
		return;
	}

	solux_cfgwatch_timer = wl_event_loop_add_timer(event_loop, solux_cfgwatch_timer_cb, NULL);
	solux_cfgwatch_fd_source = wl_event_loop_add_fd(event_loop, solux_cfgwatch_fd,
		WL_EVENT_READABLE, solux_cfgwatch_fd_cb, NULL);
	if (!solux_cfgwatch_timer || !solux_cfgwatch_fd_source) {
		fprintf(stderr, "solux: config auto-reload disabled (event loop error)\n");
		if (solux_cfgwatch_timer)
			wl_event_source_remove(solux_cfgwatch_timer);
		if (solux_cfgwatch_fd_source)
			wl_event_source_remove(solux_cfgwatch_fd_source);
		solux_cfgwatch_timer = NULL;
		solux_cfgwatch_fd_source = NULL;
		close(solux_cfgwatch_fd);
		solux_cfgwatch_fd = -1;
		return;
	}

	solux_cfgwatch_rebuild();
}

static void
solux_cfgwatch_cleanup(void)
{
	if (solux_cfgwatch_timer)
		wl_event_source_remove(solux_cfgwatch_timer);
	if (solux_cfgwatch_fd_source)
		wl_event_source_remove(solux_cfgwatch_fd_source);
	solux_cfgwatch_timer = NULL;
	solux_cfgwatch_fd_source = NULL;
	if (solux_cfgwatch_fd >= 0)
		close(solux_cfgwatch_fd);
	solux_cfgwatch_fd = -1;
	solux_cfgwatch_n = 0;
}

#endif
