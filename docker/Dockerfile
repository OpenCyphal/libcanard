# 
# Development environment for libcanard, based on Ubuntu 20.04 Focal
#
# This software is distributed under the terms of the MIT License.
# Copyright (c) 2021 UAVCAN Consortium.
# Author: Kalyan Sriram <kalyan@coderkalyan.com>

FROM ubuntu:focal

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update && apt-get -y upgrade
RUN apt-get -y --no-install-recommends install \
		build-essential cmake gcc-multilib g++-multilib \
		clang-tidy-12 clang-format-12 \
		gcc-avr avr-libc \
		sudo curl git ca-certificates

# borrowed from MAVSDK https://github.com/mavlink/MAVSDK/blob/main/docker/Dockerfile-Ubuntu-20.04
RUN curl -L https://github.com/ncopa/su-exec/archive/dddd1567b7c76365e1e0aac561287975020a8fad.tar.gz | tar xvz && \
	cd su-exec-* && make && mv su-exec /usr/local/bin && cd .. && rm -rf su-exec-*

RUN useradd --shell /bin/bash -u 1001 -c "" -m user

COPY entrypoint.sh /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

WORKDIR "/home/user/libcanard"
