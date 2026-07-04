#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

"$repo_root/scripts/build-rss-mockup.sh"
"$repo_root/scripts/deploy.sh" "$repo_root/examples/rss-reader-mockup/build/rss-reader-mockup.app" "$@"
