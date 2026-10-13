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

/* Приводит имя раскладки к виду без пробелов и скобок: "[ monocle ]" -> "monocle". */
static void
soluxctl_norm_name(const char *in, char *out, size_t outsz)
{
	const char *p = in ? in : ""; /* начало имени */
	const char *q; /* конец имени */
	size_t len; /* длина результата */

	if (!outsz) /* некуда писать */
		return;
	while (isspace((unsigned char)*p)) /* пробелы в начале */
		p++;
	q = p + strlen(p);
	while (q > p && isspace((unsigned char)q[-1])) /* пробелы в конце */
		q--;
	if (q - p >= 2 && *p == '[' && q[-1] == ']') { /* убираем квадратные скобки */
		p++;
		q--;
		while (p < q && isspace((unsigned char)*p)) /* пробелы внутри скобок слева */
			p++;
		while (q > p && isspace((unsigned char)q[-1])) /* пробелы внутри скобок справа */
			q--;
	}
	len = (size_t)(q - p);
	if (len >= outsz) /* обрезаем под буфер */
		len = outsz - 1;
	memcpy(out, p, len);
	out[len] = '\0';
}

/* Ищет раскладку по имени из словаря layouts конфига (без учёта регистра и скобок). */
static int
soluxctl_layout_find(const char *name)
{
	char want[128], have[128]; /* нормализованные имена */
	size_t i;

	soluxctl_norm_name(name, want, sizeof(want));
	if (!*want) /* пустое имя */
		return -1;
	for (i = 0; i < layouts_len; i++) {
		if (!layouts[i].symbol) /* раскладка без имени */
			continue;
		soluxctl_norm_name(layouts[i].symbol, have, sizeof(have));
		if (!strcasecmp(have, want)) /* имя совпало */
			return (int)i;
	}
	return -1;
}

/* SET-LAYOUT [имя]: без имени - следующая раскладка по кругу, с именем - именно эта. */
static void
soluxctl_cmd_set_layout(int cfd, const char *arg)
{
	char msg[256]; /* текст ошибки */
	char shown[128]; /* имя для сообщения */
	int idx; /* индекс найденной раскладки */

	while (isspace((unsigned char)*arg)) /* пропускаем пробелы после команды */
		arg++;
	if (!selmon || !selmon->pertag) { /* нет активного монитора */
		soluxctl_write_reply(cfd, "ERR no active monitor\n");
		return;
	}
	if (!layouts_len) { /* в конфиге нет раскладок */
		soluxctl_write_reply(cfd, "ERR no layouts configured\n");
		return;
	}
	if (!*arg) { /* без аргумента: цикл по раскладкам из конфига */
		Arg next = {.i = 1};
		cyclelayout(&next);
		soluxctl_write_reply(cfd, "OK\n");
		return;
	}
	idx = soluxctl_layout_find(arg);
	if (idx < 0) { /* такой раскладки нет в конфиге */
		soluxctl_norm_name(arg, shown, sizeof(shown));
		snprintf(msg, sizeof(msg), "ERR layout '%s' does not exist\n", shown);
		soluxctl_write_reply(cfd, msg);
		return;
	}
	solux_setlayout_on(selmon, &layouts[idx]); /* тот же путь, что и у setlayout() */
	soluxctl_write_reply(cfd, "OK\n");
}

/* LAYOUTS / TAGS: "OK" и затем по одному имени на строку. */
static void
soluxctl_cmd_list(int cfd, int tags_list)
{
	char out[4096]; /* буфер ответа */
	size_t len = 0, i, count; /* занято, индекс, сколько имён */
	int n; /* результат snprintf */

	n = snprintf(out, sizeof(out), "OK\n");
	len = (n > 0) ? (size_t)n : 0;
	count = tags_list ? tagcount : layouts_len;
	for (i = 0; i < count; i++) {
		const char *name = tags_list ? tags[i] : layouts[i].symbol; /* имя элемента */
		n = snprintf(out + len, sizeof(out) - len, "%s\n", name ? name : "");
		if (n < 0 || (size_t)n >= sizeof(out) - len) /* не помещается - останавливаемся */
			break;
		len += (size_t)n;
	}
	soluxctl_write_reply(cfd, out);
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
	} else if (!strcasecmp(buf, "KBLAYOUT NEXT") || !strcasecmp(buf, "SET-KBLAYOUT")) {
		/* Следующая раскладка клавиатуры по кругу (из keyboard_layout, например "us,ru") */
		if (solux_kblayout_step(1) < 0) /* клавиатура ещё не готова */
			soluxctl_write_reply(cfd, "ERR keyboard is not ready\n");
		else
			soluxctl_write_reply(cfd, "OK\n");
	} else if (!strncasecmp(buf, "SET-LAYOUT", 10) &&
			(buf[10] == '\0' || isspace((unsigned char)buf[10]))) {
		soluxctl_cmd_set_layout(cfd, buf + 10); /* смена раскладки окон по имени или по кругу */
	} else if (!strcasecmp(buf, "LAYOUTS")) {
		soluxctl_cmd_list(cfd, 0); /* список имён раскладок из конфига */
	} else if (!strcasecmp(buf, "TAGS")) {
		soluxctl_cmd_list(cfd, 1); /* список имён тегов из конфига */
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
