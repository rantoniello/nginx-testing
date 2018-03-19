LIBNAME:= libcurl
BUILD_DIR:= $(shell pwd)/build_dir
SOURCE_DIR:= $(TOOLS_DIR)/curl-7.58.0
PREFIX:= $(INSTALL_DIR)$(CROSS_ARCH)

CONFIGURE_PARAMS= --prefix=${PREFIX} --enable-static=no --with-ssl

all: install

config:
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(BUILD_DIR)
	if [ ! -f $(BUILD_DIR)/Makefile ] ; then \
		cd $(BUILD_DIR); \
		CPPFLAGS="-I$(PREFIX)/include" LDFLAGS="-L$(PREFIX)/lib" \
		$(SOURCE_DIR)/configure $(CONFIGURE_PARAMS); \
	fi

build: config
	cd $(BUILD_DIR); make

install: build
	cd $(BUILD_DIR); make install

clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(PREFIX)/lib/$(LIBNAME)*
	@rm -rf $(PREFIX)/include/curl
	@rm -f $(PREFIX)/bin/curl*
