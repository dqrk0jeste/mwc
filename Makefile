PKG_CONFIG?=pkg-config
WAYLAND_PROTOCOLS!=$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols
WAYLAND_SCANNER!=$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner

PKGS="wlroots-0.18" wayland-server xkbcommon libinput
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
CFLAGS+=-Ibuild/protocols
ifdef DEBUG
CFLAGS += -fsanitize=address,undefined
endif
LIBS!=$(PKG_CONFIG) --libs $(PKGS)

SRC_FILES := $(wildcard src/*.c)
OBJ_FILES := $(patsubst src/%.c, build/%.o, $(SRC_FILES))

all: build/notwc build/notwc-ipc

build:
	mkdir -p build

build/protocols: build
	mkdir -p build/protocols

build/protocols/xdg-shell-protocol.h: build/protocols
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

build/protocols/wlr-layer-shell-unstable-v1-protocol.h: build/protocols protocols/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) server-header \
		./protocols/wlr-layer-shell-unstable-v1.xml $@

build/protocols/cursor-shape-v1-protocol.h: build/protocols protocols/cursor-shape-v1.xml
	$(WAYLAND_SCANNER) server-header \
		./protocols/cursor-shape-v1.xml $@

build/protocols/xdg-output-unstable-v1-protocol.h: build/protocols
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

build/%.o: src/%.c src/%.h build build/protocols/xdg-shell-protocol.h build/protocols/wlr-layer-shell-unstable-v1-protocol.h build/protocols/xdg-output-unstable-v1-protocol.h build/protocols/cursor-shape-v1-protocol.h
	$(CC) -c $< $(CFLAGS) -DWLR_USE_UNSTABLE -o $@

build/notwc: $(OBJ_FILES)
	$(CC) $^ $> $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

build/notwc-ipc: notwc-ipc/notwc-ipc.c
	$(CC) $< -o $@

install: build/notwc build/notwc-ipc default.conf notwc-portals.conf notwc.desktop
	install -Dm755 build/notwc "/usr/local/bin/notwc"; \
	install -Dm755 build/notwc-ipc "/usr/local/bin/notwc-ipc"; \
	install -Dm644 default.conf "/usr/share/notwc/default.conf"; \
  install -Dm644 LICENSE "/usr/share/licenses/notwc/LICENSE"; \
	install -Dm644 notwc.desktop "/usr/share/wayland-sessions/notwc.desktop"; \
	install -Dm644 notwc-portals.conf "/usr/share/xdg-desktop-portal/notwc-portals.conf"

uninstall:
	rm /usr/local/bin/notwc; \
	rm /usr/local/bin/notwc-ipc; \
	rm -rf /usr/share/notwc; \
	rm /usr/share/xdg-desktop-portal/notwc-portals.conf; \
	rm /usr/share/wayland-sessions/notwc.desktop

clean:
	rm -rf build 2>/dev/null

.PHONY: all clean install uninstall
