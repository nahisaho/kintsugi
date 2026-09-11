#!/usr/bin/env bash
# Builds and runs the full C++ test suite. Used as the general "commands"
# gate check (musubix3 gate / evidence refresh), independent of the
# single-test-scoped TDD wrapper (scripts/run_tdd_test.sh).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" --target pottery_tests -j"$(nproc)"
exec "$BUILD_DIR/pottery_tests"
