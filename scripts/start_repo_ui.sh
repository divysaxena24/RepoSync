#!/usr/bin/env bash
# MiniDrop Single-User Remote Repo & Smart Action Button Web Dashboard Launcher

PORT=8080
LOCAL_DIR="./workspace"
REMOTE_DIR="./storage/repo"

mkdir -p "$LOCAL_DIR" "$REMOTE_DIR"

echo "========================================================="
echo "   Starting MiniDrop Single-User Remote Repository UI    "
echo "========================================================="
echo "Local Workspace:   $LOCAL_DIR"
echo "Remote Repository: $REMOTE_DIR"
echo "Web URL:           http://localhost:$PORT"
echo "========================================================="

./bin/minidrop web --port "$PORT" --local "$LOCAL_DIR" --remote "$REMOTE_DIR"

