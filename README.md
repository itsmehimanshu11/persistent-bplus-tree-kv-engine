# Persistent B+ Tree Key-Value Storage Engine

A C++17 embedded key-value storage engine built around an in-memory B+ Tree and a Write-Ahead Log (WAL).

The engine provides persistent storage through WAL-based recovery, an interactive CLI, a dependency-free HTTP/1.1 REST API, integrity verification, statistics, automated tests, and benchmarks.

---

## Features

- B+ Tree point lookup, insert/update and deletion
- B+ Tree split, redistribution and merge rebalancing
- Ordered leaf links for range scans
- Iterator API with bounded `[start, limit)` scans
- Write-Ahead Log (WAL)
- WAL sequence numbers
- CRC32 integrity checking
- WAL rotation
- WAL recovery after restart
- Explicit `flush` support
- Durable `sync` support
- Reader/writer synchronization using `std::shared_mutex`
- Interactive CLI
- Dependency-free HTTP/1.1 REST API
- Tree integrity verification
- B+ Tree and WAL statistics
- Dedicated benchmark executable
- CMake build system
- CTest integration tests
- Persistent recovery of key-value data after server restart

---

## Architecture

```text
                 CLI / C++ API
                       |
                       |
                 REST / HTTP API
                       |
                       v
                    KVEngine
                   /        \
                  /          \
                 v            v
              B+ Tree        WAL
                 |             |
                RAM           Disk
```

The current engine is intentionally designed as an embedded:

```text
B+ Tree + Write-Ahead Log
```

The B+ Tree stores the active key-value data in memory, while the WAL provides persistence and recovery after process restart.

The REST API is an adapter around the existing `KVEngine`. It does not replace or modify the underlying B+ Tree or WAL architecture.

The `memtable/`, `sstable/` and `flush/` directories contain additional experimental storage components. They are not presented as a complete LSM-tree implementation.

---

# Build

From the project root:

```bash
cmake -S . -B build
cmake --build build --config Release
```

For the Windows/MSYS2/MinGW environment:

```powershell
cmake -S . -B build
cmake --build build -j 4
```

After a successful build, the `build` directory contains executables such as:

```text
kv_engine_main.exe
kv_engine_test.exe
kv_benchmark.exe
kv_rest_server.exe
test_btree.exe
wal_test.exe
```

---

# Run Tests

Run the complete CTest suite:

```bash
ctest --test-dir build --output-on-failure
```

Individual test executables:

```text
build/test_btree.exe
build/kv_engine_test.exe
build/wal_test.exe
```

On non-Windows systems, the executables do not use the `.exe` suffix.

---

# Run the CLI

From the project root:

```bash
build/kv_engine_main.exe
```

Example:

```text
kv> put user:1001 Alice
OK

kv> put user:1002 Bob
OK

kv> get user:1001
Alice

kv> scan user:1001 user:1003
user:1001 = Alice
user:1002 = Bob
--- 2 key(s) ---

kv> del user:1002
OK

kv> stats
kv> verify
kv> walstats
kv> flush
kv> sync
kv> quit
```

---

# Write-Ahead Log

The engine uses a Write-Ahead Log to provide persistence and recovery.

For a PUT operation, the write path is:

```text
Client
   |
   v
KVEngine::Put()
   |
   v
Write operation to WAL
   |
   v
Optional WAL Sync
   |
   v
Update B+ Tree
```

For a DELETE operation:

```text
Client
   |
   v
KVEngine::Delete()
   |
   v
Delete operation to WAL
   |
   v
Optional WAL Sync
   |
   v
Delete from B+ Tree
```

The WAL therefore records the operation before the corresponding B+ Tree modification.

---

# WAL Recovery

When the engine starts with WAL enabled, it attempts to recover previous operations from the WAL.

The recovery process is conceptually:

```text
WAL files on Disk
       |
       v
Read WAL records
       |
       v
Validate records
       |
       v
CRC32 verification
       |
       v
Replay PUT / DELETE operations
       |
       v
Reconstruct B+ Tree
```

This allows the engine to reconstruct its in-memory B+ Tree after a process restart.

CRC32 is used to detect corrupted complete WAL records.

A trailing partial record is treated as an incomplete write from a possible crash and is ignored.

---

# WAL Synchronization

The engine supports explicit synchronization of WAL data.

`flush`:

```text
Flush C++ stream buffers
```

`sync`:

```text
Flush stream
     |
     v
Request OS-level synchronization
     |
     +--> fsync() on POSIX
     |
     +--> _commit() on Windows
```

The REST API exposes both operations.

---

# WAL Persistence Verification

The REST API and WAL persistence were tested using the following flow.

### 1. Start the REST server

```powershell
.\build\kv_rest_server.exe
```

The server starts on:

```text
http://127.0.0.1:8080
```

### 2. Insert a value

```powershell
curl.exe -X PUT http://127.0.0.1:8080/kv/test2 -d "HELLO_WAL"
```

Response:

```json
{"status":"ok","key":"test2"}
```

### 3. Verify the value

```powershell
curl.exe http://127.0.0.1:8080/kv/test2
```

Response:

```json
{"key":"test2","value":"HELLO_WAL"}
```

### 4. Verify WAL data exists

The WAL directory can be inspected with:

```powershell
Get-ChildItem ".\build\wal_data" | Select-Object Name,Length
```

Example:

```text
Name      Length
----      ------
wal_0.log 43
```

The non-zero WAL file size confirms that the write generated WAL data.

### 5. Restart the REST server

Stop the server:

```text
Ctrl+C
```

Then start it again:

```powershell
.\build\kv_rest_server.exe
```

### 6. Read the value after restart

```powershell
curl.exe http://127.0.0.1:8080/kv/test2
```

Expected response:

```json
{"key":"test2","value":"HELLO_WAL"}
```

This demonstrates that the value can be recovered from the WAL after the server is restarted.

---

# REST API

The engine includes a small dependency-free HTTP/1.1 REST server.

The REST layer acts as an adapter around the existing `KVEngine`.

It does not change the B+ Tree or WAL implementation.

---

## Build REST API

From the project root:

```bash
cmake -S . -B build
cmake --build build --config Release
```

---

## Start REST Server

From the `build` directory:

```powershell
.\kv_rest_server.exe
```

Or from the project root:

```powershell
.\build\kv_rest_server.exe
```

By default, the server binds to:

```text
127.0.0.1:8080
```

The server displays:

```text
KV Engine REST API
Listening on http://127.0.0.1:8080

Endpoints:
GET     /health
GET     /kv/{key}
PUT     /kv/{key}
DELETE  /kv/{key}
GET     /stats
POST    /flush
POST    /sync

Press Ctrl+C to stop.
```

---

# REST Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/health` | Health check |
| PUT | `/kv/{key}` | Insert or update a value |
| GET | `/kv/{key}` | Read a value |
| DELETE | `/kv/{key}` | Delete a key |
| GET | `/stats` | B+ Tree and WAL statistics |
| POST | `/flush` | Flush WAL buffers |
| POST | `/sync` | Flush and durably sync WAL |

The request body of a `PUT` request is used as the value.

Keys containing spaces or reserved URL characters should be percent-encoded.

---

# REST API Examples

## Health Check

Request:

```powershell
curl.exe http://127.0.0.1:8080/health
```

Response:

```json
{"status":"ok"}
```

---

## Insert / Update a Key

Request:

```powershell
curl.exe -X PUT http://127.0.0.1:8080/kv/name -d "Himanshu"
```

Response:

```json
{"status":"ok","key":"name"}
```

---

## Get a Key

Request:

```powershell
curl.exe http://127.0.0.1:8080/kv/name
```

Response:

```json
{"key":"name","value":"Himanshu"}
```

---

## Delete a Key

Request:

```powershell
curl.exe -X DELETE http://127.0.0.1:8080/kv/name
```

Response:

```json
{"status":"ok","key":"name"}
```

After deletion:

```powershell
curl.exe http://127.0.0.1:8080/kv/name
```

Response:

```json
{"error":"NotFound: Key not found"}
```

---

# REST Statistics

Request:

```powershell
curl.exe http://127.0.0.1:8080/stats
```

Example response:

```json
{
  "keys": 0,
  "nodes": 1,
  "height": 1,
  "memory_bytes": 8280,
  "wal": {
    "total_writes": 2,
    "total_bytes_written": 74,
    "total_syncs": 0,
    "current_file_number": 0,
    "current_file_size": 74
  }
}
```

The exact values depend on the current database state.

---

# Flush

The `/flush` endpoint flushes WAL buffers.

Request:

```powershell
curl.exe -X POST http://127.0.0.1:8080/flush
```

---

# Durable Sync

The `/sync` endpoint flushes and requests durable OS-level synchronization.

Request:

```powershell
curl.exe -X POST http://127.0.0.1:8080/sync
```

---

# REST API Error Handling

If a key does not exist:

```powershell
curl.exe http://127.0.0.1:8080/kv/unknown
```

The server returns:

```json
{"error":"NotFound: Key not found"}
```

---

# URL Encoding

Keys containing reserved URL characters should be percent-encoded.

For example:

```text
user:1001
```

can be represented as:

```text
user%3A1001
```

Example:

```powershell
curl.exe -X PUT http://127.0.0.1:8080/kv/user%3A1001 -d "Alice"
```

Then:

```powershell
curl.exe http://127.0.0.1:8080/kv/user%3A1001
```

Response:

```json
{"key":"user:1001","value":"Alice"}
```

---

# Concurrency

The engine uses:

```cpp
std::shared_mutex
```

for reader/writer synchronization.

Read operations use shared locking:

```cpp
std::shared_lock<std::shared_mutex>
```

Write operations use exclusive locking:

```cpp
std::unique_lock<std::shared_mutex>
```

This allows multiple readers while writes require exclusive access.

The current concurrency model is intentionally simple.

It does not implement:

- Lock-free operations
- Node-level lock coupling
- MVCC
- Snapshot isolation

---

# B+ Tree

The primary in-memory data structure is a balanced B+ Tree.

The B+ Tree supports:

- Point lookup
- Insert
- Update
- Delete
- Node splitting
- Redistribution
- Node merging
- Ordered leaf links
- Range scanning
- Iteration
- Integrity verification

Leaf nodes are linked to support efficient ordered range scans.

---

# Range Scan

For a range scan, the engine first locates the starting key in the B+ Tree and then traverses linked leaf nodes.

Conceptually:

```text
Root
 |
 v
Internal Nodes
 |
 v
Leaf -> Leaf -> Leaf -> Leaf
         |
         +---- Ordered keys
```

This allows range scans to operate in:

```text
O(log n + k)
```

where `k` is the number of returned records.

---

# Complexity

For a balanced B+ Tree:

| Operation | Complexity |
|---|---|
| Point lookup | O(log n) |
| Insert/update | O(log n) amortized |
| Delete | O(log n) amortized |
| Range scan of `k` results | O(log n + k) |
| Space | O(n) |

Insertions may occasionally require node splits.

Deletions may require redistribution or node merging.

---

# Benchmarks

Run:

```powershell
.\build\kv_benchmark.exe
```

The benchmark reports real measurements for the machine where it is executed.

It currently measures:

- PUT
- GET
- Range scans
- DELETE
- `std::map` insertion
- `std::unordered_map` insertion

Do not copy benchmark numbers from another machine into a resume.

Benchmark numbers should be generated on the machine where the project is being demonstrated.

---

# Integrity Verification

The engine includes tree integrity verification.

The purpose is to detect structural problems in the B+ Tree.

The CLI provides:

```text
verify
```

This can be used during development and testing to validate the internal tree structure.

---

# Project Structure

```text
KV_Engine_REST_API_Updated/
│
├── DATABASE ENGINE/
│   │
│   ├── include/
│   │   ├── api/
│   │   ├── btree/
│   │   ├── kv/
│   │   └── wal/
│   │
│   ├── src/
│   │   ├── api/
│   │   │   ├── rest_server.cpp
│   │   │   └── ...
│   │   │
│   │   ├── btree/
│   │   ├── kv/
│   │   └── wal/
│   │
│   └── tests/
│       ├── test_btree.cpp
│       ├── test_wal.cpp
│       └── ...
│
├── build/
│   ├── kv_engine_main.exe
│   ├── kv_engine_test.exe
│   ├── kv_benchmark.exe
│   ├── kv_rest_server.exe
│   ├── test_btree.exe
│   ├── wal_test.exe
│   └── wal_data/
│       └── wal_0.log
│
├── CMakeLists.txt
├── README.md
├── REST_API.md
└── ...
```

The primary execution path is:

```text
KVEngine
   |
   +---- B+ Tree
   |
   +---- WAL
```

The experimental MemTable/SSTable/flush components are not currently wired into `KVEngine` as a complete LSM architecture.

---

# Limitations

- The current iterator is protected by a tree-level shared lock for its lifetime.
- The iterator is not a snapshot/MVCC iterator.
- The concurrency model is reader/writer locking.
- The engine does not use lock-free data structures.
- The engine does not use node-level lock coupling.
- The B+ Tree is an in-memory index.
- The WAL provides recovery but is not an on-disk B+ Tree page format.
- The experimental MemTable/SSTable/flush components are not wired into `KVEngine` as a complete LSM architecture.
- The REST API is intentionally small and dependency-free.
- The REST server implements a focused HTTP/1.1 API rather than a full web framework.
- The REST server defaults to loopback and is intended primarily for local use.
- Authentication is not implemented by the current REST server.
- TLS is not implemented by the current REST server.

---

# Security

The REST server defaults to:

```text
127.0.0.1
```

This prevents the server from being exposed to the network accidentally.

If you intentionally bind the server to:

```text
0.0.0.0
```

do not expose it directly to the public internet.

Authentication and TLS/reverse-proxy protection should be placed in front of the server before public deployment.

---

# Design Philosophy

The project intentionally focuses on implementing the storage engine itself instead of relying on a database library.

The main components are:

```text
B+ Tree
   |
   +--> In-memory indexing and storage

WAL
   |
   +--> Persistence and crash recovery

KVEngine
   |
   +--> Coordinates B+ Tree + WAL

REST API
   |
   +--> Provides HTTP access to KVEngine

CLI
   |
   +--> Interactive access to KVEngine
```

This separation keeps the REST layer independent from the underlying storage implementation.

---

# Interview-Safe Project Description

> Built a persistent embedded key-value storage engine in C++17 using a balanced B+ Tree and Write-Ahead Logging. Implemented split/merge rebalancing, ordered range scans, CRC32-checked WAL recovery, reader/writer synchronization, a dependency-free REST API, an interactive CLI, integrity verification, automated tests and reproducible benchmarks.

---

# Key Technical Highlights

### Data Structure

- Balanced B+ Tree
- O(log n) point lookup
- O(log n) amortized insertion
- O(log n) amortized deletion
- O(log n + k) range scans
- Linked leaf nodes

### Persistence

- Write-Ahead Logging
- Sequence numbers
- CRC32 validation
- WAL rotation
- WAL replay
- Restart recovery
- Flush support
- Durable sync support

### Concurrency

- `std::shared_mutex`
- Shared read locking
- Exclusive write locking

### API

- Dependency-free HTTP/1.1
- REST endpoints
- GET
- PUT
- DELETE
- POST
- JSON responses
- Health endpoint
- Statistics endpoint
- WAL flush/sync endpoints

### Engineering

- C++17
- CMake
- CTest
- Automated tests
- Benchmark executable
- Windows/MSYS2/MinGW support
- No external HTTP framework required

---

# Example End-to-End Flow

```text
                    HTTP Client
                         |
                         v
                 REST API Server
                         |
                         v
                     KVEngine
                    /        \
                   /          \
                  v            v
              B+ Tree         WAL
                  |             |
                  |             v
                  |          Disk
                  |
                 RAM
```

Example PUT:

```text
PUT /kv/name
Body: Himanshu

        |
        v

     KVEngine

        |
        +----------------+
        |                |
        v                v
       WAL            B+ Tree
        |                |
        v                v
      Disk              RAM
```

Example restart:

```text
Server stops
     |
     v
Server starts
     |
     v
Read WAL
     |
     v
Validate records
     |
     v
Replay operations
     |
     v
Reconstruct B+ Tree
     |
     v
GET returns previous value
```

---

# Current REST API Verification

The following operations have been verified against the running server:

```text
GET /health
PUT /kv/name
GET /kv/name
DELETE /kv/name
GET /kv/name after DELETE
GET /stats
PUT /kv/test2
GET /kv/test2
```

The following persistence flow was also verified:

```text
PUT /kv/test2
        |
        v
WAL file becomes non-zero
        |
        v
Server restart
        |
        v
GET /kv/test2
        |
        v
Original value recovered
```

Example recovered response:

```json
{"key":"test2","value":"HELLO_WAL"}
```

---

# Future Improvements

Possible future improvements include:

- Snapshot/MVCC iterators
- Better HTTP request parsing
- Connection keep-alive support
- Authentication
- TLS support through a reverse proxy
- Persistent B+ Tree pages
- LSM-tree integration
- MemTable/SSTable integration
- Background compaction
- More extensive concurrency testing
- More comprehensive REST API integration tests
- Performance profiling and optimization

---

# License

Add the project's license information here if a specific license is selected.