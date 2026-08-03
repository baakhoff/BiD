Name:           bid
Version:        0.2.2
Release:        1%{?dist}
Summary:        Mixer control panel for Audient iD audio interfaces
License:        MIT
URL:            https://github.com/baakhoff/BiD
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/bid-%{version}.tar.gz
Source1:        https://github.com/ocornut/imgui/archive/refs/tags/v1.92.4.tar.gz#/imgui-1.92.4.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig(glfw3)
BuildRequires:  pkgconfig(libusb-1.0)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(gl)

%description
Unofficial Linux control panel for the Audient iD series USB audio
interfaces: mixes and cues, routing, loopback, VU meters and the monitor
section, over the interface's spare USB port so audio keeps playing while
the mixer is open.

%prep
%autosetup -n BiD-%{version}
tar -xzf %{SOURCE1} -C .

%build
%cmake -DFETCHCONTENT_SOURCE_DIR_IMGUI_EXTERNAL=$PWD/imgui-1.92.4
%cmake_build

%install
%cmake_install

%files
%license LICENSE.md
%doc README.md
%{_bindir}/BiD
%{_datadir}/applications/bid.desktop
%{_datadir}/icons/hicolor/scalable/apps/bid.svg
%{_prefix}/lib/udev/rules.d/84-audient.rules

%changelog
* Fri Jul 31 2026 baakhoff - 0.2.2-1
- First packaged release
