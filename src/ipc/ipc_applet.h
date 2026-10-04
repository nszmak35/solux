/* soluxctl control socket. */
#ifndef SOLUX_IPC_IPC_APPLET_H
#define SOLUX_IPC_IPC_APPLET_H

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

#endif
