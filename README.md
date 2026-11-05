# MiniDrop 📦
### A Multithreaded Client-Server Directory Synchronization System in C (C11)

MiniDrop is a functional directory synchronization application built in C for Linux, inspired by Dropbox's desktop engine. It automatically detects and replicates filesystem modifications (new files, updates, renames, deletions, and nested directory trees) from a monitored client directory to a remote server over TCP sockets in real time.

---

## 1. Operating Systems Concepts Demonstrated

MiniDrop is designed to demonstrate low-level systems programming concepts without reliance on high-level languages or web frameworks:

1. **Linux `inotify` Kernel Subsystem**:
   - Asynchronous filesystem event notifications without CPU-intensive polling.
   - Dynamic recursive watch tree for nested directories.
   - Kernel cookie correlation for file rename operations (`IN_MOVED_FROM` / `IN_MOVED_TO`).
   - `poll()` polling loop with timeouts for responsive thread shutdown.
2. **POSIX Multithreading (`pthreads`)**:
   - Client: Separated directory watcher thread, thread-safe sync queue, and sync worker thread.
   - Server: Master listener thread + thread-per-client concurrent worker model with `pthread_detach()`.
3. **Concurrency Synchronization Primitives**:
   - `pthread_mutex_t` and `pthread_cond_t` protecting bounded circular event queues (producer-consumer pattern).
   - Server-side mutex locking protecting storage repositories against concurrent client race conditions.
4. **Custom Network Protocol & TCP Framing**:
   - 32-byte packed binary header (`#pragma pack(1)`) with explicit Big-Endian conversion (`htons`/`htonl`/`ntohs`/`ntohl`).
   - Loop-based network abstractions (`minidrop_send_all`/`minidrop_recv_all`) preventing partial read/write stream corruption.
5. **Data Integrity & Security**:
   - Streaming OpenSSL SHA-256 calculation (`EVP_DigestInit_ex` / `EVP_DigestUpdate`) in 64 KB chunks.
   - Temporary staging files (`.<filename>.tmp.<req_id>`) with atomic POSIX `rename()` commit.
   - Path traversal defenses preventing directory escape (`../../etc/passwd`).
6. **Graceful Signal Handling**:
   - `sigaction()` handlers for `SIGINT` and `SIGTERM` with `volatile sig_atomic_t` flags.
   - Masking `SIGPIPE` with `SIG_IGN` to prevent abrupt process crashes during socket disconnects.

---

## 2. System Architecture

```
+-------------------------------------------------------------------------------+
|                             CLIENT APPLICATION                                |
|                                                                               |
|  [Monitored Directory Tree]                                                   |
|             |                                                                 |
|      (Kernel Events)                                                          |
|             v                                                                 |
|   [Watcher Thread: inotify] ---> [Thread-Safe Bounded Sync Queue]             |
|                                         (Mutex + Condvar)                     |
|                                                |                              |
|                                                v                              |
|                                     [Sync Worker Thread]                      |
|                                                |                              |
|                                                v                              |
|                                      [TCP Socket Client]                      |
+-------------------------------------------------------------------------------+
                                                 |
                                     (Framed Binary TCP Wire)
                                                 v
+-------------------------------------------------------------------------------+
|                             SERVER APPLICATION                                |
|                                                                               |
|   [Master Listener Socket: port 8080]                                         |
|             |                                                                 |
|         (accept)                                                              |
|             v                                                                 |
|   [Thread-per-Client Worker Pool] (Detached POSIX Threads)                    |
|             |                                                                 |
|             v                                                                 |
|   [Path Sanitizer & Security Jail]                                            |
|             |                                                                 |
|             v                                                                 |
|   [Storage Engine: Staging .tmp -> SHA-256 Checksum -> Atomic rename()]       |
|             |                                                                 |
|             v                                                                 |
|   [Client Storage Repository: ./storage/clients/<client_id>/]                 |
+-------------------------------------------------------------------------------+
```

---

## 3. Directory Structure

```
MiniDrop/
├── Makefile                # GNU Makefile (all, server, client, test, clean)
├── README.md               # Complete project documentation
├── .gitignore
├── client/
│   ├── main.c              # Client CLI entry point & signal handling
│   ├── client.c            # Socket connect, authenticate & ping
│   ├── client.h
│   ├── watcher.c           # inotify recursive directory monitor
│   ├── watcher.h
│   ├── queue.c             # Thread-safe bounded event queue
│   ├── queue.h
│   ├── transfer.c          # Chunked file streaming & SHA-256 calculation
│   ├── transfer.h
│   ├── sync_engine.c       # Initial sync & worker thread coordinator
│   └── sync_engine.h
├── server/
│   ├── main.c              # Server CLI entry point
│   ├── server.c            # Multithreaded TCP accept listener
│   ├── server.h
│   ├── connection.c        # Client session worker thread
│   ├── connection.h
│   ├── storage.c           # Staging writes, path validation & atomic rename
│   ├── storage.h
│   ├── receiver.c          # Protocol command dispatcher
│   └── receiver.h
├── common/
│   ├── protocol.c          # 32-byte header serialization & framing
│   ├── protocol.h
│   ├── checksum.c          # OpenSSL EVP SHA-256 digest engine
│   ├── checksum.h
│   ├── metadata.c          # Directory scan, lstat abstraction & path cleaner
│   ├── metadata.h
│   ├── logger.c            # Thread-safe colored console logging
│   └── logger.h
├── config/
│   └── server.conf         # Configuration file
├── storage/
│   └── clients/            # Server-side client repositories
├── tests/
│   ├── test_protocol.c     # Protocol framing & byte-order unit test
│   ├── test_checksum.c     # SHA-256 NIST vector test
│   ├── test_queue.c        # Multithreaded queue concurrency test
│   └── integration_test.sh # End-to-end live synchronization test
├── scripts/
│   ├── setup.sh            # Dependency checker
│   ├── test_phase2.sh      # Handshake integration test
│   ├── start_server.sh     # Convenience server starter
│   └── start_client.sh     # Convenience client starter
└── docs/
    ├── architecture.md     # In-depth architectural design
    ├── protocol.md         # Wire protocol specification
    └── testing.md          # Testing guide
```

---

## 4. Compilation & Build

### Prerequisites
- Linux OS (Ubuntu 22.04 / 24.04 or WSL 2)
- GCC Compiler with C11 support
- GNU Make
- OpenSSL development headers (`libssl-dev`)

### Compile All Binaries and Run Unit Tests
```bash
make clean && make all
```

This compiles:
- `bin/minidrop-server`: Central multithreaded daemon
- `bin/minidrop-client`: Client directory synchronizer
- `bin/test_protocol`: Protocol unit test suite
- `bin/test_checksum`: SHA-256 unit test suite
- `bin/test_queue`: Queue concurrency test suite

---

## 5. Execution Instructions

### A. Smart Dynamic Button Web Dashboard (Recommended)
Launch the single-user Remote Repo service and dashboard:
```bash
./scripts/start_repo_ui.sh
```
Or directly:
```bash
./bin/minidrop web --port 8080 --local ./workspace --remote ./storage/repo
```
Open **`http://localhost:8080`** in any web browser!

- The central **Smart Action Button** automatically polls the backend every 1 second.
- When you add or edit files in `./workspace/`, the button dynamically changes to **`[ ⬆ Push to Remote (X Files) ]`** with a glowing blue style.
- When you click it, it executes the push, streams chunks with SHA-256 verification, and automatically flips back to **`[ ✓ Repository Up to Date ]`**!

### B. Git-like Terminal CLI Commands
You can also run commands directly from the terminal:
```bash
# Check current difference between local and remote repo
./bin/minidrop status

# Push all local changes to the remote repository
./bin/minidrop push

# Pull any remote updates down to the local workspace
./bin/minidrop pull
```

---

## 6. Automated Testing

### 1. Run Unit Tests:
```bash
make test
```
- **Protocol Test**: 22 assertions verifying header layout, network byte order, framing, and corruption rejection.
- **Checksum Test**: 7 assertions verifying buffer and file SHA-256 against NIST vectors.
- **Queue Test**: 1,000 items concurrently pushed and popped across 4 producer and 2 consumer threads.

### 2. Run End-to-End Live Integration Test:
```bash
./tests/integration_test.sh
```
Runs a complete simulated workflow:
- Real-time file creation replication
- Real-time file modification synchronization
- Nested directory creation and recursive watching
- Inotify cookie-correlated file rename
- 1 MB random binary streaming transfer in 64 KB chunks
- Real-time file deletion
- Graceful `SIGINT` process shutdown

---

## 7. Limitations & Future Scope
- **NAT / WAN Traversal**: Currently designed for LAN / direct TCP IP routing. Future releases could integrate TLS tunneling or STUN/TURN.
- **Delta Sync**: Entire files are transferred when modified; future versions could use the rsync rolling-checksum algorithm to transmit only modified binary blocks.
- **End-to-End Encryption**: OpenSSL is currently used for SHA-256 data integrity; future versions can layer OpenSSL TLS 1.3 socket encryption.

