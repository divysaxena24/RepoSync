#!/usr/bin/env bash
# MiniDrop Environment Setup Script

set -e

echo "[MiniDrop Setup] Verifying system dependencies..."

command -v gcc >/dev/null 2>&1 || { echo >&2 "[ERROR] GCC is not installed. Aborting."; exit 1; }
command -v make >/dev/null 2>&1 || { echo >&2 "[ERROR] Make is not installed. Aborting."; exit 1; }
command -v openssl >/dev/null 2>&1 || { echo >&2 "[ERROR] OpenSSL is not installed. Aborting."; exit 1; }

echo "[MiniDrop Setup] Dependencies verified: gcc, make, openssl available."

echo "[MiniDrop Setup] Creating directory structure..."
mkdir -p storage/clients
mkdir -p logs
mkdir -p build/client
mkdir -p build/server
mkdir -p build/common
mkdir -p build/tests

echo "[MiniDrop Setup] Done. Workspace is ready for compilation."

