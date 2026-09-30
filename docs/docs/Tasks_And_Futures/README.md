# Tasks and futures

`async fn` and `.await`: one thread doing several things at once. Where threads run *at the same time* and the compiler checks what may cross between them, tasks take turns on one thread — so they share whatever they like, and the question is only who runs next.

## Pages

- [A task is a stack](a_task_is_a_stack.md)
- [Where `.await` may be written](where_await_may_be_written.md)
- [Blocks and closures](blocks_and_closures.md)
- [What a task may take](what_a_task_may_take.md)
- [Sharing between tasks](sharing_between_tasks.md)
- [Sleeping, waiting, and letting others run](sleeping_waiting_and_letting_others_run.md)
- [Cancellation and timeouts](cancellation_and_timeouts.md)
- [Work on other threads](work_on_other_threads.md)
- [Sockets driven by tasks](sockets_driven_by_tasks.md)
- [What it costs](what_it_costs.md)
- [What is not here](what_is_not_here.md)
