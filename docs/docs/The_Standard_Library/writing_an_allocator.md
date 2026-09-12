# Writing an allocator

`Allocator` is a mark, so a program can supply its own — an arena that frees everything at once, a pool of fixed-size blocks, or a counting wrapper around the system heap. Implement three methods and anything that takes an allocator will use it.

**A bump allocator**

```rune
import std::io
import std::mem

/// A bump allocator: hands out slices of one block and frees nothing until
/// the whole arena goes. Fast, and exactly right for a batch of short-lived
/// values that die together.
pub class Arena {
    block: *var u8
    size: usize
    used: usize
    handed: i64

    fn init(self, size: usize) {
        self.block = mem::allocator.allocate(size)
        self.size = size
        self.used = 0
        self.handed = 0
    }

    fn deinit(self) { mem::allocator.deallocate(self.block) }

    pub fn handedOut(&self) -> i64 { self.handed }
    pub fn usedBytes(&self) -> i64 { self.used as i64 }
}

bind mem::Allocator to Arena {
    /// Bumps the cursor. Returns null when the arena is full, which is what
    /// `mem::isNull` is for.
    @safe("the cursor never passes the size checked on the line above")
    fn allocate(&self, bytes: usize) -> *var u8 {
        // Keep every block 8-aligned, as the system allocator would.
        let need = (bytes + 7) / 8 * 8
        if self.used + need > self.size { return mem::noBlock() }
        let at = self.used
        self.used += need
        self.handed += 1
        unsafe { (self.block as u64 + at as u64) as *var u8 }
    }

    /// An arena frees in one go, so a single block going back is a no-op.
    fn deallocate(&self, block: *var u8) {}

    /// Growing in place is not something a bump allocator can do; hand back a
    /// fresh block and let the caller copy.
    fn reallocate(&self, block: *var u8, bytes: usize) -> *var u8 {
        self.allocate(bytes)
    }
}

@safe("every block below is sized and written through its own pointer")
fn main() -> i64 {
    let arena = Arena(1024 as usize)

    // Three blocks of four i64 each, straight out of the arena.
    var round = 0
    while round < 3 {
        let block = arena.allocate(4 as usize * mem::size_of<i64>())
        if mem::isNull(block) { break }
        let cells = unsafe { block as *var i64 }
        var i = 0
        while i < 4 {
            unsafe { cells[i] = (round + 1) * (i + 1) }
            i += 1
        }
        io::println("block " + round.$str() + " ends with " +
                    unsafe { cells[3] }.$str())
        round += 1
    }

    io::println("handed out " + arena.handedOut().$str() + " blocks, " +
                arena.usedBytes().$str() + " bytes")
    0
}
```

> [!NOTE]
> **Three methods**
>
> The three methods are the whole contract: `allocate` returns a block or null, `deallocate` takes one back, and `reallocate` resizes. Nothing else in the language needs to know which allocator it is talking to.
