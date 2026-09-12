# webserver

A multi-threaded HTTP server in about 500 lines of Rune, over `std::net`,
`std::io` and `std::thread`.

```
rune run -- --port 8080 --workers 4 --verbose
```

Then open <http://localhost:8080/>.

```
rune run -- --help
usage: webserver [--address A] [--port N] [--workers N] [--limit N] [--verbose]
```

`--limit N` stops after N connections, which is what makes it testable: the
process ends on its own instead of waiting for a signal.

## What is where

| File | Contents |
|------|----------|
| `src/main.rune` | The entry point, and nothing else. |
| `src/server.rune` | Command line, the listener, and the accept loop. |
| `src/pool.rune` | The worker threads, and one connection's conversation. |
| `src/http.rune` | Requests, responses, and the text handling they need. |
| `src/routes.rune` | The pages. |
| `src/lib.rune` | The library root, so `tests/` can reach the parts. |

The package is a library plus a binary. That is what lets `tests/basics.rune`
check the request parser and the routes without opening a socket: a test
program imports the package, and only a library has anything to import.

## The shape of it

One thread accepts and does nothing else. A fixed pool of workers takes
connections off a `thread::Channel` and answers them. Nothing is allocated per
connection except the connection.

```
        ┌──────────┐   descriptors   ┌──────────┐
        │  accept  │ ──────────────▶ │ Channel  │
        └──────────┘                 └────┬─────┘
                                          │
                            ┌─────────────┼─────────────┐
                            ▼             ▼             ▼
                        ┌────────┐   ┌────────┐   ┌────────┐
                        │ worker │   │ worker │   │ worker │
                        └────────┘   └────────┘   └────────┘
```

## Handing a connection to another thread

A `net::TcpStream` owns its file descriptor. It has a `deinit` that closes it,
so a connection that goes out of scope is closed whether or not the code that
had it remembered to — and handing one to a function *moves* it, so there is
never a second owner to close it twice. At `safety = "full"` the compiler
enforces both.

That is exactly why a `TcpStream` must not be sent through a channel: a
channel stores what it carries through raw memory the compiler cannot follow,
so it would produce a second copy of the descriptor with nothing to say which
of them owns it. What crosses instead is the descriptor itself:

```rune
// on the accepting thread
queue.send(conn.release())        // `conn` stops owning it

// on the worker
var conn = net::adopt(descriptor) // the worker owns it now
```

Between the two calls it is an `i64`: nothing owns it, and nothing can close
it by accident.

## What a connection's lifetime looks like

```rune
fn converse(conn: net::TcpStream, verbose: bool) {
    var lines = io::BufferedReader<net::TcpStream>(conn)
    ...
}
```

`conn` is taken by value, so this call owns the connection; it is then moved
into the reader, which owns it until the reader goes. Nothing closes anything
explicitly, and the descriptor is closed exactly once.

## Routes

Routing is a `match` on the path rather than a table. A table would have to be
shared between the workers; a `match` is code every thread already has. Adding
a page means adding an arm to `routes::answer`.

| Path | What it does |
|------|--------------|
| `/` | A page with links to the rest. |
| `/health` | `ok`, for a load balancer. |
| `/time` | How long the process has been up, as JSON. |
| `/echo` | The request read back: method, path, query, headers, body. |

Anything else is a 404, and anything that is not `GET` or `HEAD` is a 405.

## What it does not do

It speaks enough HTTP/1.1 to answer a browser and no more: no chunked
transfer, no compression, no TLS, no static files. Keep-alive works,
`Content-Length` is always sent, and a body larger than
`http::BODY_LIMIT` is refused rather than read.
