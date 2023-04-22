#!/usr/bin/env bash

build_number="$1"

var_name="$2"
var_image="$3:$build_number"

var_port=8078

var_logdir="$4"
var_configdir="$5"
var_datadir="$6"

echo "build number path :: $build_number_path"
echo "build number :: $build_number"
echo "name :: $var_name"
echo "image :: $var_image"
echo "log dir :: $var_logdir"
echo "config dir :: $var_configdir"
echo "data dir :: $var_datadir"
echo "port :: $var_port"

if [[ ! -d $var_logdir ]]; then
mkdir -p $var_logdir
fi

if [[ ! -d $var_datadir ]]; then
mkdir -p $var_datadir
fi

docker run --name $var_name -d -p $var_port:$var_port -v $var_configdir:/etheos/config_local -v $var_datadir:/etheos/data -v $var_logdir:/etheos/logs $var_image

docker image prune --force
