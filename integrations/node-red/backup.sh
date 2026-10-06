#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-/opt/esp32-sos}"
BACKUP_DIR="${2:-/opt/backups/esp32-sos}"
RETENTION_DAYS="${RETENTION_DAYS:-30}"
AGE_RECIPIENT_FILE="${AGE_RECIPIENT_FILE:-$ROOT_DIR/backup.age-recipient}"

for required in "$ROOT_DIR/runtime" "$ROOT_DIR/certs/emqxsl-ca.crt" \
  "$ROOT_DIR/.env" "$ROOT_DIR/docker-compose.yml" "$AGE_RECIPIENT_FILE"; do
  if [[ ! -e "$required" ]]; then
    echo "required backup input not found: $required" >&2
    exit 1
  fi
done

if ! command -v age >/dev/null 2>&1; then
  echo "age is required; install age before running this backup" >&2
  exit 1
fi

umask 077
mkdir -p "$BACKUP_DIR"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
tmp="$BACKUP_DIR/.esp32-sos-$stamp.tar.gz.age.tmp"
archive="$BACKUP_DIR/esp32-sos-$stamp.tar.gz.age"

tar -C "$ROOT_DIR" -czf - runtime certs .env docker-compose.yml \
  | age -R "$AGE_RECIPIENT_FILE" > "$tmp"
test -s "$tmp"
mv "$tmp" "$archive"

find "$BACKUP_DIR" -type f -name 'esp32-sos-*.tar.gz.age' \
  -mtime "+$RETENTION_DAYS" -delete

echo "created $archive"
