# Examples

Keep examples small and assign each to the earliest milestone that can execute it. Each one should demonstrate a stated runtime goal, not just syntax; the comment at the top of the file says which goal and what output to expect.

Current examples, roughly in reading order:

- `arithmetic.nv` - checked integer arithmetic; root value 51.
- `pingpong.nv` - `self`, `spawn`, `!`, selective receive, `exit`; root value 42.
- `rpc.nv` - Ref-correlated request/reply that skips an earlier unmatched message; root value 42.
- `hello.nv` - host output with `print`.
- `sieve.nv` - processes as data: a prime sieve built from a chain of filter processes, each a tail-recursive receive loop.
- `ring.nv` - message passing: a token circulates a ring of ten processes for 2000 laps; root value 20000. Every node handles 2000 messages in one frame.
- `isolation.nv` - fault isolation: one of three workers faults (`divide_by_zero`); its siblings and the root are unaffected, and the root detects the missing reply with `after`. Also shows why monitors will be wanted: the fault is otherwise silent.
- `ioserver.nv` - I/O as messages: clients send Ref-correlated requests to a device process and receive completions, never calling `print` themselves. The client code would not change if the device lived on another scheduler or another node.

Still wanted (in the order the language will be able to express them):

- a receive-timeout example on its own;
- a registry/key-value server (wants lists or maps to be interesting);
- length-prefixed binary decoding (milestone 09);
- multicore process fan-out (milestone 10).

`sieve.nv`, `ring.nv`, `isolation.nv`, and `ioserver.nv` are run by `tests/run.rc` (the last two against goldens under `tests/process/cli/`), so changing their output means updating the check. Edge cases and malformed programs belong under `tests/` instead.
