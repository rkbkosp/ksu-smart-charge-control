SHELL := /bin/sh

ANDROID_API ?= 28
NDK ?=
UNAME_S := $(shell uname -s)
HOST_TAG ?= $(if $(filter Darwin,$(UNAME_S)),darwin-x86_64,linux-x86_64)
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST_TAG)
ANDROID_CC := $(TOOLCHAIN)/bin/aarch64-linux-android$(ANDROID_API)-clang

NATIVE_SOURCES := \
	native/main.c \
	native/config.c \
	native/policy.c \
	native/wifi.c \
	native/power_event.c \
	native/charge_backend.c

COMMON_WARNINGS := -Wall -Wextra -Werror -Wconversion -Wshadow -Wformat=2
RELEASE_CFLAGS := -std=c11 -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -O2 -flto \
	-ffunction-sections -fdata-sections -fvisibility=hidden \
	-fstack-protector-strong -fPIE $(COMMON_WARNINGS)
RELEASE_LDFLAGS := -flto -pie -Wl,--gc-sections -Wl,--strip-all -Wl,--as-needed \
	-Wl,-z,relro,-z,now -Wl,--build-id=none

.PHONY: all android package host-test clean

all: android

android: charge-policy/bin/charged

charge-policy/bin/charged: $(NATIVE_SOURCES) $(wildcard native/*.h)
	@test -n "$(NDK)" || { echo "NDK is required (run scripts/build-android.sh)" >&2; exit 2; }
	@test -x "$(ANDROID_CC)" || { echo "Android compiler not found: $(ANDROID_CC)" >&2; exit 2; }
	@mkdir -p build charge-policy/bin
	$(ANDROID_CC) $(RELEASE_CFLAGS) -Inative $(NATIVE_SOURCES) $(RELEASE_LDFLAGS) -o build/charged
	cp build/charged $@
	chmod 0755 $@

host-test:
	$(MAKE) -C tests run

package: android
	./scripts/package.sh

clean:
	rm -rf build dist charge-policy/bin/charged tests/test_charged tests/test_charged.dSYM
