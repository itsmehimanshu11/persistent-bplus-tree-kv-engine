# KV Engine - Verified Interview Demo Guide

## 1. What to Say in 30 Seconds

> "I built an embedded persistent key-value storage engine in C++17. The core index is a balanced B+ Tree that supports O(log n) point operations and O(log n + k) ordered range scans. I implemented node splitting, deletion rebalancing with redistribution and merge, linked leaf traversal, and an iterator API. For durability, the engine writes PUT and DELETE operations to a CRC32-checked Write-Ahead Log and replays it during startup recovery. I also added reader/writer synchronization, an interactive CLI, integrity verification, automated tests, benchmarks, and a dependency-free REST API for accessing the storage engine over HTTP."

---

# 2. Architecture

```text

                 Client

                /      \

               v        v

             CLI      REST API

               \        /

                v      v

                 KVEngine

                /       \

               v         v

            B+ Tree      WAL

               |          |

              RAM        Disk

```

The REST API is an adapter around `KVEngine`.

It does not directly manipulate the B+ Tree or WAL.

```text

HTTP Request

     |

     v

REST Server

     |

     v

KVEngine

   /   \

  v     v

B+Tree WAL

```

---

# 3. CLI Demo

Start the CLI:

```powershell

.\build\kv_engine_main.exe

```

Then demonstrate:

```text

put user:1001 Alice

put user:1002 Bob

put user:1003 Charlie

scan user:1001 user:1004

get user:1002

del user:1002

get user:1002

stats

verify

walstats

flush

sync

quit

```

Expected concepts to explain:

- `put` inserts or updates a key.

- `get` performs a B+ Tree lookup.

- `scan` uses the ordered leaf links.

- `del` removes a key and may trigger rebalancing.

- `stats` displays engine/tree/WAL statistics.

- `verify` checks B+ Tree integrity.

- `walstats` displays WAL information.

- `flush` flushes WAL buffers.

- `sync` performs durable synchronization.

---

# 4. REST API Demo

Start the REST server:

```powershell

.\build\kv_rest_server.exe

```

The server listens by default on:

```text

http://127.0.0.1:8080

```

Available endpoints:

```text

GET     /health

GET     /kv/{key}

PUT     /kv/{key}

DELETE  /kv/{key}

GET     /stats

POST    /flush

POST    /sync

```

## Health Check

```powershell

curl.exe http://127.0.0.1:8080/health

```

Expected:

```json

{"status":"ok"}

```

---

## PUT

Insert a value:

```powershell

curl.exe -X PUT http://127.0.0.1:8080/kv/name -d "Himanshu"

```

Expected:

```json

{"status":"ok","key":"name"}

```

---

## GET

```powershell

curl.exe http://127.0.0.1:8080/kv/name

```

Expected:

```json

{"key":"name","value":"Himanshu"}

```

---

## DELETE

```powershell

curl.exe -X DELETE http://127.0.0.1:8080/kv/name

```

Expected:

```json

{"status":"ok","key":"name"}

```

After deletion:

```powershell

curl.exe http://127.0.0.1:8080/kv/name

```

Expected:

```json

{"error":"NotFound: Key not found"}

```

---

## Statistics

```powershell

curl.exe http://127.0.0.1:8080/stats

```

The response contains B+ Tree and WAL statistics such as:

```text

keys

nodes

height

memory_bytes

wal statistics

```

---

**# 5. Persistence Demo

This is one of the strongest demonstrations because it proves that the engine is not simply an in-memory key-value store.

Verified persistence flow

The REST server was used to write a value, stop the process, start it again, and read the same value successfully.

Step 1 - Start the server

.\\build\\kv_rest_server.exe

Step 2 - Insert persistence test data

curl.exe -X PUT http://127.0.0.1:8080/kv/persistence_test -d "WAL_RECOVERY_WORKS"

Step 3 - Stop the server

Press:

Ctrl+C

Step 4 - Start the server again

.\\build\\kv_rest_server.exe

Step 5 - Read the same key after restart

curl.exe http://127.0.0.1:8080/kv/persistence_test

Verified result:

{"key":"persistence_test","value":"WAL_RECOVERY_WORKS"}

What this proves

The B+ Tree is an in-memory structure, so the process restart removes the in-memory index. The WAL preserves the mutation records on disk, and startup recovery replays those records to reconstruct the B+ Tree.

What to Say

"I verified persistence through the REST API. I inserted persistence_test with the value WAL_RECOVERY_WORKS, stopped the server, restarted it, and successfully read the same value. That demonstrates WAL-backed recovery across a process restart."

6. Important Implementation Points**

## B+ Tree

- Internal nodes route searches using separator keys.

- Leaf nodes store the actual key/value pairs.

- Leaf nodes are linked using `next_leaf` for ordered scans.

- Insert splits a full leaf or internal node.

- A separator key is promoted to the parent after a split.

- Splitting can propagate upward.

- A new root can be created when the old root splits.

- Delete removes the key and checks for underflow.

- An underfull node first attempts redistribution from a sibling.

- If redistribution is not possible, nodes are merged.

- Parent separator keys are updated during rebalancing.

- The root can collapse when appropriate.

---

# 7. WAL

PUT and DELETE operations are written to the WAL before the B+ Tree is modified.

Conceptually:

```text

PUT / DELETE

      |

      v

Write WAL record

      |

      v

Optional Sync

      |

      v

Modify B+ Tree

```

WAL records contain information including:

- Sequence number

- Timestamp

- Operation type

- Key/value sizes

- Key/value data

- CRC32 checksum

The WAL supports:

- Sequential writes

- Sequence numbers

- CRC32 validation

- File rotation

- Recovery

- Flush

- Durable synchronization

---

# 8. WAL Recovery

During startup:

```text

WAL files

    |

    v

Read records

    |

    v

Validate CRC32

    |

    v

Replay PUT / DELETE

    |

    v

Rebuild B+ Tree

```

Recovery scans WAL files in numeric file order.

Complete records with invalid CRC32 are reported as corruption.

A trailing incomplete record is treated as a possible crash tail and ignored.

This allows the engine to recover from an interrupted final WAL write.

---

# 9. Flush vs Sync

### Flush

`flush` writes buffered data from the C++ stream to the operating system.

### Sync

`sync` performs:

```text

C++ stream flush

       |

       v

OS-level synchronization

```

On POSIX systems this uses `fsync`.

On Windows it uses `_commit`.

The important distinction is that `sync` provides a stronger durability request than simply flushing the application stream.

---

# 10. Concurrency

`KVEngine` uses:

```cpp

std::shared_mutex

```

Read operations use shared locking:

```cpp

std::shared_lock<std::shared_mutex>

```

Write operations use exclusive locking:

```cpp

std::unique_lock<std::shared_mutex>

```

Conceptually:

```text

GET / Stats

     |

     v

Shared Lock

     |

     v

B+ Tree

PUT / DELETE

     |

     v

Exclusive Lock

     |

     v

WAL + B+ Tree

```

A B+ Tree iterator holds a shared tree lock for its lifetime, so a concurrent writer waits rather than modifying the tree underneath the iterator.

### Important

Do not call this:

- Snapshot isolation

- MVCC

- Lock-free concurrency

The current design is based on reader/writer locking.

---

# 11. REST API Design

The REST layer is intentionally separate from the storage engine.

```text

HTTP

 |

 v

REST Server

 |

 v

KVEngine API

 |

 +------ B+ Tree

 |

 +------ WAL

```

This means the storage engine can still be used directly through the C++ API or CLI without requiring the REST server.

The REST server is dependency-free and implements a small HTTP/1.1 interface.

---

**# 12. REST API Verification

The REST layer was tested with real HTTP requests for:

Health check

PUT / insert

GET / lookup

PUT / update

DELETE

Not-found handling

Ordered range scanning

Persistence across server restart

The REST adapter remains separate from KVEngine, so the same storage engine can still be used through the native C++ API and CLI.

12. Tests**

The project contains three main CTest targets:

```text

btree_test

kv_engine_integration_test

wal_test

```

Coverage includes:

- Basic B+ Tree operations

- Insert/update

- Lookup

- Delete

- Node splitting

- Range scans

- Deletion/rebalancing stress

- Persistence recovery

- Concurrency

- WAL rotation

- CRC corruption detection

Run:

```powershell

ctest --test-dir build --output-on-failure

```

---

# 13. Benchmarks

Run:

```powershell

.\build\kv_benchmark.exe

```

The benchmark currently measures:

- B+ Tree insert

- B+ Tree lookup

- Range scan

- Delete

- `std::map` insertion

- `std::unordered_map` insertion

Only report benchmark numbers generated on the machine being discussed.

The current benchmark is single-threaded and does not implement a Zipfian multi-threaded workload.

---

# 14. Complexity

For a balanced B+ Tree:

| Operation | Complexity |

|---|---|

| Point lookup | O(log n) |

| Insert/update | O(log n) amortized |

| Delete | O(log n) amortized |

| Range scan of `k` results | O(log n + k) |

| Space | O(n) |

The initial tree traversal costs `O(log n)`, followed by sequential traversal of the linked leaves for the requested range.

---

# 15. Interview Questions

## Why B+ Tree instead of a hash table?

A hash table is excellent for exact key lookups but does not maintain sorted order.

The B+ Tree provides:

- Efficient point lookup

- Ordered keys

- Efficient range scans

- Sequential leaf traversal

So the B+ Tree is a better fit when both point operations and ordered range queries are required.

---

## Why B+ Tree instead of B-Tree?

A B+ Tree keeps actual records in the leaf nodes and links the leaves.

Internal nodes primarily act as routing structures.

This makes ordered sequential and range traversal convenient.

```text

Root

 |

 v

Internal Nodes

 |

 v

Leaf -> Leaf -> Leaf -> Leaf

```

---

## What happens when a node becomes full?

The node is split into two nodes.

A separator key is promoted to the parent.

If the parent also becomes full, the split can propagate upward.

If the root splits, a new root is created.

---

## How does deletion work?

After deleting a key, the tree checks whether the node has become underfull.

The node first tries to borrow entries from a sibling.

If redistribution is not possible, it merges with a sibling.

The corresponding separator is then removed or updated in the parent.

If necessary, this process propagates upward.

The root can collapse when it has only one child.

---

## How does recovery work?

The engine writes PUT and DELETE operations to the WAL before modifying the B+ Tree.

During startup:

1. WAL files are opened.

2. Records are read.

3. CRC32 is validated.

4. Records are replayed in sequence order.

5. PUT operations reconstruct values.

6. DELETE operations remove keys.

7. The in-memory B+ Tree is reconstructed.

---

## Why use a WAL?

The B+ Tree is currently an in-memory structure.

Without a WAL:

```text

Process stops

     |

     v

RAM is lost

     |

     v

Data is lost

```

With a WAL:

```text

Process stops

     |

     v

WAL remains on disk

     |

     v

Process restarts

     |

     v

Replay WAL

     |

     v

Reconstruct B+ Tree

```

Therefore the WAL provides persistence and crash recovery without requiring the B+ Tree itself to be stored as an on-disk page structure.

---

## Is this an LSM Tree?

No.

The active `KVEngine` architecture is:

```text

B+ Tree + WAL

```

There are additional `MemTable`, `SSTable` and `flush` components in the repository, but they are experimental and are not integrated into `KVEngine` as a complete LSM implementation.

---

## Is it snapshot isolation?

No.

The current implementation uses reader/writer locks.

Iterators hold a shared tree lock for their lifetime rather than providing MVCC snapshots.

---

## Does the REST API replace the storage engine?

No.

The REST API is only an interface layer.

```text

REST API

    |

    v

KVEngine

   / \

  /   \

B+Tree WAL

```

The same `KVEngine` can be accessed through the C++ API, CLI or REST API.

---

## What happens if a key does not exist?

The REST API returns a not-found error.

Example:

```powershell

curl.exe http://127.0.0.1:8080/kv/unknown

```

Response:

```json

{"error":"NotFound: Key not found"}

```

---

## Why does the REST server default to 127.0.0.1?

To avoid accidentally exposing the storage engine to the network.

The default is:

```text

127.0.0.1:8080

```

If the server is intentionally bound to `0.0.0.0`, authentication and TLS/reverse-proxy protection should be added before public deployment.

---

# 16. What Would You Improve Next?

A strong answer is:

> "The next improvements would depend on the workload. For the storage engine itself, I would add an on-disk B+ Tree page format or checkpointing layer, MVCC or snapshot iterators, and finer-grained concurrency such as node-level locking. For write-heavy workloads, I could integrate the experimental MemTable/SSTable components into a complete LSM architecture. On the service side, I would add authentication, stronger HTTP handling, and TLS through a reverse proxy."

---

# 17. Important Things NOT to Claim

Do not claim that the project currently has:

- A complete LSM-tree architecture

- MVCC

- Snapshot isolation

- Lock-free concurrency

- Node-level lock coupling

- Persistent B+ Tree pages

- Multi-threaded benchmark results

- Authentication

- TLS

- Production-ready public deployment

These are future improvements, not current features.

---

# 18. Best Demo Order

For an interview, use this order:

```text

1. Explain architecture

        |

        v

2. Show B+ Tree CLI

        |

        v

3. Show REST API

        |

        v

4. PUT / GET / DELETE

        |

        v

5. Show /stats

        |

        v

6. Show WAL file

        |

        v

7. Restart server

        |

        v

8. GET previously stored key

        |

        v

9. Run tests

        |

        v

10. Run benchmark

```

The most impressive demonstration is the persistence flow:

```text

PUT

 |

 v

WAL written

 |

 v

Server stopped

 |

 v

Server restarted

 |

 v

WAL recovery

 |

 v

GET returns original value

```

---

# 19. Final Project Description

### Short Resume Version

> **Persistent B+ Tree Key-Value Storage Engine — C++17**  

> Built an embedded persistent key-value storage engine using a balanced B+ Tree and CRC32-validated Write-Ahead Log. Implemented node rebalancing, ordered range scans, WAL recovery, reader/writer synchronization, CLI, automated tests, benchmarks, and a dependency-free HTTP/1.1 REST API.

### Interview Version

> "This project is essentially a small storage engine. The B+ Tree provides the in-memory indexing and ordered access, while the WAL provides durability and crash recovery. I kept the REST API as a separate adapter layer so the underlying storage engine remains independent of HTTP. I also added tests, integrity verification, statistics and benchmarks so the implementation can be evaluated beyond just basic CRUD operations."