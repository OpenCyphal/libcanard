#!/usr/bin/env bash

MIN_CLANG_FORMAT_VERSION=9

die()
{
    echo "$@" >&2
    exit 1
}

command -v clang-format > /dev/null || die "clang-format not found"
clang_format_version=$(clang-format --version | sed -ne 's/[^0-9]*\([0-9]*\)\..*/\1/p')
[ "$clang_format_version" -ge $MIN_CLANG_FORMAT_VERSION ] || \
    die "clang-format v$MIN_CLANG_FORMAT_VERSION+ required; found v$clang_format_version"

all_source_files="canard.c canard.h $(find tests -name 'test_*.cpp')"
echo "Source files that will be auto-formatted:"
for i in $all_source_files; do echo "    $i"; done

# shellcheck disable=SC2086
clang-format -i -fallback-style=none -style=file $all_source_files || die "clang-format has failed"
