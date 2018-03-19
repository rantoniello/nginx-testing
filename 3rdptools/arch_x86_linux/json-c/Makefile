LIBNAME:= libjson-c
BUILD_DIR:= $(shell pwd)/build_dir
SOURCE_DIR:= $(TOOLS_DIR)/json-c-0.13.1
PREFIX:= $(INSTALL_DIR)$(CROSS_ARCH)

CONFIGURE_PARAMS= --prefix=${PREFIX}

all: install

config:
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(BUILD_DIR)
	if [ ! -f $(BUILD_DIR)/Makefile ] ; then \
		cd $(BUILD_DIR); \
		$(SOURCE_DIR)/configure $(CONFIGURE_PARAMS); \
	fi

build: config
	cd $(BUILD_DIR); make

install: build
	cd $(BUILD_DIR); make install

clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(PREFIX)/lib/$(LIBNAME)*
	@rm -f $(PREFIX)/lib/pkgconfig/json-c*
	@rm -rf $(PREFIX)/include/json-c
