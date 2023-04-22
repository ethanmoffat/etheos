#!/usr/bin/env bash

var_configdir="$1"
var_password="$2"
var_passfile="$var_configdir/dbpass"

sudo rm "$var_passfile"
echo "$var_password" > $var_passfile

sudo chmod 400 $var_passfile
sudo chown etheos:etheos $var_passfile
