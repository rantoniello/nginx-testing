LIBNAME:= libz
BUILD_DIR:= $(shell pwd)/build_dir
SOURCE_DIR:= $(TOOLS_DIR)/zlib-1.2.11
PREFIX:= $(INSTALL_DIR)$(CROSS_ARCH)

CONFIGURE_PARAMS= --prefix=${PREFIX} --libdir=$(PREFIX)/lib --sharedlibdir=$(PREFIX)/lib --includedir=$(PREFIX)/include

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
	@rm -f $(PREFIX)/lib/pkgconfig/zlib*
	@rm -f $(PREFIX)/include/zlib* $(PREFIX)/include/zconf*
