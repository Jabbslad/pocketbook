#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

docker run --rm \
  --user "$(id -u):$(id -g)" \
  --mount type=bind,source="$repo_root",target=/project \
  5keeve/pocketbook-sdk:6.3.0-b288-v1 \
  -c 'cd /project/examples/rss-reader-mockup && cmake -S . -B build && cmake --build build'

printf '\nBuilt: %s\n' "$repo_root/examples/rss-reader-mockup/build/rss-reader-mockup.app"
