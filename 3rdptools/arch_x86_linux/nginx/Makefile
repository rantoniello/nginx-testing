LIBNAME:= nginx
BUILD_DIR:= $(shell pwd)/build_dir
SOURCE_DIR:= $(TOOLS_DIR)/nginx-1.10.2
PREFIX:= $(INSTALL_DIR)$(CROSS_ARCH)

OPENSSL_DIR:= $(shell pwd)/../openssl/build_dir

# Export PATH to be able to find bin/ executables in compilation
export PATH:= $(PREFIX)/bin:$(PATH)

# General configurations
CONFIGURE_PARAMS= --prefix=${PREFIX} 
CONFIGURE_PARAMS+= --with-cc-opt=-I$(PREFIX)/include --with-ld-opt=-L$(PREFIX)/lib
CONFIGURE_PARAMS+= --with-pcre

# Added modules -private-
#CONFIGURE_PARAMS+= --add-module=$(PREFIX)/../nginx_modules/hello_world

# RHL configuration for Nginx's RPM
#//--prefix=/usr/share/nginx // We will not use path-related configurations
#//--sbin-path=/usr/sbin/nginx
#//--modules-path=/usr/lib64/nginx/modules
#//--conf-path=/etc/nginx/nginx.conf
#//--error-log-path=/var/log/nginx/error.log
#//--http-log-path=/var/log/nginx/access.log
#//--http-client-body-temp-path=/var/lib/nginx/tmp/client_body
#//--http-proxy-temp-path=/var/lib/nginx/tmp/proxy
#//--http-fastcgi-temp-path=/var/lib/nginx/tmp/fastcgi
#//--http-uwsgi-temp-path=/var/lib/nginx/tmp/uwsgi
#//--http-scgi-temp-path=/var/lib/nginx/tmp/scgi
#//--pid-path=/run/nginx.pid --lock-path=/run/lock/subsys/nginx
#//
#//--with-google_perftools_module // We will not use this module by the moment
#//--with-http_addition_module
#//--with-http_xslt_module=dynamic
#//--with-http_image_filter_module=dynamic
#//--with-http_geoip_module=dynamic
#//--with-http_sub_module
#//--with-http_dav_module
#//--with-http_flv_module
#//--with-http_mp4_module
#//--with-http_perl_module=dynamic
#//--with-cc-opt='-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions \
-fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches \
-specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -m64 -mtune=generic'
#//--with-ld-opt='-Wl,-z,relro -specs=/usr/lib/rpm/redhat/redhat-hardened-ld -Wl,-E'

CONFIGURE_PARAMS+= --user=nginx --group=nginx --with-file-aio --with-ipv6 --with-http_ssl_module \
--with-http_v2_module --with-http_realip_module --with-http_gunzip_module \
--with-http_gzip_static_module --with-http_random_index_module \
--with-http_secure_link_module --with-http_degradation_module \
--with-http_slice_module --with-http_stub_status_module \
--with-mail=dynamic --with-mail_ssl_module \
--with-pcre --with-stream=dynamic --with-stream_ssl_module \
--with-debug
# enable thread pool support
CONFIGURE_PARAMS+= --with-threads

all: install

config:
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(BUILD_DIR)
	@if [ ! -f $(SOURCE_DIR)/configure ] ; then \
                cp $(SOURCE_DIR)/auto/configure $(SOURCE_DIR); \
        fi
	@if [ ! -f $(BUILD_DIR)/configure ] ; then \
		cp $(SOURCE_DIR)/configure $(BUILD_DIR); \
		chmod +x $(BUILD_DIR)/configure; \
		cp -a $(SOURCE_DIR)/auto $(BUILD_DIR); \
		cp -a $(SOURCE_DIR)/src $(BUILD_DIR); \
		cp -a $(SOURCE_DIR)/docs $(BUILD_DIR); \
		cp -a $(SOURCE_DIR)/conf $(BUILD_DIR); \
	fi
	@if [ ! -f $(BUILD_DIR)/Makefile ] ; then \
		cd $(BUILD_DIR); \
		./configure $(CONFIGURE_PARAMS); \
	fi

build: config
	cd $(BUILD_DIR); make

install: build
	cd $(BUILD_DIR); make install

clean:
	@rm -f  $(PREFIX)/sbin/$(LIBNAME)*
	@rm -rf $(BUILD_DIR)
