# MiniDrop Architecture Specification

## 1. Executive Overview

MiniDrop is a multithreaded directory synchronization system built in C (C11) for Linux. It demonstrates low-level Operating Systems engineering principles: asynchronous kernel event monitoring (`inotify`), concurrent POSIX multithreading (`pthreads`), thread-safe synchronization queues protected by mutexes and condition variables, raw TCP socket network framing, streaming cryptographic data integrity validation (OpenSSL SHA-256), path canonicalization defenses, and atomic POSIX file replacement.

## 2. Process & Thread Model

### Client Architecture
- **Main Thread**: CLI argument parsing, network connection bootstrap, authentication handshake, initial directory tree synchronization, and signal loop (`SIGINT`/`SIGTERM`).
- **Inotify Directory Watcher Thread**: Continuously monitors the directory tree recursively via Linux `inotify`. Catches `IN_CREATE`, `IN_MODIFY`, `IN_CLOSE_WRITE`, `IN_DELETE`, and rename events (`IN_MOVED_FROM` / `IN_MOVED_TO`). Enqueues normalized events into the thread-safe bounded queue.
- **Sync Engine Worker Thread**: Dequeues events from `sync_queue_t`. Converts them into TCP wire messages (`MSG_FILE_TRANSFER_START`, `MSG_FILE_TRANSFER_CHUNK`, `MSG_FILE_TRANSFER_END`, `MSG_FILE_DELETE`, `MSG_FILE_RENAME`, etc.) and streams chunked file payloads.

### Server Architecture
- **Master Listener Thread**: Runs on the main process. Creates passive listening socket, sets `SO_REUSEADDR`, and polls `accept()` with 500 ms timeouts for responsive signal termination.
- **Worker Thread Per Client**: For every incoming client, a new thread is spawned (`pthread_create`) and detached (`pthread_detach`). The worker handles client authentication, keepalive pings, and streaming file writes into the client's isolated sandbox directory (`storage/clients/<client_id>/`).
- **Storage Subsystem**: Thread-safe file writing using POSIX file staging (`.<file>.tmp.<req_id>`), SHA-256 validation, and atomic `rename()` to eliminate race conditions between concurrent clients.

## 3. Concurrency & Synchronization Primitives

| Primitive | Usage Location | Purpose |
| :--- | :--- | :--- |
| `pthread_mutex_t` | `client/queue.c` | Protects the circular event queue buffer during concurrent push/pop. |
| `pthread_cond_t` (`cond_not_empty`) | `client/queue.c` | Blocks the sync worker thread when the event queue is empty without busy-waiting. |
| `pthread_cond_t` (`cond_not_full`) | `client/queue.c` | Blocks producer/watcher threads if the queue reaches capacity (512 items). |
| `pthread_mutex_t` | `server/storage.c` | Serializes file creation, rename, and deletion operations across client handler threads. |
| `pthread_mutex_t` | `common/logger.c` | Serializes formatted console/file log output, preventing interleaved lines. |

