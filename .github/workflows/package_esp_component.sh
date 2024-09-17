#!/bin/bash
set -e

# This script must be called from the root of this repository.
# It packages up libcanard as an ESP-IDF component in package/libcanard.

mkdir -p package/libcanard/include

cp CONTRIBUTING.md                  package/libcanard/CONTRIBUTING.md
cp LICENSE                          package/libcanard/LICENSE
cp README.md                        package/libcanard/README.md

cp libcanard/canard.c               package/libcanard/canard.c
cp libcanard/_canard_cavl.h         package/libcanard/_canard_cavl.h
cp libcanard/canard.h               package/libcanard/include/canard.h

cp esp_metadata/CMakeLists.txt      package/libcanard/CMakeLists.txt
cp esp_metadata/Kconfig             package/libcanard/Kconfig
cp esp_metadata/idf_component.yml   package/libcanard/idf_component.yml

# Install compote, a tool for uploading ESP-IDF components.
python3 -m pip install --upgrade idf-component-manager

echo "Successfully packaged ESP component into package/libcanard:"
find package/libcanard
echo
