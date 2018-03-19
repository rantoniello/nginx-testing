
#### GLOBAL DEFINITIONS #####
# Get **THIS** Makefile path:
# As make reads various makefiles, including any obtained from the MAKEFILES 
# variable, the command line, the default files, or from include directives, 
# their names will be automatically appended to the MAKEFILE_LIST variable. 
# They are added right before make begins to parse them. This means that if 
# the first thing a makefile does is examine the last word in this variable, 
# it will be the name of the current makefile. Once the current makefile has 
# used include, however, the last word will be the just-included makefile.
CONFIG_LINUX_MK_PATH_= $(shell pwd)/$(dir $(lastword $(MAKEFILE_LIST)))
CONFIG_LINUX_MK_PATH= $(shell readlink -f $(CONFIG_LINUX_MK_PATH_))
3RDPTOOLS= 3rdptools
3RDPTOOLS_DIR= $(CONFIG_LINUX_MK_PATH)
3RDP_PREFIX=$(CONFIG_LINUX_MK_PATH)/_install_dir_

##### Define scripting #####
SHELL:=/bin/bash

##### Get platform host #####
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	ARCHS=x86 # //... arm mips...
	SO_NAME=linux
endif
#ifeq ($(UNAME_S),Darwin) # e.g. use to add Darwing support
#	ARCHS= ... 
#	SO_NAME=darwin
#endif
export SO_NAME

##### BASIC COMMON CFLAGS #####
CFLAGS+=-Wall -O3
#CFLAGS+=-Wall -g -O0
LBITS := $(shell getconf LONG_BIT)
ifeq ($(LBITS),64)
   # 64 bit stuff here
   CFLAGS+=-fPIC -D_LARGE_FILE_API -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
else
   # 32 bit stuff here
endif
export CFLAGS

.PHONY : $(3RDPTOOLS)

#### Rules ####

# Make every target of the form '_<ARCHS array entry>'
# (Note: Substitution function '$(ARCHS:x=y)' substitutes very 'x' 
# pattern by replacement 'y')
all: $(ARCHS:=_)

# The MAKE and CLEAN macros:
# 1) $* will be substituted by each entry of the array ARCHS (e.g x86, ...)
# 2) _libname is a tag to be substituted by each library name 
# (e.g. ffmpeg, lame, ...) in the target
MAKE_MACRO__= $(MAKE) -C arch_$*_$(SO_NAME)/libname__ all CROSS_ARCH=$* \
	INSTALL_DIR=$(3RDP_PREFIX) TOOLS_DIR=$(3RDPTOOLS_DIR)
MAKE_MACRO= $(MAKE_MACRO__:libname__=$(patsubst $*_%,%,$@))
CLEAN_MACRO__= $(MAKE) -C arch_$*_$(SO_NAME)/libname__ clean CROSS_ARCH=$* \
	INSTALL_DIR=$(3RDP_PREFIX) TOOLS_DIR=$(3RDPTOOLS_DIR)
CLEAN_MACRO= $(CLEAN_MACRO__:libname__=$(patsubst $*_%_clean,%,$@))

# NOTE: 
# ’%_’ below is used to write target with wildcards (e.g. ‘%_’ matches all 
# targets that have that substring in ‘$(ARCHS:=_)’)
# ‘$*’ takes exactly the value of the substring matched by ‘%’ in the 
# correspondent target itself. 
%_:
	make $*_json-c
	make $*_zlib
	make $*_openssl
	make $*_pcre
	make $*_nginx
	make $*_mongoose
	make $*_curl
	make $*_utils
	make $*_examples

# To compile library "nginx" standalone: "make x86_nginx".
# To clean library "nginx" standalone: "make x86_nginx_clean".
# The same applies to the ALL the libraries below (e.g. "make x86_mylib", 
# "make x86_mylib_clean", ...)

%_json-c \
%_zlib \
%_examples \
%_utils \
%_mongoose \
%_curl \
%_pcre \
%_openssl \
%_nginx:
	$(MAKE_MACRO)

clean: $(ARCHS:=_clean)

%_clean:
	make $*_nginx_clean
	make $*_openssl_clean
	make $*_pcre_clean
	make $*_mongoose_clean
	make $*_curl_clean
	make $*_utils_clean
	make $*_examples_clean
	make $*_zlib_clean
	make $*_json-c_clean
	rm -rf *~ *.log *.log*
	rm -rf $(3RDP_PREFIX)*

%_json-c_clean \
%_zlib_clean \
%_examples_clean \
%_utils_clean \
%_mongoose_clean \
%_curl_clean \
%_pcre_clean \
%_openssl_clean \
%_nginx_clean:
	$(CLEAN_MACRO)
