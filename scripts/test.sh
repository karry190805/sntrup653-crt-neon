#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
exec bash packages/sntrup653-crt-cortex-a76/run_correctness.sh
