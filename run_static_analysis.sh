#!/usr/bin/env bash

MIN_CLANG_TIDY_VERSION=9

die()
{
    echo "$@" >&2
    exit 1
}

command -v clang-tidy > /dev/null || die "clang-tidy not found"
clang_tidy_version=$(clang-tidy --version | sed -ne 's/[^0-9]*\([0-9]*\)\..*/\1/p')
[ "$clang_tidy_version" -ge $MIN_CLANG_TIDY_VERSION ] || \
    die "clang-tidy v$MIN_CLANG_TIDY_VERSION+ required; found v$clang_tidy_version"

set -e
clang-tidy canard.c
