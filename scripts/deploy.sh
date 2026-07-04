#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
default_app="$repo_root/examples/hello-inkview/build/hello-inkview.app"

usage() {
  cat <<EOF
Usage: $0 [app-file] [applications-dir]

Builds and deploys a PocketBook .app file to a USB-mounted PocketBook.

Defaults:
  app-file:         examples/hello-inkview/build/hello-inkview.app
  applications-dir: auto-detected, or POCKETBOOK_APPS_DIR if set

If app-file is omitted, the hello-inkview example is built before deploy.
If applications-dir points at the mount root and contains an applications/
subdirectory, that subdirectory is used automatically.
EOF
}

mount_pocketbook_devices() {
  if ! command -v udisksctl >/dev/null 2>&1; then
    return 0
  fi

  for dev in /dev/disk/by-label/PB* /dev/disk/by-id/*PocketBook*; do
    if [ ! -e "$dev" ]; then
      continue
    fi

    real_dev=$(readlink -f "$dev")
    if findmnt -rn -S "$real_dev" >/dev/null 2>&1; then
      continue
    fi

    udisksctl mount -b "$real_dev" >/dev/null 2>&1 || true
  done
}

find_applications_dir() {
  mount_pocketbook_devices

  {
    for base in \
      "/run/media/$(id -un)" \
      /run/media \
      "/media/$(id -un)" \
      /media \
      /mnt
    do
      if [ -d "$base" ]; then
        find "$base" -maxdepth 6 -type d -name applications -writable 2>/dev/null
      fi
    done
  } | awk '!seen[$0]++' | grep -v "^$repo_root/" || true
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

app_path=${1:-$default_app}
apps_dir=${2:-${POCKETBOOK_APPS_DIR:-}}

if [ "$app_path" = "$default_app" ] && [ "${POCKETBOOK_SKIP_BUILD:-0}" != "1" ]; then
  "$repo_root/scripts/build-hello.sh"
fi

if [ ! -f "$app_path" ]; then
  echo "App file not found: $app_path" >&2
  exit 1
fi

if [ -n "$apps_dir" ] && [ -d "$apps_dir/applications" ]; then
  apps_dir="$apps_dir/applications"
fi

if [ -z "$apps_dir" ]; then
  candidates=$(find_applications_dir)
  count=$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l | tr -d ' ')

  if [ "$count" -eq 0 ]; then
    echo "Could not find a writable PocketBook applications directory." >&2
    echo "Connect the device in PC-link mode, or pass the directory explicitly:" >&2
    echo "  $0 $app_path /path/to/PocketBook/applications" >&2
    exit 1
  fi

  if [ "$count" -gt 1 ]; then
    echo "Found multiple applications directories; pass one explicitly:" >&2
    printf '%s\n' "$candidates" >&2
    exit 1
  fi

  apps_dir=$candidates
fi

if [ ! -d "$apps_dir" ]; then
  echo "Applications directory does not exist: $apps_dir" >&2
  exit 1
fi

if [ ! -w "$apps_dir" ]; then
  echo "Applications directory is not writable: $apps_dir" >&2
  exit 1
fi

dest="$apps_dir/$(basename -- "$app_path")"

if [ -f "$dest" ]; then
  if cmp -s "$app_path" "$dest"; then
    echo "Already up to date: $dest"
    exit 0
  fi

  backup="$dest.backup-$(date +%Y%m%d-%H%M%S)"
  cp -p "$dest" "$backup"
  echo "Backed up existing app: $backup"
fi

cp "$app_path" "$dest"
chmod 755 "$dest" 2>/dev/null || true
sync "$dest" 2>/dev/null || sync

cat <<EOF
Deployed: $dest

Next steps:
1. Eject/unmount the PocketBook cleanly.
2. Open the Applications menu on the device.
3. Reboot the device if the app does not appear immediately.
EOF
