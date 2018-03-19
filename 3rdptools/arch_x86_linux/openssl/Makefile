LIBNAME:= openssl
BUILD_DIR:= $(shell pwd)/build_dir
SOURCE_DIR:= $(TOOLS_DIR)/openssl-1.0.2n
PREFIX:= $(INSTALL_DIR)$(CROSS_ARCH)

CONFIGURE_PARAMS= --prefix=${PREFIX} --openssldir=${PREFIX}
CONFIGURE_PARAMS+= shared
# Disables hardware support (useful on mobile devices)
##CONFIGURE_PARAMS+= no-hw no-engine
# Disables Next Protocol Negotiation (NPN) -will not be used-
CONFIGURE_PARAMS+= no-nextprotoneg

all: install

config:
	mkdir -p $(PREFIX)
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(BUILD_DIR)
	if [ ! -f $(BUILD_DIR)/Makefile ] ; then \
		cp -a $(SOURCE_DIR)/* $(BUILD_DIR); \
		cd $(BUILD_DIR); \
		./config $(CONFIGURE_PARAMS); \
	fi

build: config
	cd $(BUILD_DIR); make

install: build
	if [ ! -f $(PREFIX)/lib/libssl.so ] ; then cd $(BUILD_DIR); make install; fi

clean:
	rm -rf $(BUILD_DIR)
	@rm -rf $(LIB_DIR)/libcrypto* $(LIB_DIR)/libssl* $(LIB_DIR)/pkgconfig/libcrypto* $(LIB_DIR)/pkgconfig/libssl* $(LIB_DIR)/pkgconfig/$(LIBNAME)*
	@rm -rf $(INCLUDE_DIR)/$(LIBNAME)
	@rm -f $(INSTALL_DIR)/bin/$(LIBNAME) $(INSTALL_DIR)/bin/c_rehash
	@rm -rf $(INSTALL_DIR)/ssl
