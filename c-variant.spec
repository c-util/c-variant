%global soname cvariant

%global commit d1cdeefcc82443fcae5d47fb6ce198c3b46abdd8
%global shortcommit %(c=%{commit}; echo ${c:0:7})

Name:           c-variant
Version:        0
Release:        0.1git%{shortcommit}%{?dist}
Summary:        GVariant Implementation
License:        LGPLv2+
URL:            https://github.com/c-util/%{name}
Source0:        %{url}/archive/%{commit}/%{name}-%{shortcommit}.tar.gz
BuildRequires:  gcc
BuildRequires:  autoconf
BuildRequires:  automake

%description
Standalone GVariant Implementation in Standard ISO-C11

%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{?epoch:%{epoch}:}%{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%autosetup -n %{name}-%{commit}

%build
./autogen.sh
%configure
%make_build

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%license COPYING LICENSE.LGPL2.1
%{_libdir}/lib%{soname}.so.*

%files devel
%{_includedir}/%{name}.h
%{_libdir}/lib%{soname}.so
%{_libdir}/pkgconfig/%{name}.pc

%changelog
* Thu Jul 07 2016 Igor Gnatenko <ignatenko@redhat.com> - 0-0.1gitd1cdeef
- Add missing BR
- add missing %%{?_isa} in Requires
- Trivial fixes

* Tue Jun 21 2016 <kay@redhat.com> 0-0.0.2
- update spec file according to Fedora guidelines

* Mon Apr 25 2016 <kay@redhat.com> 0-0.0.1
- c-variant 1
