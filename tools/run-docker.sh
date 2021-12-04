#!/usr/bin/env sh
# First-time usage: docker build . && docker tag <id> libcanard && ./run-docker.sh

dockerimage=libcanard

docker run -it --rm -v $(pwd):/home/user/libcanard:z -e LOCAL_USER_ID=`id -u` $dockerimage "$@"
