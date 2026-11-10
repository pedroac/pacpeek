CC ?= gcc
# I18N=0 builds an English-only binary: no gettext, HarfBuzz, FriBidi, per-language
# fonts or catalogs.
I18N ?= 1

PKGS = wayland-client cairo freetype2
ifeq ($(I18N),1)
PKGS += harfbuzz fribidi
endif
# CFLAGS, LDFLAGS and LDLIBS belong to the user (or the distro's packaging) and can be
# overridden freely; what the build itself needs is added separately below.
CFLAGS ?= -O2 -Wall -Wextra
# Hardening: fortified libc calls, stack protector, PIE, full RELRO and immediate binding.
# Control-flow protection only exists on x86 compilers. Ask the compiler rather than guess
# from the architecture (uname -m would describe the build host, which is wrong when
# cross-compiling, and the C preprocessor can't pass a flag to the compiler anyway).
CET_CFLAGS := $(shell echo 'int main(void){return 0;}' | $(CC) -x c - -fcf-protection=full \
                -o /dev/null 2>/dev/null && echo -fcf-protection=full)
ALL_CFLAGS := $(CFLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong \
              -fstack-clash-protection $(CET_CFLAGS) -fPIE -MMD -MP -Ibuild \
              $(shell pkg-config --cflags $(PKGS))
ALL_LDFLAGS := $(LDFLAGS) -Wl,--as-needed -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
ALL_LIBS := $(LDLIBS) $(shell pkg-config --libs $(PKGS))

WL_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
PROTOCOLS = protocol/wlr-layer-shell-unstable-v1.xml \
            $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml \
            $(WL_PROTOCOLS)/stable/viewporter/viewporter.xml \
            $(WL_PROTOCOLS)/staging/single-pixel-buffer/single-pixel-buffer-v1.xml

PROTO_NAMES = $(basename $(notdir $(PROTOCOLS)))
PROTO_HEADERS = $(PROTO_NAMES:%=build/%-client-protocol.h)
PROTO_OBJS = $(PROTO_NAMES:%=build/%-client-protocol.o)

SRCS = $(wildcard src/*.c)
ifneq ($(I18N),1)
SRCS := $(filter-out src/shape.c,$(SRCS))
endif
OBJS = $(SRCS:src/%.c=build/%.o) $(PROTO_OBJS)
TARGET = pacpeek

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LOCALEDIR ?= $(PREFIX)/share/locale
export LOCALEDIR

ifeq ($(I18N),1)
ifneq ($(shell command -v msgfmt 2>/dev/null),)
LINGUAS = $(basename $(notdir $(wildcard po/*.po)))
else
$(info msgfmt not found: building without translation catalogs (install gettext, or use I18N=0))
endif
endif
MOS = $(LINGUAS:%=build/locale/%/LC_MESSAGES/$(TARGET).mo)

all: $(TARGET) $(MOS)

$(TARGET): $(OBJS)
	$(CC) $(ALL_LDFLAGS) $^ -o $@ $(ALL_LIBS)

build/%.o: src/%.c | $(PROTO_HEADERS) build/config.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@

build/%.o: build/%.c | $(PROTO_HEADERS)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Rewritten only when its content changes, so a new LOCALEDIR or I18N rebuilds what uses it.
build/config.h: FORCE | build
	@case "$$LOCALEDIR" in *[[:cntrl:]]*) echo "LOCALEDIR must not contain control characters" >&2; exit 1;; esac; \
	{ printf '#define LOCALEDIR "%s"\n' "$$(printf '%s' "$$LOCALEDIR" | sed 's/[\\"]/\\&/g')"; \
	   [ '$(I18N)' = 1 ] || echo '#define NO_I18N 1'; } > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

vpath %.xml $(sort $(dir $(PROTOCOLS)))

build/%-client-protocol.h: %.xml | build
	wayland-scanner client-header $< $@

build/%-client-protocol.c: %.xml | build
	wayland-scanner private-code $< $@

build/locale/%/LC_MESSAGES/$(TARGET).mo: po/%.po
	@mkdir -p $(dir $@)
	msgfmt --check -o $@ $<

build:
	mkdir -p build

pot:
	xgettext --from-code=UTF-8 --keyword=_ --keyword=N_ --keyword=ngettext:1,2 \
		--add-comments=TRANSLATORS: --add-location=file --package-name=$(TARGET) \
		-o po/$(TARGET).pot $(sort $(SRCS))

update-po: pot
	for po in po/*.po; do msgmerge --update --backup=none --add-location=file $$po po/$(TARGET).pot; done

clean:
	rm -rf build $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	for lang in $(LINGUAS); do \
		install -Dm644 build/locale/$$lang/LC_MESSAGES/$(TARGET).mo \
			$(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/$(TARGET).mo; \
	done

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	for lang in $(LINGUAS); do \
		rm -f $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/$(TARGET).mo; \
	done

-include $(wildcard build/*.d)

.PHONY: all clean install uninstall pot update-po FORCE
