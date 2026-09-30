# Single ownership without a count

Reference counting is the default, but it is not the only choice. Built with `--memory zombie`, a program keeps no counts at all: every value has exactly one owner, is handed on by moving, and is destroyed the moment its owner's scope ends. A second, precise borrow checker — Zombie — proves that every borrow is finished before the value it points at is gone, so nothing dangles and nothing is freed twice.

## Pages

- [Turning it on](turning_it_on.md)
- [Values move; borrows look](values_move_borrows_look.md)
- [The checker is precise](the_checker_is_precise.md)
- [Reference-counted code, ported](reference_counted_code_ported.md)
- [Where a reference is borrowed from](where_a_reference_is_borrowed_from.md)
- [Views: which fields a method touches](views_which_fields_a_method_touches.md)
- [Internal references](internal_references.md)
- [The standard library runs on it](the_standard_library_runs_on_it.md)
- [When a borrow has to wait for run time](when_a_borrow_has_to_wait_for_run_time.md)
- [Handles instead of back-references](handles_instead_of_back_references.md)
- [Threads that borrow shared data](threads_that_borrow_shared_data.md)
- [What single ownership does without](what_single_ownership_does_without.md)
- [The guards](the_guards.md)
