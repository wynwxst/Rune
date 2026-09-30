# Sockets driven by tasks

`std::net`'s `TcpStream` blocks: a `read` holds the thread until bytes arrive. `net::AsyncStream` and `net::AsyncListener` are the same sockets set not to wait: each `read`, `write` and `accept` tries at once and, if nothing is ready, parks the task on the socket — `task::readable`, `task::writable` — until it is, while the other tasks run. The executor watches every parked socket with `poll`, alongside its timers. Nothing about the bytes changes; only who waits, and how.

**An echo server and its clients, on one thread**

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

Resolving a name and connecting both wait on the network, so `connectAsync` does that part on a worker and parks the task until it is done. Files are not sockets: an operating system cannot say a regular file is "ready", so file I/O goes through `offload`, as it does in every runtime of this kind.
