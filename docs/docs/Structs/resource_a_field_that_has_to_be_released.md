# `#resource`: a field that has to be released

A field marked `#resource` holds something the compiler cannot release on its own. Saying so makes the type's `deinit` responsible for it: at `--safety full` a `deinit` that never mentions the field is an error, and so is having no `deinit` at all. Below `full` both are warnings.

**A descriptor that closes itself**

```rune
import std::io

extern "C" { fn close(fd: i32) -> i32 }

pub struct Socket {
    #resource fd: i32 = -1,
    pub port: i32 = 0
}

extend Socket {
    #safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 {
            io::println("close(" + self.fd.$str() + ")")
            close(self.fd)
            self.fd = -1
        }
    }
}

fn main() -> i64 {
    { var s = Socket { fd: 3, port: 80 }; io::println("serving on " + s.port.$str()) }
    0
}
```

**A `#resource` the `deinit` forgets**

```rune
import std::io

struct Socket {
    #resource fd: i32,
    port: i32
}

extend Socket {
    fn deinit(&self) { io::println("bye") }
}

fn main() -> i64 { let s = Socket { fd: 3, port: 80 }; 0 }
```

> [!NOTE]
> **Note**
>
> An enum may declare a `deinit` in exactly the same way, and a `mark` may require one — `std::net`'s `TcpStream` and `TcpListener` are both structs that own a descriptor this way.
