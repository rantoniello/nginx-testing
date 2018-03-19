%define package_name nginx-helloworld

Summary:   TCDN Nginx Module Helloworld
Name:      nginx-helloworld
Version:   %{versionModule}
Release:   %{releaseModule}
License:   © Telefonica I+D
Group:     Development/Libraries
BuildRoot: %{_topdir}/BUILDROOT
Prefix:    %{_prefix}
BuildArch: x86_64
Vendor:    Telefonica I+D
AutoReq:   no
Requires:  nginx

%description
%{summary}

%install
#-------------------------------------------------------------------------------
# INSTALL
#-------------------------------------------------------------------------------
cd %{_sourcedir}/../../..
mkdir -p $RPM_BUILD_ROOT/opt/p2pcdn/lib
cp -r build/* $RPM_BUILD_ROOT/opt/p2pcdn/lib/.
cp -r %{_sourcedir}/* $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_ROOT/modules

%clean
rm -rf $RPM_BUILD_ROOT

%files
/opt/p2pcdn/lib/*.so
%(config)/usr/share/nginx/modules/*.conf
%(config)/etc/nginx/default.d/*.conf

