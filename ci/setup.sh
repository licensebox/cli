#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y cmake gcc libjson-c-dev python3-cryptography openssl
