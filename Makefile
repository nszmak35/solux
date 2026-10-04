.POSIX:
.SUFFIXES:

include config.mk

SRCDIR   = src
BUILDDIR = build
PROTODIR = protocols
GENDIR   = $(BUILDDIR)/gen

# SceneFX 0.5 requires the wlroots 0.20 ABI. config.mk must provide
# WLR_INCS/WLR_LIBS for that same wlroots-0.20 installation.
DWLCPPFLAGS = -I$(SRCDIR) -I$(GENDIR) -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
DWLDEVCFLAGS = -g -Wpedantic -w -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

PKGS      = scenefx-0.5 wayland-server xkbcommon libinput pixman-1 $(XLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

PYTHON_CFLAGS = `$(PKG_CONFIG) --cflags python3-embed 2>/dev/null || python3-config --includes`
PYTHON_LIBS   = `$(PKG_CONFIG) --libs python3-embed 2>/dev/null || python3-config --embed --ldflags`

SOLUXCTL_CFLAGS = $(CFLAGS) -I$(SRCDIR) -I$(GENDIR) -std=c99 -D_POSIX_C_SOURCE=200809L `$(PKG_CONFIG) --cflags wayland-client`
SOLUXCTL_LDLIBS = `$(PKG_CONFIG) --libs wayland-client`

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

# Every header below is generated from an XML file by wayland-scanner. They are
# written to $(GENDIR); wlroots headers include several of them by bare name.
GEN_HEADERS = \
	$(GENDIR)/cursor-shape-v1-protocol.h \
	$(GENDIR)/pointer-constraints-unstable-v1-protocol.h \
	$(GENDIR)/wlr-layer-shell-unstable-v1-protocol.h \
	$(GENDIR)/wlr-output-power-management-unstable-v1-protocol.h \
	$(GENDIR)/xdg-shell-protocol.h \
	$(GENDIR)/ext-workspace-v1-protocol.h \
	$(GENDIR)/dwl-ipc-unstable-v2-protocol.h \
	$(GENDIR)/dwl-ipc-unstable-v2-client-protocol.h

HEADERS = $(wildcard $(SRCDIR)/*.h $(SRCDIR)/*/*.h)

SOLUX_OBJS = \
	$(BUILDDIR)/solux.o \
	$(BUILDDIR)/util.o \
	$(BUILDDIR)/parser/parserconf.o \
	$(BUILDDIR)/ext_workspace/wlr_ext_workspace_v1.o \
	$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o

SOLUXCTL_OBJS = \
	$(BUILDDIR)/soluxctl/soluxctl.o \
	$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o

all: solux soluxctl

solux: $(SOLUX_OBJS)
	$(CC) $(SOLUX_OBJS) $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) $(PYTHON_LIBS) -o $@

soluxctl: $(SOLUXCTL_OBJS)
	$(CC) $(SOLUXCTL_OBJS) $(SOLUXCTL_CFLAGS) $(LDFLAGS) $(SOLUXCTL_LDLIBS) -o $@

# solux.c includes the other modules under src/ directly (single translation unit).
$(BUILDDIR)/solux.o: $(SRCDIR)/solux.c $(HEADERS) $(GEN_HEADERS) config.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $(SRCDIR)/solux.c

$(BUILDDIR)/util.o: $(SRCDIR)/util.c $(SRCDIR)/util.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $(SRCDIR)/util.c

$(BUILDDIR)/parser/parserconf.o: $(SRCDIR)/parser/parserconf.c $(SRCDIR)/parser/parserconf.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) $(PYTHON_CFLAGS) -o $@ -c $(SRCDIR)/parser/parserconf.c

$(BUILDDIR)/ext_workspace/wlr_ext_workspace_v1.o: $(SRCDIR)/ext_workspace/wlr_ext_workspace_v1.c \
		$(SRCDIR)/ext_workspace/wlr_ext_workspace_v1.h $(GENDIR)/ext-workspace-v1-protocol.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $(SRCDIR)/ext_workspace/wlr_ext_workspace_v1.c

$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o: $(GENDIR)/dwl-ipc-unstable-v2-protocol.c $(GENDIR)/dwl-ipc-unstable-v2-protocol.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $(GENDIR)/dwl-ipc-unstable-v2-protocol.c

$(BUILDDIR)/soluxctl/soluxctl.o: $(SRCDIR)/soluxctl/soluxctl.c $(GENDIR)/dwl-ipc-unstable-v2-client-protocol.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SOLUXCTL_CFLAGS) -o $@ -c $(SRCDIR)/soluxctl/soluxctl.c

# Protocol generation (XML sources live in $(PROTODIR)/, except the stable and
# staging protocols that ship with wayland-protocols).
$(GENDIR)/cursor-shape-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@

$(GENDIR)/pointer-constraints-unstable-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@

$(GENDIR)/xdg-shell-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

$(GENDIR)/wlr-layer-shell-unstable-v1-protocol.h: $(PROTODIR)/wlr-layer-shell-unstable-v1.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header $(PROTODIR)/wlr-layer-shell-unstable-v1.xml $@

$(GENDIR)/wlr-output-power-management-unstable-v1-protocol.h: $(PROTODIR)/wlr-output-power-management-unstable-v1.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header $(PROTODIR)/wlr-output-power-management-unstable-v1.xml $@

$(GENDIR)/ext-workspace-v1-protocol.h: $(PROTODIR)/ext-workspace-v1.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header $(PROTODIR)/ext-workspace-v1.xml $@

$(GENDIR)/dwl-ipc-unstable-v2-protocol.h: $(PROTODIR)/dwl-ipc-unstable-v2.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header $(PROTODIR)/dwl-ipc-unstable-v2.xml $@

$(GENDIR)/dwl-ipc-unstable-v2-client-protocol.h: $(PROTODIR)/dwl-ipc-unstable-v2.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) client-header $(PROTODIR)/dwl-ipc-unstable-v2.xml $@

$(GENDIR)/dwl-ipc-unstable-v2-protocol.c: $(PROTODIR)/dwl-ipc-unstable-v2.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) private-code $(PROTODIR)/dwl-ipc-unstable-v2.xml $@

clean:
	rm -rf $(BUILDDIR) solux soluxctl

install: solux soluxctl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(PREFIX)/bin/soluxctl
	cp -f solux $(DESTDIR)$(PREFIX)/bin/solux
	cp -f soluxctl $(DESTDIR)$(PREFIX)/bin/soluxctl
	chmod 755 $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(PREFIX)/bin/soluxctl
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f solux.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/solux.desktop
	mkdir -p $(DESTDIR)/etc/solux
	cp -f default/config.py $(DESTDIR)/etc/solux/config.py

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/solux $(DESTDIR)$(PREFIX)/bin/soluxctl \
		$(DESTDIR)$(DATADIR)/wayland-sessions/solux.desktop

.PHONY: all clean install uninstall
