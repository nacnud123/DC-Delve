#!/bin/bash
set -e
KOS_ENV="${KOS_BASE:-/opt/toolchains/dc/kos}/environ.sh"
if [ ! -f "$KOS_ENV" ]; then
    echo "ERROR: KOS environ.sh not found at $KOS_ENV"
    exit 1
fi
source "$KOS_ENV"
make "$@"
