# Memory and reference counting

Class instances are reference counted. The compiler inserts every retain and release itself, at the points a careful C programmer would have written them, and a debug build tells you if any object outlived the program.

## Pages

- [What is counted, and what is not](what_is_counted_and_what_is_not.md)
- [Sharing an object](sharing_an_object.md)
- [Deinitialisers](deinitialisers.md)
- [Values have destructors too](values_have_destructors_too.md)
- [Counting that is not emitted](counting_that_is_not_emitted.md)
- [Cycles are refused, not collected](cycles_are_refused_not_collected.md)
- [`Unique`: one owner, so no ring](unique_one_owner_so_no_ring.md)
- [`weak`, in detail](weak_in_detail.md)
- [Leak reporting](leak_reporting.md)
- [Where the counting happens](where_the_counting_happens.md)
