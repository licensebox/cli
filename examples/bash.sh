#!/usr/bin/env bash
set -euo pipefail
: "${LICENSEBOX_PUBLIC_KEYS:?Set the public keys JSON path}"
licensebox_args=(--product your-product --keys "$LICENSEBOX_PUBLIC_KEYS" --version 1.0)
if licensebox "${licensebox_args[@]}" require export; then
    your-application export "$@"
else
    exit 1
fi
