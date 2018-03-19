##### Define scripting #####
SHELL:=/bin/bash

#### Dirs definitions #####
THIS_MAKEFILE_DIR_= $(shell pwd)/$(dir $(lastword $(MAKEFILE_LIST)))
THIS_MAKEFILE_DIR = $(shell readlink -f $(THIS_MAKEFILE_DIR_))
BUILD_DIR = $(THIS_MAKEFILE_DIR)/build
NGINX_SBIN_PATH = /usr/sbin
NGINX_SRC_DIR = /opt/p2pcdn/nginx/nginx-1.10.2
WORKSPACE_DIR = $(THIS_MAKEFILE_DIR)
NGINX_MODULES_SRC_PATH = $(THIS_MAKEFILE_DIR)/src/rpm/SOURCES/modules

#### Dynamic modules directories full-path CSV ####
#
# All dynamic modules sources to be added to compilation should be indicated by 
# providing their directories full-paths in a comma-separated-value (CSV) 
# formatted definition. For example (Note we use makefile simply-expanded 
# variable to avoid white spaces in the CSV):
# NGINX_MODS_SRC_DIRS_CSV:=/my/path1/MODNAME1
# NGINX_MODS_SRC_DIRS_CSV:=$(NGINX_MODS_SRC_DIRS_CSV),/my/path2/MODNAME2
# NGINX_MODS_SRC_DIRS_CSV:=$(NGINX_MODS_SRC_DIRS_CSV),...
#
# Directory nomenclature:
# Each module directory name *MUST* be the same as the module name, for example:
# 'ngx_http_MODNAME_module' source must be available at any path of the form 
# /any/path/MODNAME 
# (compilation will generate a dynamic lib named 'ngx_http_MODNAME_module.so').
NGINX_MODS_SRC_DIRS_CSV:=$(NGINX_MODULES_SRC_PATH)/hello_world
#NGINX_MODS_SRC_DIRS_CSV:=$(NGINX_MODS_SRC_DIRS_CSV),$(NGINX_MODULES_SRC_PATH)/tcdn_webcache

#### Other defs #####
NAME=cdn-nginx-modules
VERSION=$(shell dp_version.sh 2>/dev/null)
RELEASE=$(shell dp_release.sh 2>/dev/null)
OS_RELEASE=$(shell dp_os_release.sh 2>/dev/null)
PROJECT_NAME=$(shell dp_project_name.sh)
STABLE_BUILD=$(shell dp_is_stable_build.sh)
CI_SERVER=$(shell dp_ci-server.sh)
METRICS_FILE=sonar-project.properties

DP:
	@if [ "$(shell which dp_version.sh)" != "" ]; then \
	   exit 0;\
	else \
	   if [ ! -f ~/pipeline_plugin/dp_version.sh ]; then \
	     cd ;\
	     curl http://cdn-jenkins-server.cdn.hi.inet/develenv/repositories/artifacts/dp.tar.gz 2>/dev/null|tar xfz - ;\
	   fi; \
	   printf "\\033[47m\\033[1;31m[ERROR] Add deployment pipeline software to the path\\033[0m\n"; \
	   printf "\\033[47m\\033[1;31m[ERROR] export PATH=~/pipeline_plugin:\$$PATH\\033[0m\n"; \
	   exit 1; \
	fi

rpm: DP
	dp_package.sh;

sonar-config: $(METRICS_FILE)
$(METRICS_FILE):
	@echo -e "\nsonar.lang.patterns.c=**/*.c,**/*.h\
\nsonar.lang.patterns.c++=**/*.cxx,**/*.cpp,**/*.cc,**/*.hxx,**/*.hpp,**/*.hh\
\nsonar.sources=src/rpm/SOURCES/modules/hello_world\
">$(METRICS_FILE)

codemetrics: sonar-config
	dp_metrics.sh

buildci: compile codemetrics rpm

compile:
	@echo "creating build folder and compiling"
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/tmp
	@$(WORKSPACE_DIR)/scripts/build_nginx_modules.sh \
	--workspace-path=$(WORKSPACE_DIR) --build-dir=$(BUILD_DIR) --nginx-sbin-path=$(NGINX_SBIN_PATH) \
	--nginx-src-dir=$(NGINX_SRC_DIR) --nginx-modules-src-dirs=$(NGINX_MODS_SRC_DIRS_CSV) \
	|| (echo "Nginx building failed (code $$?)"; exit 1)

clean:
	@rm -rf $(BUILD_DIR)

deployci: buildci rpm

# Rule '3rdptools' is though to be used for compiling locally in a PC 
# environment (e.g. developing purposes).
.PHONY : 3rdptools
3rdptools:
	# We compile all dependencies locally, inside '3rdptools' folder.
	# (Note that *no* installation is performed in the user system)
	make -C 3rdptools
	# We point sbin and source variables to our locally compiled versions
	$(eval NGINX_SBIN_PATH := $(THIS_MAKEFILE_DIR)/3rdptools/_install_dir_x86/sbin)
	$(eval NGINX_SRC_DIR := $(THIS_MAKEFILE_DIR)/3rdptools/nginx-1.10.2)
	make -f Makefile compile LD_LIBRARY_PATH=$(THIS_MAKEFILE_DIR)/3rdptools/_install_dir_x86/lib \
	NGINX_SRC_DIR=$(NGINX_SRC_DIR) NGINX_SBIN_PATH=$(NGINX_SBIN_PATH)

3rdptools_clean:
	make -C 3rdptools clean
