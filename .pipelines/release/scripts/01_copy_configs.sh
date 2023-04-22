#!/usr/bin/env bash

if (( $EUID != 0 )); then
    >&2 echo "This script must be run as root. Please run using sudo."
    exit -1
fi

rsync -a --exclude=.git $1/* $2
find $2 -type d -exec chmod 775 "{}" \;
find $2 -type f -exec chmod 664 "{}" \;
chown -R etheos:etheos $2
