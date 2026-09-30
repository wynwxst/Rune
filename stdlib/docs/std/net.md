# std::net

TCP, in the shape `std::io`'s stream marks already describe: `listen` and
`connect` give a `TcpListener` and a `TcpStream`, and a `TcpStream` *is* an
`io::Stream`, so `BufferedReader`, `copy` and everything else written against
those marks works on a connection unchanged. Both own their descriptor and
close it in `deinit`.

## A round trip on one machine

The server thread listens on port 0 — whatever is free — and publishes the
port it got through a `Mutex`; the client connects to it and reads one line
back. A `TcpListener` owns its descriptor, so it lives on the thread that
made it rather than being handed across.

```rune
import std::io
import std::net
import std::thread
import std::time

global port: thread::Mutex<i64> = thread::Mutex<i64>(0)

fn serve(unused: i64) -> i64 {
    var listener = match net::listen("127.0.0.1", 0i32) {
        Ok(l) => l,
        Err(e) => { io::eprintln(net::describe(e)); return 1 },
    }
    var slot = port
    slot.set(listener.port() as i64)
    match listener.accept() {
        Ok(client) => {
            var conn = client
            var lines = io::BufferedReader<net::TcpStream>(conn)
            let line = lines.readLine() ?? ""
            lines.writeText("echo: " + line + "\n")
            0
        },
        Err(e) => { io::eprintln(net::describe(e)); 1 },
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
        Ok(stream) => {
            var conn = stream
            conn.writeText("hello\n")
            var lines = io::BufferedReader<net::TcpStream>(conn)
            io::println(lines.readLine() ?? "")
        },
        Err(e) => io::println(net::describe(e)),
    }
    server.join()
    0
}
```

## When it fails

Every failure is a `NetError`, and `describe` turns one into a sentence.

```rune
import std::io
import std::net

fn main() -> i64 {
    match net::connect("127.0.0.1", 1i32) {
        Ok(s) => io::println("connected?"),
        Err(net::NetError::Refused) => io::println("refused, as expected"),
        Err(e) => io::println(net::describe(e)),
    }
    0
}
```

## Sockets driven by tasks

`AsyncListener` and `AsyncStream` are the same sockets set not to wait: each
`accept`, `read` and `write` tries at once and, if nothing is ready, parks
the task on the socket until it is, while the other tasks run. One thread
serves many connections; `connectAsync` resolves and connects on a worker.

```rune
import std::io
import std::net
import std::task
import std::collections::vector

async fn serve(server: net::AsyncListener, count: i64) -> i64 {
    var served = 0
    while served < count {
        match server.accept().await {
            Ok(conn) => { handle(conn); served += 1 },
            Err(e) => { io::println(net::describe(e)); return served },
        }
    }
    served
}

async fn handle(conn: net::AsyncStream) {
    var c = conn
    if c.read(1024).await is Ok(bytes) {
        c.writeText("echo: " + bytes.toString()).await
        c.shutdown(net::Shutdown::Write)
    }
}

async fn client(port: i32, message: String) -> String {
    match net::connectAsync("127.0.0.1", port).await {
        Ok(conn) => {
            var c = conn
            c.writeText(message).await
            c.shutdown(net::Shutdown::Write)
            c.readAll().await.unwrap()
        },
        Err(e) => net::describe(e),
    }
}

async fn main() -> i64 {
    var server = net::listenAsync("127.0.0.1", 0).unwrap()
    let port = server.port()
    let serving = serve(server, 2)
    for r in task::all(vec![client(port, "one"), client(port, "two")]).await {
        io::println(r)
    }
    io::println(serving.await)
    0
}
```

A `TcpStream`, a `TcpListener` and the `Async` pair all have a `clone` that
duplicates the descriptor, so a `$clone()` — an `unwrap()` under single
ownership, a read out of a `Vector` — never leaves two owners with one.
