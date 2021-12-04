#!/usr/bin/env bash

# Utility to use local user, taken from:
# https://github.com/mavlink/MAVSDK/blob/main/docker/entrypoint.sh

# Use LOCAL_USER_ID if passed in at runtime.

if [ -n "${LOCAL_USER_ID}" ]; then
    echo "Starting with UID: $LOCAL_USER_ID"
    usermod -u $LOCAL_USER_ID user
    export HOME=/home/user
    chown -R user:user $HOME

    exec su-exec user "$@"
else
    exec "$@"
fi
