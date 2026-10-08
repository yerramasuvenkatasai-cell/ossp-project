#!/bin/bash
# Quick start: build (if needed) and run the server.
# Usage: ./run.sh [port] [workers]
set -e
cd "$(dirname "$0")"

if [ ! -x ./server ]; then
    echo "Building..."
    make
fi

mkdir -p shared
echo "Sharing files from: $(pwd)/shared"
ls shared

PORT="${1:-8080}"
WORKERS="${2:-8}"
exec ./server "$PORT" "$WORKERS"
