#!/usr/bin/env bash
set -euo pipefail

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bench" "$tmp/config"

out="$(XDG_CONFIG_HOME="$tmp/config" QT_QPA_PLATFORM=offscreen timeout 10 "$1" --bench "$tmp/bench" 2>&1)"
n="$(printf '%s\n' "$out" | grep -cE '^tempo até a primeira pintura: [0-9]+ ms$' || true)"

if [ "$n" -ne 1 ]; then
    printf 'esperava 1 medição, obteve %s\n--- saída ---\n%s\n' "$n" "$out"
    exit 1
fi
