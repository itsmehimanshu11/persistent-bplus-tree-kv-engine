# REST API Guide

The REST API is a thin HTTP adapter over `KVEngine`. It does not contain storage logic; all reads and writes still go through the existing B+ Tree + WAL engine.

## Start the server

```bash
cmake -S . -B build
cmake --build build --config Release
build/kv_rest_server.exe
```

Defaults:

- Host: `127.0.0.1`
- Port: `8080`
- WAL directory: `./wal_data`
- Maximum request body: 1 MiB

Optional arguments:

```text
kv_rest_server [port] [host]
```

Example:

```text
build/kv_rest_server.exe 8080 127.0.0.1
```

## API

### Health

```http
GET /health
```

Response:

```json
{"status":"ok"}
```

### Put / update

The request body is stored as the value.

```http
PUT /kv/user%3A1001
Content-Type: text/plain

Alice
```

Response:

```json
{"status":"ok","key":"user:1001"}
```

### Get

```http
GET /kv/user%3A1001
```

Response:

```json
{"key":"user:1001","value":"Alice"}
```

A missing key returns HTTP `404`.

### Delete

```http
DELETE /kv/user%3A1001
```

Response:

```json
{"status":"ok","key":"user:1001"}
```

### Statistics

```http
GET /stats
```

The response contains B+ Tree size, node count, height, memory usage and WAL statistics.

### Flush and sync

```http
POST /flush
POST /sync
```

`flush` flushes buffered WAL data. `sync` also requests durable OS-level synchronization through the existing WAL implementation.

## curl examples

```bash
curl http://127.0.0.1:8080/health
curl -X PUT --data "Alice" http://127.0.0.1:8080/kv/user%3A1001
curl http://127.0.0.1:8080/kv/user%3A1001
curl http://127.0.0.1:8080/stats
curl -X DELETE http://127.0.0.1:8080/kv/user%3A1001
curl -X POST http://127.0.0.1:8080/flush
curl -X POST http://127.0.0.1:8080/sync
```

## Design notes

- The server uses HTTP/1.1 with `Connection: close` for simple request/response handling.
- Multiple client connections are handled concurrently using one detached worker thread per connection.
- `KVEngine` already protects reads/writes with `std::shared_mutex`, so the API does not add a second storage-level locking layer.
- JSON escaping is implemented locally, so no third-party JSON dependency is required.
- The default loopback bind prevents accidental public exposure.
- For public deployment, add authentication, TLS and a reverse proxy/API gateway rather than exposing this development server directly.
