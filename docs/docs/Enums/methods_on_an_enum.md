# Methods on an enum

**A state machine in an enum**

```rune
import std::io

enum Direction {
    North, East, South, West,

    pub fn turnRight(&self) -> Direction {
        match self {
            Direction::North => Direction::East,
            Direction::East => Direction::South,
            Direction::South => Direction::West,
            Direction::West => Direction::North,
        }
    }

    pub fn name(&self) -> String {
        match self {
            Direction::North => "north",
            Direction::East => "east",
            Direction::South => "south",
            Direction::West => "west",
        }
    }
}

fn main() -> i64 {
    var d = Direction::North
    var trace = ""
    for i in 0..5 {
        trace += d.name() + " "
        d = d.turnRight()
    }
    io::println(trace)
    0
}
```
