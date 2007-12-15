Name:           kerneloops
Version:        0.3
Release:        1%{?dist}
Summary:        Tool to automatically collect and submit kernel crash signatures

Group:          System Environment/Base
License:        GPLv2
URL:            http://www.kerneloops
Source0:        kerneloops-0.3.tar.gz

BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  curl-devel

%description
This package contains the tools to collect kernel crash signatures, and to
submit them to the kerneloops website where the kernel crash signatures
get collected and groups for presentation to the Linux kernel developers.

%prep
%setup -q


%build
make CFLAGS="$RPM_OPT_FLAGS" %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc/init.d/
cp kerneloops.init $RPM_BUILD_ROOT/etc/init.d/kerneloops

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ $1 = 1 ]; then
        chkconfig --add kerneloops
fi

%preun
if [ $1 = 0 ]; then
        service kerneloops stop > /dev/null 2>&1
        /sbin/chkconfig --del kerneloops
fi

%files
%defattr(-,root,root,-)
%doc
/usr/sbin/kerneloops
%config(noreplace) /etc/kerneloops.org
/etc/init.d/kerneloops


%changelog
* Sun Dec 9 2008 Arjan van de Ven <arjan@linux.intel.com> - 0.3-1
- more fixes
* Sat Dec 8 2008 Arjan van de Ven <arjan@linux.intel.com> - 0.2-1
- bugfix to submit the whole oops on x86
* Sat Dec 1 2008 Arjan van de Ven <arjan@linux.intel.com> - 0.1-1
- Initial packaging
