#!/usr/bin/env bash
# Convenience script to start MiniDrop Server

PORT=8080
STORAGE="./storage/clients"
TOKEN="minidrop_secret_key"

mkdir -p "$STORAGE"

echo "[MiniDrop] Starting server on port $PORT..."
./bin/minidrop-server --port "$PORT" --storage "$STORAGE" --token "$TOKEN" --verbose

