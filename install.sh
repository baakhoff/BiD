#!/usr/bin/env bash
# BiD one-line installer:
#   curl -fsSL https://raw.githubusercontent.com/baakhoff/BiD/master/install.sh | bash
#
# Installs the build dependencies with your distribution's package manager,
# builds the newest release, installs it (binary, desktop entry, icon) and
# writes the udev rule so BiD runs without root. Re-running updates to the
# newest release. Knobs:
#   BID_PREFIX=/usr/local   install prefix
#   BID_REF=<tag|branch>    build this ref instead of the newest release tag
#   BID_SKIP_UDEV=1         do not touch /etc/udev
set -euo pipefail

REPO="https://github.com/baakhoff/BiD"
PREFIX="${BID_PREFIX:-/usr/local}"

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

as_root() {
	if [ "$(id -u)" = 0 ]; then "$@"
	elif command -v sudo >/dev/null; then sudo "$@"
	else fail "need root for: $* (install sudo, or run as root)"
	fi
}

# ---------- dependencies ----------
have_tools() {
	command -v git >/dev/null && command -v cmake >/dev/null \
		&& command -v make >/dev/null && command -v pkg-config >/dev/null \
		&& { command -v c++ >/dev/null || command -v g++ >/dev/null; }
}
have_libs() {
	pkg-config --exists glfw3 libusb-1.0 gl 2>/dev/null
}

if have_tools && have_libs; then
	say "build dependencies already present"
else
	if command -v apt-get >/dev/null; then
		say "installing dependencies with apt"
		as_root apt-get update -qq
		as_root apt-get install -y git cmake g++ make pkg-config \
			libglfw3-dev libusb-1.0-0-dev libgl-dev libsystemd-dev
	elif command -v dnf >/dev/null; then
		say "installing dependencies with dnf"
		as_root dnf install -y git cmake gcc-c++ make pkgconf-pkg-config \
			glfw-devel libusb1-devel mesa-libGL-devel systemd-devel
	elif command -v pacman >/dev/null; then
		say "installing dependencies with pacman"
		as_root pacman -S --needed --noconfirm git cmake gcc make pkgconf \
			glfw libusb mesa
	elif command -v zypper >/dev/null; then
		say "installing dependencies with zypper"
		as_root zypper --non-interactive install git cmake gcc-c++ make pkg-config \
			libusb-1_0-devel Mesa-libGL-devel systemd-devel
		# the glfw dev package answers to different names across releases
		as_root zypper --non-interactive install glfw-devel \
			|| as_root zypper --non-interactive install libglfw-devel \
			|| fail "could not find a glfw development package"
	else
		have_tools && have_libs || fail \
			"no known package manager; install git, cmake, a C++ compiler, make, pkg-config, glfw3, libusb-1.0 and GL headers, then re-run"
	fi
	have_tools || fail "the build tools are still missing after the install"
fi

# ---------- source ----------
REF="${BID_REF:-}"
if [ -z "$REF" ]; then
	REF="$(git ls-remote --tags --refs "$REPO" 'v*' 2>/dev/null | awk -F/ '{print $NF}' | sort -V | tail -1)"
	[ -n "$REF" ] || REF=master
fi
WORK="$(mktemp -d /tmp/bid-install.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
say "fetching BiD $REF"
git clone -q --depth 1 -b "$REF" "$REPO" "$WORK/src"

# ---------- build ----------
say "building"
cmake -S "$WORK/src" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" > "$WORK/cmake.log" 2>&1 \
	|| { tail -20 "$WORK/cmake.log" >&2; fail "configure failed (log above)"; }
make -C "$WORK/build" -j"$(nproc)" > "$WORK/make.log" 2>&1 \
	|| { tail -20 "$WORK/make.log" >&2; fail "build failed (log above)"; }

# ---------- install ----------
say "installing to $PREFIX"
if [ -w "$PREFIX" ] || { [ ! -e "$PREFIX" ] && [ -w "$(dirname "$PREFIX")" ]; }; then
	make -C "$WORK/build" install >/dev/null
else
	as_root make -C "$WORK/build" install >/dev/null
fi
# icon and menu caches, best effort: a stale cache hides the menu entry
gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

# ---------- udev ----------
if [ "${BID_SKIP_UDEV:-0}" != 1 ]; then
	GRP=""
	getent group audio >/dev/null 2>&1 && GRP='GROUP="audio", '
	[ -z "$GRP" ] && getent group plugdev >/dev/null 2>&1 && GRP='GROUP="plugdev", '
	RULE="SUBSYSTEM==\"usb\", ATTR{idVendor}==\"2708\", MODE=\"0660\", ${GRP}TAG+=\"uaccess\""
	RULES_FILE=/etc/udev/rules.d/84-audient.rules
	if [ ! -e "$RULES_FILE" ] || [ "$(cat "$RULES_FILE" 2>/dev/null)" != "$RULE" ]; then
		say "writing the udev rule for the Audient vendor id"
		printf '%s\n' "$RULE" | as_root tee "$RULES_FILE" >/dev/null
		as_root udevadm control --reload-rules
		as_root udevadm trigger --attr-match=idVendor=2708 2>/dev/null || true
	else
		say "udev rule already in place"
	fi
fi

say "done: BiD $REF is installed"
say "launch it from your application menu, or run: $PREFIX/bin/BiD"
