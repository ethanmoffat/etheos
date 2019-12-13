#!/usr/bin/env bash

set -e

HELP=false
DEBUG=false
CLEAN=false
BUILDDIR=build

function parse_options {
  while [[ $# -gt 0 ]]
  do
    OPTION="${1}"
    case $OPTION in
      -d|--debug)           DEBUG=true        ;;
      -c|--clean)           CLEAN=true        ;;
      -b|--builddir)        BUILDDIR=$2       ; shift ;;
      *)                    HELP=true         ; break ;;
    esac
    shift
  done
}

parse_options "$@"

if [ "$HELP" == "true" ]; then
    echo "Options available for build: "
    echo "  -d|--debug             Build in debug mode (defaults to release)"
    echo "  -c|--clean             Clean before building"
    echo "  -b|--builddir <value>  Specify the build directory for the cmake build files"

    exit 0
fi

if [ -z "$BUILDDIR" ]; then
    echo "-b/--builddir is required"
    exit -1
fi

if [ "$CLEAN" == "true" ]; then
    if [ -d "$BUILDDIR" ]; then
        rm -rf $BUILDDIR
    fi
fi

if [ ! -d "$BUILDDIR" ]; then
    mkdir -p $BUILDDIR
fi

if [ ! -d "install" ]; then
    mkdir install
fi

pushd $BUILDDIR

BUILDMODEARG=Release
if [ "$DEBUG" == "true" ]; then
    BUILDMODEARG=Debug
fi

cmake -DEOSERV_WANT_SQLSERVER=ON -G "Unix Makefiles" ..
cmake --build . --config $BUILDMODEARG --target install --

popd
