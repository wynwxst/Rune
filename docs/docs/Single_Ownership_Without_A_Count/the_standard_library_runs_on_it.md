# The standard library runs on it

The whole standard library compiles and runs under single ownership — `Vector`, `Map` and `Set`, `Option` and `Result`, `String` and the text routines, files and streams, JSON, the command-line parser. A container hands back a copy of what it is asked for rather than a shared reference (`v.at(0)` clones the element), and moves values in and out of its own storage with `std::mem`. How you call it does not change between the two modes: the same program builds either way, which is the whole point — anything that compiles under `zombie` compiles under `arc` too.
