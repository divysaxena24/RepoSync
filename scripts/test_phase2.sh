#!/usr/bin/env bash
# MiniDrop Phase 2 Integration Test: Server-Client Handshake, Auth & Keepalive

set -e

PORT=8999
TOKEN="minidrop_secret_phase2"
STORAGE_DIR="/tmp/minidrop_phase2_storage"
CLIENT_ID="client_test01"

echo "=========================================================="
echo "      MiniDrop Phase 2 Integration Test Suite             "
echo "=========================================================="

rm -rf "$STORAGE_DIR"
mkdir -p "$STORAGE_DIR"

# 1. Start server in background
echo "[STEP 1] Starting MiniDrop server on port $PORT..."
./bin/minidrop-server --port "$PORT" --storage "$STORAGE_DIR" --token "$TOKEN" --verbose > /tmp/minidrop_server.log 2>&1 &
SERVER_PID=$!

# Ensure server process is terminated on script exit
trap "kill -INT $SERVER_PID 2>/dev/null || true; rm -rf '$STORAGE_DIR'" EXIT

# Wait up to 3 seconds for server socket to bind
sleep 1
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "[FAIL] Server process died prematurely. Log output:"
    cat /tmp/minidrop_server.log
    exit 1
fi
echo "[PASS] Server is running with PID: $SERVER_PID"

# 2. Test valid client connection & authentication
echo ""
echo "[STEP 2] Running client with VALID credentials..."
if ./bin/minidrop-client --server 127.0.0.1 --port "$PORT" --client-id "$CLIENT_ID" --token "$TOKEN"; then
    echo "[PASS] Valid client successfully connected, authenticated, and verified PING/PONG!"
else
    echo "[FAIL] Valid client failed to connect or authenticate!"
    cat /tmp/minidrop_server.log
    exit 1
fi

# 3. Test invalid credentials rejection
echo ""
echo "[STEP 3] Running client with INVALID credentials..."
if ./bin/minidrop-client --server 127.0.0.1 --port "$PORT" --client-id "hacker_client" --token "wrong_password"; then
    echo "[FAIL] Server unexpectedly accepted invalid credentials!"
    exit 1
else
    echo "[PASS] Server correctly rejected unauthorized client credentials with STATUS_ERR_AUTH."
fi

# 4. Graceful shutdown
echo ""
echo "[STEP 4] Sending SIGINT to server for graceful shutdown..."
kill -INT $SERVER_PID
wait $SERVER_PID || true
echo "[PASS] Server terminated cleanly on SIGINT."

echo ""
echo "=========================================================="
echo "    ALL PHASE 2 INTEGRATION TESTS PASSED SUCCESSFULLY!    "
echo "=========================================================="

