#!/bin/bash

THIS_SCRIPT_DIR="$(cd "$(readlink -f $(dirname "${BASH_SOURCE[0]}"))"&& pwd)"
ARCH_DEFAULT='x86'
ARCH=$ARCH_DEFAULT
CURR_DIR=`pwd`
WS_DIR="$THIS_SCRIPT_DIR/.."
BUILD_DIR="$THIS_SCRIPT_DIR/../build"
NGINX_SBIN_DIR="/usr/sbin"
NGINX_SRC_DIR="/opt/p2pcdn/nginx/nginx-1.10.2"

#### Get configurations from input arguments if specified.
echo ""
echo "Checking arguments ..."
while test $# -gt 0
do
    case "$1" in
        *)
        #echo "argument $1" #testing purposes
		# Check arguments for system architecture 
        if [[ $1 = "--sys-arch="* ]]; then
        	ARCH="${1#*=}"
        fi
		# Check arguments for workspace path 
	    if [[ $1 = "--workspace-path="* ]]; then
        	WS_DIR="${1#*=}"
        fi
		# Check arguments for build dir 
	    if [[ $1 = "--build-dir="* ]]; then
        	BUILD_DIR="${1#*=}"
        fi
    	# Check arguments for nginx binary (executable) path 
		if [[ $1 = "--nginx-sbin-path="* ]]; then
        	NGINX_SBIN_DIR="${1#*=}"
        fi
		# Check arguments for nginx source directory path 
		if [[ $1 = "--nginx-src-dir="* ]]; then
        	NGINX_SRC_DIR="${1#*=}"
        fi
		# Check arguments for nginx modules source directory paths. 
		# Each path will be set comma separated (as a CSV)
		if [[ $1 = "--nginx-modules-src-dirs="* ]]; then
        	NGINX_MODS_SRC_DIRS_CSV="${1#*=}"
        fi
            ;;
    esac
    shift
done
echo "Using architecture \"$ARCH\"."
# Better to set absolute path in our variables to operate securely
WS_DIR="$(readlink -f $WS_DIR)"
echo "Workspace path is \"$WS_DIR\"."
BUILD_DIR="$(readlink -f $BUILD_DIR)"
echo "Building dir is \"$BUILD_DIR\"."
NGINX_SBIN_DIR="$(readlink -f $NGINX_SBIN_DIR)"
echo "Nginx sbin path is \"$NGINX_SBIN_DIR\"."
NGINX_SRC_DIR="$(readlink -f $NGINX_SRC_DIR)"
echo "Nginx source path is \"$NGINX_SRC_DIR\"."
# Check that given dirs exist...
if [[ ! -d "$WS_DIR" ]] || [[ ! -d "$BUILD_DIR" ]] || [[ ! -d "$NGINX_SBIN_DIR" ]] || [[ ! -d "$NGINX_SRC_DIR" ]]; then
	echo "... error: a not valid path was given (please review paths)."
	exit 1
fi
# Check Nginx source dir... (we check only some folders existence)
if [[ ! -d "$NGINX_SRC_DIR/auto" ]] || [[ ! -d "$NGINX_SRC_DIR/conf" ]] || [[ ! -d "$NGINX_SRC_DIR/contrib" ]]; then
	echo "... error: directory \"$NGINX_SRC_DIR/...\" does not seem to be Nginx directory."
	exit 1
fi
echo ""

#### Get nginx cflags
echo "Getting nginx configured CFLAGS..."
NGINX_CFLAGS="$($NGINX_SBIN_DIR/nginx -V 2>&1)"
#echo "${NGINX_CFLAGS}" #testing purposes
if [[ $NGINX_CFLAGS =~ "configure"[[:space:]]"arguments:" ]]; then
	NGINX_CFLAGS="${NGINX_CFLAGS#*configure arguments:}"
	echo "Specified nginx cflags are '$NGINX_CFLAGS'"
else
	echo "... error occurred. Cannot access/perform \"$NGINX_SBIN_DIR/nginx -V\"."
	echo "The following Nginx flags have been parsed ($NGINX_SBIN_DIR/nginx -V): '$NGINX_CFLAGS'"
	exit 1
fi
echo ""

#### Create temporary installation dir to operate and compile modules
echo "Creating temporary compilation dir at \"$BUILD_DIR/tmp\" (if applicable)"
mkdir -p $BUILD_DIR/tmp
echo ""

#### Copy nginx source to temporal, configure and compile modules...
echo "Configuring and compiling given modules..."
echo "cp -a $NGINX_SRC_DIR $BUILD_DIR/tmp"
cp -a $NGINX_SRC_DIR $BUILD_DIR/tmp
NGINX_SRCTMP_DIR=$BUILD_DIR/tmp/$(basename $NGINX_SRC_DIR)
echo "cd $NGINX_SRCTMP_DIR"
cd $NGINX_SRCTMP_DIR
# Substitute cvs for cflags accordingly
NGINX_ADD_DYN_MOD_FLAG=" --add-dynamic-module="
NGINX_MODS_SRC_DIRS_CFLAG=${NGINX_MODS_SRC_DIRS_CSV//,/$NGINX_ADD_DYN_MOD_FLAG}
NGINX_MODS_SRC_DIRS_CFLAG=$NGINX_ADD_DYN_MOD_FLAG$NGINX_MODS_SRC_DIRS_CFLAG
# We print a Makefile to avoid going around with quotings in bash...
printf "config:\n\t./configure$NGINX_MODS_SRC_DIRS_CFLAG$NGINX_CFLAGS" > $NGINX_SRCTMP_DIR/config.mk
# make configuration
make -f config.mk config
# make the modules- this will put the *.so in ./obj
make modules
echo ""

#### Copy generated .so's to build dir
LIBCNT=`ls -1 "$NGINX_SRCTMP_DIR"/objs/*.so 2>/dev/null | wc -l`
if [[ -n $LIBCNT ]] && [[ $LIBCNT -gt "0" ]]; then
	echo "Installing generated .so's to \"$BUILD_DIR\""
else
	echo "... error in compilation (no .so generated)."
	exit 1;
fi
IFS=$','
for i in $NGINX_MODS_SRC_DIRS_CSV; do 
	MODULE_PATH="$NGINX_SRCTMP_DIR"/objs/ngx_http_$(basename $i)_module.so
	if [[ -f "$MODULE_PATH" ]] ; then
		echo "cp "$MODULE_PATH" $BUILD_DIR"
		cp "$MODULE_PATH" "$BUILD_DIR"
	else
		echo "Error: the specified dynamic module "$MODULE_PATH" does not exist" 
		exit 1;
	fi
done
unset IFS
echo ""

#### Clean temporary nginx source folder
cd $NGINX_SRCTMP_DIR
make clean >/dev/null 2>/dev/null

#### We did it OK
echo "O.K."
exit 0
