#!/usr/bin/env bash

gzip -f -d $1/image.tar.gz
docker load -i $1/image.tar
