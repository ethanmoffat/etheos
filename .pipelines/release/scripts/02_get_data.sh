#!/usr/bin/env bash

wget "$1" 2>&1 > /dev/null
unzip -o sample-data.zip -d "$2/.."
