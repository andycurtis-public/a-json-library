#!/usr/bin/env bash
set -euxo pipefail

rm -rf build
mkdir -p build
cd build
cmake .. "$@"
make -j"$(nproc)"
./test_ajson
cd ..
