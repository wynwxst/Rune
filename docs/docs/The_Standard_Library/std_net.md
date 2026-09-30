# std::net

TCP, as streams. `listen` and `connect` are the two ways in, and what comes back is an `io::Stream` — so `readLine`, `writeText`, `BufferedReader` and `io::copy` all work over a socket without knowing it is one.

| Member | Signature | Does |
| --- | --- | --- |
| `listen` | `(address: String, port: i32) -> Result<TcpListener, NetError>` | port `0` asks the system to pick one |
| `listenWith` | `(address, port, backlog: i32) -> …` | the same, with the backlog said out loud |
| `connect` | `(host: String, port: i32) -> Result<TcpStream, NetError>` | a name or a dotted address; the platform resolves it |
| `TcpListener::accept` | `(&var self) -> Result<TcpStream, NetError>` | waits for the next connection |
| `TcpListener::port` | `(&self) -> i32` | the port it actually got |
| `TcpStream` | `struct`, binds `io::Stream` | one end of a connection |
| `peerAddress` / `port` | `(&self)` | who is at the other end |
| `setNoDelay` | `(&var self, on: bool) -> NetError?` | send small writes immediately |
| `setReadTimeout` / `setWriteTimeout` | `(&var self, milliseconds: i64) -> NetError?` | zero waits for ever, which is the default |
| `shutdown` | `(&var self, how: Shutdown) -> NetError?` | `Read`, `Write` or `Both` |
| `close` | `(&var self)` | now rather than later |
| `release` / `adopt` | `(&var self) -> i64` / `(i64) -> TcpStream` | hand the descriptor over, and take one |
| `clone` | `(&self) -> Self` | a second descriptor for the same socket; what `$clone()` does |
| `AsyncStream` / `AsyncListener` | `class` | the same sockets for tasks: `read`, `write`, `writeText`, `readAll` and `accept` are `async fn`s that park the task on the socket |
| `listenAsync` | `(address: String, port: i32) -> Result<AsyncListener, NetError>` | listen, for tasks |
| `connectAsync` | `(host: String, port: i32) -> Future<Result<AsyncStream, NetError>>` | connect without holding the thread |
| `wrap` | `(stream: TcpStream) -> AsyncStream` | drive an existing connection with tasks |

*`NetError` is `Refused`, `AddressInUse`, `Unreachable`, `WouldBlock`, `Interrupted`, `TimedOut`, `Reset`, `PermissionDenied`, `NotFound`, `Closed` or `Failed`; `describe` puts it in words. See **Tasks and futures** for the `Async` pair.*

Both types own their descriptor the way [Structs](#structs) describes: the field is `@resource`, the `deinit` closes it, and handing one on is a move. So a connection closes itself when the binding holding it goes, and there is never a second owner to close it twice.

**A connection over the loopback**

```rune
import std::io
import std::net
import std::thread
import std::time

/// The port the server ended up on, so the client knows where to knock.
global port: thread::Mutex<i64> = thread::Mutex<i64>(0)

fn serve(unused: i64) -> i64 {
    var listener = match net::listen("127.0.0.1", 0i32) {
        Ok(l) => l,
        Err(e) => { io::eprintln("listen: " + net::describe(e)); return 1 },
    }
    var slot = port
    slot.set(listener.port() as i64)

    match listener.accept() {
        Ok(c) => {
            var conn = c
            // A socket is a stream, so this is the same reader a file gets.
            var lines = io::BufferedReader<net::TcpStream>(conn)
            match lines.readLine() {
                Some(line) => io::println("server got: " + line),
                None => io::println("server got nothing"),
            }
            lines.writeText("pong\n")
            0
        },
        Err(e) => { io::eprintln("accept: " + net::describe(e)); 1 },
    }
}

fn main() -> i64 {
    var server = thread::spawn(serve, 0)

    var slot = port
    var waited = 0
    while slot.get() == 0 && waited < 200 {
        thread::sleep(time::milliseconds(10))
        waited += 1
    }

    match net::connect("127.0.0.1", slot.get() as i32) {
        Ok(c) => {
            var conn = c
            conn.setNoDelay(true)
            conn.writeText("ping\n")
            var lines = io::BufferedReader<net::TcpStream>(conn)
            match lines.readLine() {
                Some(line) => io::println("client got: " + line),
                None => io::println("client got nothing"),
            }
        },
        Err(e) => io::eprintln("connect: " + net::describe(e)),
    }
    server.join()
    0
}
```

> [!WARNING]
> **Crossing a boundary**
>
> A `TcpStream` must not be put in a container or sent through a channel: both store what they carry through memory the compiler cannot follow, so there would be two copies of one descriptor and nothing to say which owns it. `release` hands the descriptor out as a plain `i64` — nothing owns it, so nothing can close it — and `adopt` takes it back. `examples/project/webserver` is a worker pool built that way.
