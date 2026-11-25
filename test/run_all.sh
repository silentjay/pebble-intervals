#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
test/run_c_tests.sh
node --test test/
