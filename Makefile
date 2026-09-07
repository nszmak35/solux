.POSIX:
.SUFFIXES:

include config.mk

DWLCPPFLAGS = -I. -Iext-prot -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
DWLDEVCFLAGS = -g -Wpedantic -w -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

PKGS      = wayland-server xkbcommon libinput pixman-1 $(XLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

SOLUXCTL_CFLAGS = $(CFLAGS) -I. -std=c99 -D_POSIX_C_SOURCE=200809L `$(PKG_CONFIG) --cflags wayland-client`
SOLUXCTL_LDLIBS = `$(PKG_CONFIG) --libs wayland-client`

all: solux soluxctl

solux: dwl.o util.o parsednx.o dwl-ipc-unstable-v2-protocol.o ext-prot/wlr_ext_workspace_v1.o
	$(CC) dwl.o util.o parsednx.o dwl-ipc-unstable-v2-protocol.o $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

dwl.o: solux.c parsednx.h ext-prot/ext-workspace.h ext-prot/wlr_ext_workspace_v1.h ext-workspace-v1-protocol.h client.h config.mk cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	dwl-ipc-unstable-v2-protocol.h
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c solux.c


soluxctl: soluxctl/soluxctl

soluxctl/soluxctl: soluxctl/soluxctl.o dwl-ipc-unstable-v2-protocol.o
	$(CC) soluxctl/soluxctl.o dwl-ipc-unstable-v2-protocol.o $(SOLUXCTL_CFLAGS) $(LDFLAGS) $(SOLUXCTL_LDLIBS) -o $@

soluxctl/soluxctl.o: soluxctl/soluxctl.c dwl-ipc-unstable-v2-client-protocol.h
	$(CC) $(CPPFLAGS) $(SOLUXCTL_CFLAGS) -o $@ -c soluxctl/soluxctl.c

util.o: util.c util.h

parsednx.o: parsednx.c parsednx.h
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c parsednx.c

dwl-ipc-unstable-v2-protocol.o: dwl-ipc-unstable-v2-protocol.c dwl-ipc-unstable-v2-protocol.h
ext-prot/wlr_ext_workspace_v1.o: ext-prot/wlr_ext_workspace_v1.c ext-prot/wlr_ext_workspace_v1.h ext-workspace-v1-protocol.h

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

dwl-ipc-unstable-v2-client-protocol.h: protocols/dwl-ipc-unstable-v2.xml
	$(WAYLAND_SCANNER) client-header \
		protocols/dwl-ipc-unstable-v2.xml $@

dwl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/dwl-ipc-unstable-v2.xml $@
dwl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-ipc-unstable-v2.xml $@
wlr-foreign-toplevel-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-foreign-toplevel-management-unstable-v1.xml $@
ext-workspace-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header ext-prot/ext-workspace-v1.xml $@

clean:
	rm -f solux dwl *.o soluxctl/*.o ext-prot/*.o \
		dwl-ipc-unstable-v2-protocol.h \
		dwl-ipc-unstable-v2-client-protocol.h

install: solux soluxctl/soluxctl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(PREFIX)/bin/soluxctl
	cp -f solux $(DESTDIR)$(PREFIX)/bin/solux
	cp -f soluxctl/soluxctl $(DESTDIR)$(PREFIX)/bin/soluxctl
	chmod 755 $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(PREFIX)/bin/soluxctl
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f solux.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/solux.desktop
	mkdir -p $(DESTDIR)/etc/solux
	cp -r default/config.dnx $(DESTDIR)/etc/solux

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(MANDIR)/man1/dwl.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/solux.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $<
