#!/usr/bin/env sh

dockerimage=libcanard

docker run -it --rm -v $(pwd):/home/user/libcanard:z -e LOCAL_USER_ID=`id -u` $dockerimage "$@"
