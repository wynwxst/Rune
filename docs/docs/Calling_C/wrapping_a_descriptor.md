# Wrapping a descriptor

A C API that hands out a descriptor hands out an obligation with it. A struct with a `#resource` field and a `deinit` is how that obligation is written down: the value closes itself, and the compiler will not let a second owner of it exist. This is exactly what `std::net` is built out of.

**A descriptor that closes itself**

```rune
import std::io

extern "C" {
    fn open(path: CString, flags: i32) -> i32
    fn close(fd: i32) -> i32
    fn read(fd: i32, buffer: *var u8, count: usize) -> i64
}

/// An open file descriptor, and the promise to close it.
pub struct Descriptor {
    #resource fd: i32 = -1
}

extend Descriptor {
    #safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 {
            close(self.fd)
            self.fd = -1
        }
    }

    pub fn isOpen(&self) -> bool { self.fd >= 0 }
}

#safe("open either returns a descriptor or -1, which is what is checked")
pub fn openRead(path: String) -> Descriptor? {
    let fd = unsafe { open(path.$cstr(), 0i32) }
    if fd < 0 { return nil }
    Descriptor { fd: fd }
}

fn main() -> i64 {
    match openRead("/etc/hosts") {
        Some(d) => io::println("opened: " + d.isOpen().$str()),
        None    => io::println("could not open it"),
    }
    // Nothing closes it explicitly; going out of scope does.
    0
}
```
