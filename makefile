PREFIX ?= /usr
WEBKITVER ?= 4.1
WEBKIT ?= webkit2gtk-$(WEBKITVER)
EXTENSION_DIR ?= $(PREFIX)/lib/wyebrowser
DISTROURI ?= https://archlinux.org/
DISTRONAME ?= "Arch Linux"
CFLAGS += -Wno-parentheses -ggdb -lX11
DEBUG ?= 1

ifneq ($(WEBKITVER), 4.0)
	VERDIR=/$(WEBKITVER)
endif
ifeq ($(DEBUG), 1)
	CFLAGS += -Wall -Wno-deprecated-declarations
else
	CFLAGS += -Wno-deprecated-declarations
endif

# export PKG_CONFIG_PATH appropriately and invoke make with MKCLPLUG=1
ifdef MKCLPLUG
	CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config mkclplug-1 --cflags) -DMKCLPLUG
	LDFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH)  pkg-config mkclplug-1 --libs)
endif


all: wyeb ext.so

wyeb: main.c general.c makefile surfprop.h extraschemes.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< \
		-D_GNU_SOURCE \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 $(WEBKIT) gcr-3` \
		-DEXTENSION_DIR=\"$(EXTENSION_DIR)$(VERDIR)\" \
		-DGCR_API_SUBJECT_TO_CHANGE \
		-DDISTROURI=\"$(DISTROURI)\" \
		-DDISTRONAME=\"$(DISTRONAME)\" \
		-DDEBUG=${DEBUG} -lm

ext.so: ext.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -shared -fPIC \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 $(WEBKIT)` \
		-DDEBUG=${DEBUG} -DJSC=${JSC}

clean:
	rm -f wyeb ext.so

install: all
	install -Dm755 wyeb   $(DESTDIR)$(PREFIX)/bin/wyeb
	install -Dm755 ext.so   $(DESTDIR)$(EXTENSION_DIR)$(VERDIR)/ext.so
	install -Dm644 wyeb.png   $(DESTDIR)$(PREFIX)/share/pixmaps/wyeb.png
	install -Dm644 wyeb.desktop $(DESTDIR)$(PREFIX)/share/applications/wyeb.desktop
	install -Dm755 omnihist-wyeb $(DESTDIR)$(PREFIX)/bin/omnihist-wyeb

uninstall:
	rm -f  $(PREFIX)/bin/wyeb
	rm -f  $(EXTENSION_DIR)$(VERDIR)/ext.so
	-rmdir $(EXTENSION_DIR)$(VERDIR)
	-rmdir $(EXTENSION_DIR)
	rm -f  $(PREFIX)/share/pixmaps/wyeb.png
	rm -f  $(PREFIX)/share/applications/wyeb.desktop


re: clean all
#	$(MAKE) clean
#	$(MAKE) all

full: re install
