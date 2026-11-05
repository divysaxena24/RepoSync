#!/usr/bin/env bash
# Convenience script to start MiniDrop Client

SERVER="127.0.0.1"
PORT=8080
CLIENT_ID="client01"
SYNC_DIR="./sync_folder"
TOKEN="minidrop_secret_key"

mkdir -p "$SYNC_DIR"

echo "[MiniDrop] Starting client '$CLIENT_ID' watching '$SYNC_DIR'..."
./bin/minidrop-client --server "$SERVER" --port "$PORT" --client-id "$CLIENT_ID" \
                      --directory "$SYNC_DIR" --token "$TOKEN" --verbose

