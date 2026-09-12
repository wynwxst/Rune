# std::atomic

| Name | Signature | Does |
| --- | --- | --- |
| `Counter` | `class` | a number several threads may change |
| `Counter::load` / `store` | `(&self) -> i64` / `(&var self, i64)` | read, write |
| `Counter::add` / `sub` | `(&var self, delta: i64) -> i64` | and hand back the value *before* |
| `Counter::increment` / `decrement` / `next` | `(&var self) -> i64` | by one |
| `Counter::exchange` | `(&var self, next: i64) -> i64` | replace, handing back what was there |
| `Counter::compareExchange` | `(&var self, was: i64, want: i64) -> bool` | store only while the value is still `was` |
| `Counter::update` | `(&var self, f: @cfunction(i64) -> i64) -> i64` | apply `f`, retrying until it sticks |
| `Counter::raiseTo` | `(&var self, floor: i64) -> i64` | keep the larger |
| `Flag` | `class` | a `bool` several threads may set |
| `Flag::raise` | `(&var self) -> bool` | set it, and say whether this call did — exactly one caller ever gets `true` |
| `Flag::lower` / `isRaised` |  | clear it, read it |

*One number at a time, without a lock. For anything larger, `thread::Mutex`.*
