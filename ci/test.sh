#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
cmake --install build --prefix "$PWD/build/install"
python3 tests/integration.py "$PWD/build/install/bin/licensebox"
build/install/bin/licensebox --sdk-version
# Only the executable and license belong to the CLI binary install surface.
test "$(find build/install -type f | wc -l)" -eq 2
sh -n examples/posix.sh
bash -n examples/bash.sh
