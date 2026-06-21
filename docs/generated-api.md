# Generated API Reference

How to use the generated client, server, and Python bindings.

All generated code lives in a single namespace derived from the spec's
`info.title` (sanitized) or the `--namespace` CLI flag. The examples
below use `Echo_API` as the namespace.

## Client (`client.hpp`)

### Construction

The client must be heap-allocated in a `std::shared_ptr`:

```cpp
boost::asio::io_context ctx;

// Default config
auto client = std::make_shared<Echo_API::Client>(ctx);

// Custom timeouts (zero = disabled)
Echo_API::Client::Config conf;
conf.connect_timeout = std::chrono::milliseconds(5000);
conf.read_timeout = std::chrono::milliseconds::zero();
conf.write_timeout = std::chrono::milliseconds::zero();
auto client = std::make_shared<Echo_API::Client>(ctx, conf);
```

When the spec defines `securitySchemes`, the constructor takes an
additional auth parameter:

```cpp
// HttpBearer — token is pre-computed as "Bearer <token>" in the constructor
auto client = std::make_shared<Api::Client>(ctx, "my-bearer-token");

// ApiKey
auto client = std::make_shared<Api::Client>(ctx, "my-api-key");
```

### Lifecycle

```cpp
// 1. Connect
client->start(boost::asio::ip::make_address("127.0.0.1"), 8080);
ctx.run();

// 2. Make requests (see below)

// 3. Disconnect
client->stop();
```

### Method naming

Each OpenAPI operation becomes a method on the `Client` class:

| OpenAPI | Generated method |
|---------|-----------------|
| `GET /echo` | `get__echo(...)` |
| `POST /echo` | `post__echo(...)` |
| `GET /echo/{id}` | `get__echo__id(...)` |
| `DELETE /echo/{id}` | `delete__echo__id(...)` |
| `PUT /items/{id}` | `put__items__id(...)` |
| `GET /items/{itemId}/tags/{tagIndex}` | `get__items__itemId_tags__tagIndex(...)` |

Pattern: `{verb}__{path}` where slashes become `_`, path parameter
braces are removed, and special characters become `_`. The HTTP `delete`
verb becomes `delete_` to avoid the C++ keyword.

### Parameter mapping

Parameters appear in the method signature in this order:

| OpenAPI parameter | C++ type | Position |
|-------------------|----------|----------|
| Request body (`requestBody`) | `const T& body` | First |
| Path parameter (required) | `int64_t id`, `std::string name` | After body |
| Query parameter (required) | `int32_t category`, `std::string q` | After path params |
| Query parameter (optional) | `std::optional<int32_t> limit` | After required params |
| Header parameter (optional) | `std::optional<std::string> h` | After query params |
| Completion token | `auto&& token` | Always last |

Examples from a generated client:

```cpp
// GET /echo?message=...  (required query + optional header)
auto get__echo(std::string message,
               std::optional<std::string> headerParam,
               auto&& token);

// POST /items  (request body only)
auto post__items(const Item& body, auto&& token);

// PUT /items/{id}  (body + path param)
auto put__items__id(const Item& body, int64_t id, auto&& token);

// GET /items?limit=...&status=...  (two optional query params)
auto get__items(std::optional<int32_t> limit,
                std::optional<std::string> status,
                auto&& token);
```

### Completion tokens

The last parameter is a boost::asio completion token. The method works
with any token type:

```cpp
// use_future — synchronous
auto future = client->get__echo("hello", std::nullopt, boost::asio::use_future);
ctx.restart();
ctx.run();
auto outcome = future.get();

// Callback
client->get__echo("hello", std::nullopt, [](Echo_API::Client::outcome_type outcome) {
    if (outcome.has_value()) {
        std::cout << outcome.value().body() << std::endl;
    }
});
ctx.restart();
ctx.run();

// C++20 coroutine
auto outcome = co_await client->get__echo("hello", std::nullopt, boost::asio::use_awaitable);
```

### Outcome type

All methods complete with `outcome_type`, which is
`boost::outcome_v2::std_outcome<response_type>`:

```cpp
auto outcome = future.get();

if (outcome.has_value()) {
    // 2xx response
    auto& response = outcome.value();
    std::string body = response.body();
    auto status = response.result();  // e.g., http::status::ok
} else {
    // Non-2xx response (4xx, 5xx)
    auto ec = outcome.error();
    int status_code = ec.value();  // e.g., 404
}
```

Non-2xx HTTP responses are returned as errors, not values. The error
code's value is the HTTP status code.

## Server (`server.hpp` + `server.cpp`)

### Subclassing

The generated `Server` is abstract — every endpoint is a pure virtual
method. Subclass it and implement all handlers:

```cpp
struct MyServer : Echo_API::Server {
    using Echo_API::Server::Server;

    void get__echo(const request req, Session::Ptr session) override {
        auto& resp = session->get_response();
        resp.result(boost::beast::http::status::ok);
        resp.body() = "{\"message\":\"hello\"}";
        resp.set(boost::beast::http::field::content_type, "application/json");
        resp.prepare_payload();
        session->write();
    }

    // ... implement all other virtual methods
};
```

### Handler pattern

Every handler follows the same pattern:

1. Parse the request (URL, headers, body) from `req`
2. Fill the response via `session->get_response()`
3. Call `session->write()` to send the response

The `Session::Ptr` (`std::shared_ptr<Session>`) keeps the connection
alive while the handler runs. Call `write()` exactly once per handler
invocation.

### Construction & lifecycle

```cpp
boost::asio::io_context ctx;

// Default config
MyServer server(ctx);

// Custom timeouts (zero = disabled)
siesta::beast::ServerBase::Config conf;
conf.read_timeout = std::chrono::milliseconds::zero();
conf.write_timeout = std::chrono::milliseconds::zero();
MyServer server(ctx, conf);

// Start accepting connections
server.start(boost::asio::ip::make_address("0.0.0.0"), 8080);
ctx.run();  // blocks until io_context is stopped
```

### Virtual method signatures

All handler methods have the same signature:

```cpp
virtual void method_name(const request, Session::Ptr) = 0;
```

Where `request` is `boost::beast::http::request<boost::beast::http::string_body>`.

The method name follows the same `{verb}__{path}` convention as the
client. The server must implement every endpoint — there are no default
implementations.

### Session

| Method | Purpose |
|--------|---------|
| `get_response()` | Returns a mutable reference to the HTTP response |
| `write()` | Sends the response and resumes the connection's read loop |
| `id()` | Returns a unique connection identifier |

### Dispatch

The generated `server.cpp` contains the dispatch table. Incoming requests
are routed by path and HTTP verb:

- **Static paths** (`/echo`, `/items`): O(1) hash map lookup by
  `(path, verb)` pair
- **Parameterised paths** (`/echo/{id}`, `/items/{id}/tags/{index}`):
  linear segment-by-segment matching
- **Unmatched routes**: automatic 404 response (no handler called)

Static paths are checked first, then parameterised paths.

## Python Client (`py_module.cpp`)

The generated nanobind module wraps the C++ client with a synchronous
Python API.

### Import and construction

```python
from Echo_API import Client

# Connect on construction
client = Client("127.0.0.1", 8080)

# Reconnect
client.start("127.0.0.1", 9090)

# Disconnect
client.stop()
```

The module name is the sanitized `info.title` from the spec.

### Calling endpoints

All methods are synchronous — they block until the response arrives:

```python
result = client.get__echo("hello")
# result is a Python dict: {"message": "hello"}

result = client.post__items({"id": 1, "name": "widget"})

# Optional params default to None
result = client.get__items(limit=10)
result = client.get__items()  # no params
```

Return value is a Python `dict` parsed from the JSON response body.
On error, returns a dict with an `"error"` key containing the exception
message.

## Python Server (`server_py.cpp`)

The generated server trampoline module enables Python-side subclassing.

```python
from Echo_API_server import Server

class MyServer(Server):
    def get__echo(self, req):
        return '{"message": "hello from python"}'

server = MyServer()
server.listen("0.0.0.0", 8080)
# server.shutdown() to stop
```

The module name is `{title}_server` (e.g., `Echo_API_server`).

## Limitations

1. **No typed response objects.** All client methods return the raw HTTP
   response (`outcome_type`). The response body is a string — the caller
   must parse it with `boost::json::parse()` and `value_to<T>()`.

2. **No automatic body deserialization in server handlers.** Handlers
   receive the raw `request` object. The body is a string that must be
   parsed manually.

3. **Python client is synchronous only.** No async/await support. Each
   call blocks on `io_context.run()`.

4. **Only the first content-type is used.** If a `requestBody` has
   multiple content types, only the first entry is used for code generation.

5. **Parameter name sanitization.** Parameter names that collide with C++
   keywords get a `param_` prefix (`token` → `param_token`,
   `type` → `param_type`). Brackets and special characters become `_`.
