#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-/opt/esp32-sos}"
BACKUP_DIR="${2:-/opt/backups/esp32-sos}"
RETENTION_DAYS="${RETENTION_DAYS:-30}"

if [[ ! -d "$ROOT_DIR/runtime" ]]; then
  echo "runtime directory not found: $ROOT_DIR/runtime" >&2
  exit 1
fi

umask 077
mkdir -p "$BACKUP_DIR"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
tmp="$BACKUP_DIR/.esp32-sos-$stamp.tar.gz.tmp"
archive="$BACKUP_DIR/esp32-sos-$stamp.tar.gz"

tar -C "$ROOT_DIR" --ignore-failed-read -czf "$tmp" \
  runtime certs .env docker-compose.yml
mv "$tmp" "$archive"

find "$BACKUP_DIR" -type f -name 'esp32-sos-*.tar.gz' \
  -mtime "+$RETENTION_DAYS" -delete

echo "created $archive"
