# Threads and sharing

`std::thread` runs more than one thing at once. The rule the whole module rests on is that **threads share nothing they can change** — and the compiler, not the programmer, is what checks it.

## Pages

- [Two questions](two_questions.md)
- [Running something](running_something.md)
- [When it will not compile](when_it_will_not_compile.md)
- [Shared mutable state](shared_mutable_state.md)
- [Asking directly](asking_directly.md)
- [Sharing a value](sharing_a_value.md)
- [Channels](channels.md)
- [Counters and flags](counters_and_flags.md)
- [Waiting](waiting.md)
- [What reference counting costs](what_reference_counting_costs.md)
- [Where this works](where_this_works.md)
- [What is not here](what_is_not_here.md)
