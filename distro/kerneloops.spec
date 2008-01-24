Name:		kerneloops
Version:	0.10
Release:	1%{?dist}
Summary:	Tool to automatically collect and submit kernel crash signatures

Group:		System Environment/Base
License:	GPLv2
URL:		http://www.kerneloops.org
Source0:	http://www.kerneloops.org/download/%{name}-%{version}.tar.gz

BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:	curl-devel
BuildRequires:	libnotify-devel
BuildRequires:	gtk2-devel
BuildRequires:	dbus-glib-devel
BuildRequires:	gettext
BuildRequires:	desktop-file-utils
Requires(post):		chkconfig
Requires(preun):	chkconfig, initscripts

%description
This package contains the tools to collect kernel crash signatures,
and to submit them to the kerneloops.org website where the kernel
crash signatures get collected and grouped for presentation to the
Linux kernel developers.


%prep
%setup -q

%build
make CFLAGS="$RPM_OPT_FLAGS" %{?_smp_mflags}

%check
make tests

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -m 0755 -p $RPM_BUILD_ROOT%{_initrddir}
install -p -m 0755 kerneloops.init $RPM_BUILD_ROOT%{_initrddir}/%{name}
%find_lang %{name}

%clean
make clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add %{name}


%preun
if [ $1 = 0 ]; then
	/sbin/service %{name} stop > /dev/null 2>&1
	/sbin/chkconfig --del %{name}
fi


%files -f %{name}.lang
%defattr(-,root,root)
%doc COPYING Changelog
%{_sbindir}/%{name}
%config(noreplace) %{_sysconfdir}/kerneloops.conf
%{_initrddir}/%{name}
%{_sysconfdir}/dbus-1/system.d/kerneloops.dbus
%{_sysconfdir}/xdg/autostart/kerneloops-applet.desktop
%{_datadir}/kerneloops/
%{_bindir}/kerneloops-applet
%{_mandir}/man8/kerneloops.8.gz

%changelog
* Sat Jan 5 2008 Arjan van de Ven <arjan@linux.intel.com> - 0.10-1
- fix some bugs caught by the fedora review process
* Tue Jan 1 2008 Arjan van de Ven <arjan@linux.intel.com> - 0.9-1
- make translatable
* Mon Dec 31 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.8-1
- Add UI applet to ask the privacy question
* Sat Dec 29 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.7-1
- fix memory leak
* Wed Dec 19 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.6-1
- various cleanups and minor improvements
- Merged Matt Domsch's improvements
* Tue Dec 18 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.5-1
- fix infinite loop
* Mon Dec 17 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.4-1
- PPC bugfixes
* Sun Dec 9 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.3-1
- more fixes
* Sat Dec 8 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.2-1
- bugfix to submit the whole oops on x86
* Sat Dec 1 2007 Arjan van de Ven <arjan@linux.intel.com> - 0.1-1
- Initial packaging
