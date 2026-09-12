# What cannot be overloaded

`&&`, `||` and `??` cannot be overloaded: they short-circuit, so they never evaluate their right side unconditionally, and a method call would have to.
