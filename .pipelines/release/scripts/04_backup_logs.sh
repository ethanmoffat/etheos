#!/usr/bin/env bash

var_logdir="$1"
var_configdir="$2"

localdate=$(date +%Y%m%d.%H%M%S)
var_destdir="$3/$localdate"

mkdir -p $var_destdir

sudo test -f $var_logdir/stdout && sudo mv $var_logdir/stdout $var_destdir
sudo test -f $var_logdir/stderr && sudo mv $var_logdir/stderr $var_destdir
sudo test -f $var_configdir/world.bak.json && sudo cp $var_configdir/world.bak.json $var_destdir
