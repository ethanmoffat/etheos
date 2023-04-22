#!/usr/bin/env bash

existing=$(docker ps -af "name=$1" -q)
if [[ ! -z "$existing" ]]; then
    echo "Stopping existing deployment..."
    docker stop "$existing" &> /dev/null
    docker rm -v "$existing" &> /dev/null
else
    echo "No existing deployment was found"
fi
