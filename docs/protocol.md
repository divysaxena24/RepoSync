# MiniDrop Wire Protocol Specification (v1)

## 1. Frame Structure

All communication across TCP sockets uses binary framing consisting of a fixed 32-byte header followed by an optional payload.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          MAGIC (0x4D44)       |   VERSION (1) |  MSG_TYPE (1) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          REQUEST ID                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         PAYLOAD LENGTH                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          STATUS CODE                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     CLIENT ID (16 Bytes UTF-8)                |
|                                                               |
|                                                               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## 2. Header Fields

- **`magic`** (`uint16_t`): Fixed at `0x4D44` ("MD" in ASCII).
- **`version`** (`uint8_t`): Protocol version (currently `1`).
- **`msg_type`** (`uint8_t`): Identifier for operation.
- **`request_id`** (`uint32_t`): Monotonically increasing request identifier.
- **`payload_len`** (`uint32_t`): Length of variable payload following header (Big-Endian).
- **`status_code`** (`uint32_t`): Result code (`STATUS_OK = 0`, `STATUS_ERR_AUTH = 1`, etc.).
- **`client_id`** (`char[16]`): Client username or UUID string.

## 3. Message Catalog

| Value | Identifier | Payload Format |
| :--- | :--- | :--- |
| `0x01` | `MSG_AUTH_REQUEST` | Secret authentication token string |
| `0x02` | `MSG_AUTH_RESPONSE` | Empty payload; status code indicates result |
| `0x03` | `MSG_PING` | Empty payload |
| `0x04` | `MSG_PONG` | Empty payload |
| `0x26` | `MSG_FILE_TRANSFER_START` | `rel_path\0` + `expected_sha256\0` |
| `0x27` | `MSG_FILE_TRANSFER_CHUNK` | Raw byte chunk (up to 64 KB) |
| `0x28` | `MSG_FILE_TRANSFER_END` | Expected SHA-256 hexadecimal string (65 bytes) |
| `0x22` | `MSG_FILE_DELETE` | `rel_path\0` |
| `0x23` | `MSG_FILE_RENAME` | `old_rel_path\0` + `new_rel_path\0` |
| `0x24` | `MSG_DIRECTORY_CREATE` | `rel_path\0` |
| `0x25` | `MSG_DIRECTORY_DELETE` | `rel_path\0` |
| `0xE0` | `MSG_ACK` | Empty payload; status code indicates result |
| `0xFF` | `MSG_ERROR` | Empty payload or error description string |

