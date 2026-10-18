#!/bin/sh
set -eu
: "${LICENSEBOX_PUBLIC_KEYS:?Set the public keys JSON path}"
if licensebox --product your-product --keys "$LICENSEBOX_PUBLIC_KEYS" require export; then
    # Check access before opening or processing user files.
    your-application export "$@"
else
    exit 1
fi
