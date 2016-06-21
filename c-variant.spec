Name:           c-variant
Version:        1
Release:        2%{?dist}
Summary:        GVariant Implementation
License:        LGPLv2+
URL:            https://github.com/c-util/c-variant
Source0:        https://github.com/c-util/c-variant/archive/v%{version}.tar.gz
BuildRequires:  autoconf automake pkgconfig

%description
Standalone GVariant Implementation in Standard ISO-C11

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license COPYING
%license LICENSE.LGPL2.1
%{_libdir}/libcvariant.so.*

%files devel
%{_includedir}/c-variant.h
%{_libdir}/libcvariant.so
%{_libdir}/pkgconfig/c-variant.pc

%changelog
* Tue Jun 21 2016 <kay@redhat.com> 1-2
- update spec file according to Fedora guidelines

* Mon Apr 25 2016 <kay@redhat.com> 1-1
- c-variant 1
