Name:           osddtransfer
Version:        0.1.0
Release:        1%{?dist}
Summary:        Data diode transfer manager for Maven, container, and RPM mirrors
License:        MIT
URL:            https://example.invalid/osddtransfer
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  yaml-cpp-devel
Requires:       skopeo
Requires:       dnf-plugins-core
Requires:       systemd

%description
OSDD Transfer manager stages content for upstream/downstream diode transfers.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
/usr/bin/osddtransfer
/etc/osddtransfer/osddtransfer.yaml
/usr/lib/systemd/user/osddtransfer.service

%changelog
* Sat May 09 2026 Codex <codex@example.invalid> - 0.1.0-1
- Initial package
