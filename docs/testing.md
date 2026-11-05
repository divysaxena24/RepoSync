# MiniDrop Testing Guide

MiniDrop includes comprehensive unit and system integration testing suites designed to verify protocol framing, cryptographic verification, thread concurrency, and live file synchronization.

## 1. Running Unit Tests

To compile and execute all unit tests:
```bash
make test
```

### Protocol Unit Tests (`bin/test_protocol`)
- Asserts that `sizeof(minidrop_header_t) == 32`.
- Verifies big-endian network byte-order translation.
- Tests rejection of corrupted magic numbers and unsupported protocol versions.
- Uses `socketpair()` IPC to test full-duplex transmission and message framing.

### Checksum Unit Tests (`bin/test_checksum`)
- Tests OpenSSL EVP SHA-256 against official NIST test vectors (empty string, "abc").
- Verifies chunked file streaming calculation against memory buffer digest.

### Concurrency Queue Tests (`bin/test_queue`)
- Launches 4 producer threads and 2 consumer threads pushing and popping 1,000 items concurrently.
- Validates mutex locking, condition variable signaling, and clean queue shutdown with zero deadlocks.

## 2. Running End-to-End Live Integration Tests

MiniDrop provides automated integration test scripts:

### Phase 2 Network Handshake Test:
```bash
./scripts/test_phase2.sh
```
- Spawns background server.
- Verifies valid client authentication and PING/PONG round-trip latency.
- Tests rejection of invalid authentication credentials.
- Tests `SIGINT` server graceful termination.

### Full System Synchronization Test:
```bash
./tests/integration_test.sh
```
- Spawns background server and client sync engine on `/tmp`.
- Tests real-time file creation replication.
- Tests file modification synchronization and content verification.
- Tests recursive directory watching and nested subfolder creation.
- Tests rename event correlation via inotify cookies.
- Tests streaming 1 MB binary files in 64 KB chunks with SHA-256 validation.
- Tests real-time file deletion synchronization.
- Tests graceful shutdown of both daemon processes.

