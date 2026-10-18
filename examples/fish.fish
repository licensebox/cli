#!/usr/bin/env fish
if not set -q LICENSEBOX_PUBLIC_KEYS
    echo 'Set LICENSEBOX_PUBLIC_KEYS to the public keys JSON path' >&2
    exit 2
end
if licensebox --product your-product --keys "$LICENSEBOX_PUBLIC_KEYS" require export
    your-application export $argv
else
    exit 1
end
