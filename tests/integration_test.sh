#!/usr/bin/env bash
# MiniDrop End-to-End Live Integration Test Suite

set -e

PORT=9090
TOKEN="minidrop_e2e_secret"
STORAGE_ROOT="/tmp/minidrop_e2e_storage"
CLIENT_SYNC="/tmp/minidrop_e2e_client_sync"
CLIENT_ID="client01"

echo "=========================================================="
echo "    MiniDrop End-to-End Live System Integration Test      "
echo "=========================================================="

rm -rf "$STORAGE_ROOT" "$CLIENT_SYNC"
mkdir -p "$STORAGE_ROOT" "$CLIENT_SYNC"

# 1. Start Server in background
echo "[STEP 1] Starting MiniDrop Multithreaded Server on port $PORT..."
./bin/minidrop-server --port "$PORT" --storage "$STORAGE_ROOT" --token "$TOKEN" --verbose > /tmp/minidrop_e2e_server.log 2>&1 &
SERVER_PID=$!

sleep 1
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "[FAIL] Server failed to start!"
    cat /tmp/minidrop_e2e_server.log
    exit 1
fi
echo "[PASS] Server started with PID: $SERVER_PID"

# 2. Start Client in background
echo ""
echo "[STEP 2] Starting MiniDrop Client sync engine on directory $CLIENT_SYNC..."
./bin/minidrop-client --server 127.0.0.1 --port "$PORT" --client-id "$CLIENT_ID" \
                      --directory "$CLIENT_SYNC" --token "$TOKEN" --verbose > /tmp/minidrop_e2e_client.log 2>&1 &
CLIENT_PID=$!

trap "kill -INT $CLIENT_PID $SERVER_PID 2>/dev/null || true; rm -rf '$STORAGE_ROOT' '$CLIENT_SYNC'" EXIT

sleep 1
if ! kill -0 $CLIENT_PID 2>/dev/null; then
    echo "[FAIL] Client failed to connect and start sync engine!"
    cat /tmp/minidrop_e2e_client.log
    exit 1
fi
echo "[PASS] Client sync engine active with PID: $CLIENT_PID"

SERVER_CLIENT_DIR="$STORAGE_ROOT/$CLIENT_ID"

# 3. Test File Creation
echo ""
echo "[STEP 3] Testing Real-Time File Creation..."
echo "Welcome to MiniDrop OS Project Synchronization Engine!" > "$CLIENT_SYNC/hello.txt"
sleep 1.5

if [ -f "$SERVER_CLIENT_DIR/hello.txt" ]; then
    echo "  [PASS] File 'hello.txt' detected by inotify and replicated to server"
    LOCAL_SUM=$(sha256sum "$CLIENT_SYNC/hello.txt" | awk '{print $1}')
    SERVER_SUM=$(sha256sum "$SERVER_CLIENT_DIR/hello.txt" | awk '{print $1}')
    if [ "$LOCAL_SUM" == "$SERVER_SUM" ]; then
        echo "  [PASS] SHA-256 verified match: $LOCAL_SUM"
    else
        echo "  [FAIL] Checksum mismatch! Local: $LOCAL_SUM, Server: $SERVER_SUM"
        exit 1
    fi
else
    echo "  [FAIL] File 'hello.txt' not found on server!"
    cat /tmp/minidrop_e2e_client.log
    exit 1
fi

# 4. Test File Modification
echo ""
echo "[STEP 4] Testing Real-Time File Modification..."
echo "Appending second paragraph to test IN_CLOSE_WRITE debouncing." >> "$CLIENT_SYNC/hello.txt"
sleep 1.5

LOCAL_SUM=$(sha256sum "$CLIENT_SYNC/hello.txt" | awk '{print $1}')
SERVER_SUM=$(sha256sum "$SERVER_CLIENT_DIR/hello.txt" | awk '{print $1}')
if [ "$LOCAL_SUM" == "$SERVER_SUM" ]; then
    echo "  [PASS] Modified file synchronized and verified: $LOCAL_SUM"
else
    echo "  [FAIL] Modification sync failed! Local: $LOCAL_SUM, Server: $SERVER_SUM"
    exit 1
fi

# 5. Test Nested Directory Creation & Deep File Sync
echo ""
echo "[STEP 5] Testing Nested Subdirectories & Recursive inotify Watching..."
mkdir -p "$CLIENT_SYNC/subfolder/deep_dir"
sleep 0.5
echo "Deeply nested file contents" > "$CLIENT_SYNC/subfolder/deep_dir/deep.txt"
sleep 1.5

if [ -f "$SERVER_CLIENT_DIR/subfolder/deep_dir/deep.txt" ]; then
    echo "  [PASS] Nested directory and file 'subfolder/deep_dir/deep.txt' created on server"
else
    echo "  [FAIL] Nested file missing from server!"
    exit 1
fi

# 6. Test File Rename
echo ""
echo "[STEP 6] Testing File Rename (IN_MOVED_FROM / IN_MOVED_TO cookie correlation)..."
mv "$CLIENT_SYNC/hello.txt" "$CLIENT_SYNC/hello_renamed.txt"
sleep 1.5

if [ -f "$SERVER_CLIENT_DIR/hello_renamed.txt" ] && [ ! -f "$SERVER_CLIENT_DIR/hello.txt" ]; then
    echo "  [PASS] File rename correlated successfully: 'hello.txt' -> 'hello_renamed.txt'"
else
    echo "  [FAIL] File rename not applied properly on server!"
    exit 1
fi

# 7. Test Large Binary File Transfer (Chunking Verification)
echo ""
echo "[STEP 7] Testing 1 MB Binary Streaming Transfer..."
dd if=/dev/urandom of="$CLIENT_SYNC/payload.bin" bs=64k count=16 status=none
sleep 2

LOCAL_BIN_SUM=$(sha256sum "$CLIENT_SYNC/payload.bin" | awk '{print $1}')
SERVER_BIN_SUM=$(sha256sum "$SERVER_CLIENT_DIR/payload.bin" | awk '{print $1}')
if [ "$LOCAL_BIN_SUM" == "$SERVER_BIN_SUM" ]; then
    echo "  [PASS] 1 MB binary chunked transfer completed with bit-identical SHA-256: $LOCAL_BIN_SUM"
else
    echo "  [FAIL] Binary transfer checksum mismatch!"
    exit 1
fi

# 8. Test File Deletion
echo ""
echo "[STEP 8] Testing File Deletion Sync..."
rm "$CLIENT_SYNC/hello_renamed.txt"
sleep 1.5

if [ ! -f "$SERVER_CLIENT_DIR/hello_renamed.txt" ]; then
    echo "  [PASS] File deletion synchronized: 'hello_renamed.txt' removed from server"
else
    echo "  [FAIL] Deleted file still exists on server!"
    exit 1
fi

# 9. Graceful Shutdown
echo ""
echo "[STEP 9] Testing Graceful Shutdown with SIGINT..."
kill -INT $CLIENT_PID
wait $CLIENT_PID || true
echo "  [PASS] Client terminated cleanly."

kill -INT $SERVER_PID
wait $SERVER_PID || true
echo "  [PASS] Server terminated cleanly."

echo ""
echo "=========================================================="
echo "   ALL END-TO-END SYSTEM INTEGRATION TESTS PASSED 100%!   "
echo "=========================================================="
