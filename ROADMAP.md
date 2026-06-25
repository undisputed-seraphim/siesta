# Roadmap — Siesta RPC

## High-Level Integration Plan

The REST (OpenAPI) and RPC (proto/OpenRPC) compilers merge into a single pipeline
by unifying on `Endpoint` as the shared IR. The type system (`NormalizedAST`) is
already shared.

```
.proto                  OpenRPC.json            OpenAPI.json
   │                        │                        │
   ▼                        ▼                        ▼
Spirit X3 parser      simdjson parser          simdjson parser
   │                        │                        │
   ▼                        ▼                        ▼
ProtoFile AST          OpenRPC DOM             OpenAPI DOM
   │                        │                        │
   ▼                        ▼                        ▼
ProtoBridge       materialiseMethods()    parseEndpoints()
   │                        │                        │
   ▼                        ▼                        ▼
┌──────────────────────────────────────────────────────────┐
│               RPCMethod → Endpoint bridge                │
│  (trivial: maps service+rpc → POST /rpc/{Svc}/{Method}) │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
              std::vector<Endpoint>     ← unified endpoint IR
                       │
           ┌───────────┴────────────┐
           ▼                        ▼
     NormalizedAST           DepGraph + Sort
           │                        │
           └───────────┬────────────┘
                       │
                       ▼
              CodegenArgs { ast, order, spec=nullptr, endpoints }
                       │
     ┌─────────────────┼───────────────────┐
     ▼                 ▼                    ▼
DefsGenerator     BeastServerGen       BeastClientGen
(defs.hpp/cpp)    (server.hpp/cpp)     (client.hpp)

     ┌─────────────────┼───────────────────┐
     ▼                 ▼                    ▼
BeastPythonGen   BeastServerPythonGen  (python bindings)
```

### Integration Tasks ✅

All four phases complete:

- [x] **Phase 1: RPCMethod → Endpoint bridge** (`RPCEndpointBridge.hpp`)
- [x] **Phase 2: Unified CodegenArgs** (no more `spec` field)
- [x] **Phase 3: Driver unification** (auto-detect format, single binary)
- [x] **Phase 4: Remove mock backend** (`generator-rpc/` deleted, folded into `generator/`)

---

## gRPC Feature Parity (HTTP/1.1)

### ✅ Already Implemented (Zero Work)

- [x] **Unary RPC** — POST JSON request body, JSON response body
- [x] **TLS** — `ssl_stream_type` in `Session` (variant stream)
- [x] **Compression** — `Content-Encoding: gzip` via `Config::compress`
- [x] **Token auth** — `Authorization: Bearer` header in `EndpointIR`
- [x] **API key auth** — `ApiKey` in `EndpointIR`
- [x] **Per-session deadlines** — `Config::read_timeout` / `write_timeout` / `idle_timeout`
- [x] **JSON-only encoding** — boost::json `tag_invoke` ser/des
- [x] **Server-side interceptors** — `request_context` + `Interceptor` chain on `ServerBase`; generated `handle_request()` wraps handler calls via `run_interceptors()`
- [x] **Client retry infrastructure** — `RetryConfig` in `siesta/common.hpp`; `ClientBase` has `_retry` + `set_retry()`/`retry()`. Retry loop not yet wired — generated code still calls `async_submit_request()` directly
- [x] **Backend-agnostic server** — `Session::make_response()` abstracts response construction; `Shared/MethodEmitter` and `Shared/DispatchEmitter` parameterized for multi-backend reuse; `siesta/common.hpp` for shared types
- [x] **StreamingMode IR** — `StreamingMode` on `Endpoint` (`None`, `ServerStream`, `ClientStream`, `Bidirectional`); `RPCEndpointBridge` maps proto streaming annotations; `emitWebSocketEndpoint()` skeleton in client codegen

### ✅ Implementable Within Existing Infrastructure

- [ ] **Server streaming** — WebSocket upgrade on RPC endpoints with `server_streaming = true`
  - Framework: `Session::upgrade_to_websocket()` already exists
  - Generated handler: `awaitable<void>` that writes JSON frames in a loop
  - Client: `ClientBase` WebSocket upgrade + frame read loop
  - Effort: ~80 lines of generated code per streaming RPC
  - StreamingMode IR + emitWebSocketEndpoint() skeleton ready; need actual read/write loop generation

- [ ] **Client streaming** — WebSocket upgrade on RPC endpoints with `client_streaming = true`
  - Server: read frames until stream-close message, then dispatch to handler
  - Client: write frames from async producer
  - Effort: ~100 lines

- [ ] **Bidirectional streaming** — WebSocket with both directions active
  - Server: read + write concurrently via `asio::co_spawn` on two coroutines
  - Client: same pattern
  - Effort: ~120 lines

- [ ] **Deadlines / timeouts** — per-RPC deadline header
  - Client sends `X-Deadline-Ms` header
  - Server reads header, sets `asio::system_timer`, cancels if exceeded
  - Effort: ~20 lines in generated dispatch
  - Cleaner path via interceptors: a deadline interceptor reads `X-Deadline-Ms` from `request_context.req`

- [ ] **Cancellation** — cancel in-flight RPC
  - Unary: close HTTP connection
  - Streaming: close WebSocket with close code
  - Framework: `Session::do_close()` already exists
  - Effort: ~15 lines of client-side helper

- [ ] **Metadata (headers)** — pass key-value metadata per RPC
  - gRPC metadata maps to HTTP headers 1:1
  - Server: access `req.base()` headers — fits naturally in an interceptor
  - Client: `_extra_headers` map, merged into request
  - Effort: ~30 lines in generated client + server

- [ ] **Trailers** — response metadata after body
  - HTTP/1.1 trailers via `Transfer-Encoding: chunked`
  - Beast supports trailers on responses (`http::response::trailers()`)
  - Note: some HTTP proxies strip trailers
  - Effort: ~25 lines in generated server

- [ ] **Health checking** — `/health` endpoint
  - Generated as a static-path GET endpoint returning `{"status":"SERVING"}`
  - Trivial now that ServerEmitter is split: add `emitHealthCheck()`
  - Response construction via `session->make_response()`
  - Effort: ~15 lines

- [ ] **Load balancing** — client-side connection distribution
  - Framework: connection pool (`pool.hpp`) already does round-robin
  - Add: weighted selection, health-aware routing
  - Effort: ~80 lines in `pool.hpp`

### 🔨 Needs New Infrastructure (Moderate Work)

- [ ] **Structured error model**
  - gRPC equivalent: `grpc::Status` (code + message + details)
  - Design: JSON error envelope `{"code": 5, "message": "...", "details": [...]}`
  - Server: generated `try/catch` around handler dispatch
  - Client: `extract_object` parses error envelope on non-2xx responses
  - Standard codes map: OK=0, CANCELLED=1, INVALID_ARGUMENT=3, NOT_FOUND=5, INTERNAL=13, etc.
  - `make_response()` already handles error response construction; interceptors can set `error_response` for short-circuiting
  - Effort: ~100 lines across generated code

- [ ] **Client-side interceptors / middleware**
  - gRPC equivalent: `ClientInterceptor` chain
  - Design: `before_submit` callback wrapping `async_submit_request`
  - Effort: ~50 lines

- [ ] **Client-side message size limits**
  - gRPC equivalent: `MaxRecvMessageSize` / `MaxSendMessageSize`
  - Framework: `Config::max_body_size` already limits incoming; add outgoing limit
  - Effort: ~15 lines

### 🔧 Needs New Infrastructure (Heavy Work)

- [ ] **Per-stream flow control**
  - gRPC equivalent: `WriteOptions::set_buffer_hint()` + per-message buffer limits
  - HTTP/1.1 has no per-stream flow control; rely on TCP backpressure
  - WebSocket: can implement application-level `ACK` messages for server→client
  - This is a NICE-TO-HAVE; TCP backpressure is sufficient for most workloads

- [ ] **gRPC ecosystem translation proxy** (future)
  - Standalone binary that translates gRPC ↔ JSON-RPC
  - Input: `.proto` service definitions
  - Depends on: Google `libprotobuf` for wire format
  - Server mode: listens gRPC, translates to JSON, forwards to our RPC server
  - Client mode: listens HTTP, translates to gRPC, forwards to gRPC server
  - Enables: `grpcurl` compatibility, Envoy gRPC filter integration
  - This is a SEPARATE PROJECT from the core RPC compiler

### Not Applicable / Not Planned

- [ ] **Protobuf wire format** — intentionally use JSON; delegate binary encoding to Google libprotobuf if needed
- [ ] **gRPC service reflection** — only relevant for gRPC ecosystem; can be part of the translation proxy
- [ ] **HTTP/2 transport** — HTTP/1.1 + WebSocket gives equivalent streaming; multiplexing via connection pool
- [ ] **Multiplexed streams over one connection** — requires HTTP/2 or QUIC; deferred to future transport backends
- [ ] **Java/Python compiled codegen** — OpenRPC spec covers only C++. Multi-language is out of scope.
