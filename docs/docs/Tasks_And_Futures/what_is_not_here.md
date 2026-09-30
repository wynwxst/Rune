# What is not here

| Missing | Instead |
| --- | --- |
| a task moving between threads | a task stays on the thread that made it; `offloadAsync` starts one on a worker, and results cross back |
| pre-emptive cancellation | cooperative: a `checkpoint()` in a loop that never suspends |
| asynchronous file I/O | `offload` around the call; a file is never "not ready" |
| `select` over channels | `first` over futures; a `pending` future a channel's reader completes |
