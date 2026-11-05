*This project has been created as part of the 42 curriculum by zhamza.*

# Codexion

A C / POSIX-threads resource-arbitration simulation inspired by the dining
philosophers problem. It models coders sharing USB dongles in a circular hub,
where every compilation needs **both** adjacent dongles, every release incurs a
**cooldown**, and competing requests are ordered using **FIFO or EDF**.


## Description

There are `N` coders and `N` shared dongles. Each coder loops through:

1. Acquire their two neighboring dongles **atomically**.
2. Compile while both dongles are held.
3. Release both, initiating each dongle's cooldown.
4. Debug and refactor without holding dongles, then request them again.

Every coder has a deadline: `last_compile_start + time_to_burnout`. At the
start of the simulation, `last_compile_start` is the common simulation start.
A monitor thread stops the simulation at the earliest missed deadline. It also
stops once every coder has **completed** the required number of compilations.

## Instructions

Requires a C compiler with POSIX threads, `make`, and (for the optional test
suite) Python 3. No external C libraries or libft are required.

```sh
make
./codexion 5 800 200 200 100 3 50 edf
./codexion 5 800 200 200 100 3 50 fifo
make test
make clean
make fclean
make re
```

All eight positional arguments are mandatory:

| Position | Name | Meaning |
| --- | --- | --- |
| 1 | `number_of_coders` | Positive integer; also number of dongles |
| 2 | `time_to_burnout` | Milliseconds from start of last compilation to next start deadline |
| 3 | `time_to_compile` | Milliseconds holding both dongles |
| 4 | `time_to_debug` | Milliseconds debugging |
| 5 | `time_to_refactor` | Milliseconds refactoring |
| 6 | `number_of_compiles_required` | Stop after all have completed at least this many |
| 7 | `dongle_cooldown` | Minimum milliseconds from release to reacquisition |
| 8 | `scheduler` | Exactly `fifo` or `edf` |

Invalid numbers, negatives, overflow, wrong argument count, invalid scheduler,
and zero coders are rejected. Zero durations and zero required compilations are
accepted. With zero required compilations, the program exits immediately.

### Logs

The program emits only these event forms to stdout:

```text
0 1 has taken a dongle
0 1 has taken a dongle
0 1 is compiling
200 1 is debugging
400 1 is refactoring
```

A single coder has only one dongle; it takes that dongle once and cannot start
compiling. A separate monitor eventually prints `<timestamp> 1 burned out`.

## Architecture

| File | Responsibility |
| --- | --- |
| `src/main.c` | CLI and lifecycle |
| `src/parsing.c` | Strict nonnegative, overflow-safe argument parsing |
| `src/init.c`, `src/cleanup.c` | Structures, mutexes, queues, allocation and cleanup |
| `src/threads.c` | Worker/monitor startup barrier, joins and creation failures |
| `src/worker.c`, `src/worker_phases.c` | Compilation, debugging, refactoring |
| `src/acquire.c` | Request creation and waiting |
| `src/grant.c`, `src/grant_wait.c` | Pair arbitration and cooldown wake-ups |
| `src/dongle.c` | Per-dongle mutex discipline, pair locking and release |
| `src/heap.c`, `src/heap_pop.c` | Custom binary min-heap priority queues |
| `src/monitor.c` | Precise earliest-deadline burnout detection; completed coders excluded |
| `src/edf_schedule.c` | Coordinated EDF admission slots for conflict-free repeating cycles |
| `src/timing.c` | Monotonic elapsed time; condition-variable timed waits |
| `src/logging.c` | Serialized, direct `write(1, ...)` output to stdout |

### Dongle mapping

Coder `i` (zero-based) needs dongles `i` and `(i + 1) % N`, ensuring that
neighboring coders share exactly one dongle. For `N == 2`, both coders share
both dongles. For `N == 1`, there is only one dongle and no compilation.

### FIFO / EDF priority heaps and coordinated reservations

Every dongle has a custom binary min-heap of queued requests. A coder **queues
its request for both dongles immediately** after it reaches the acquisition
step; it never physically holds one dongle while waiting for the other.

- **FIFO**: the oldest `request_seq` is granted first when the necessary
  resources are free. Strict FIFO is implemented by the heap comparator.
- **EDF**: earlier absolute `request_deadline` comes first. A deterministic
  tie-break uses a phase-slot timestamp, then request sequence and coder ID.

A naive approach that simply required a coder to be heap head on both dongles
could squander capacity and cause burnout even for feasible EDF configurations.
To avoid this, EDF uses **coordinated admission windows** while keeping the
requests present in the heaps. The window does not acquire or reserve a dongle;
actual acquisition is still a two-dongle transaction that verifies ownership,
cooldown and priority under locks.

For `N >= 2`, let `k = floor(N / 2)`, `Q = compile + cooldown`, and
`S = compile + debug + refactor`. The coordinator builds a repeating cycle
with conservative millisecond rounding/guard time:

```text
cycle = max(S + 4, ceil((Q + 4) * N / k))
phase(coder i, 0-based) = floor(cycle * ((i * k) mod N) / N)
allowed_start(i, round) = simulation_start + phase(i) + round * cycle
```

The order places non-neighboring coders at compatible points in the cycle.
Adjacent coders' reserved starts remain separated by at least their resource
hold plus cooldown. For even rings this naturally becomes alternating parity
sets; for odd rings the phases rotate around the ring. An already-queued
request can acquire both dongles only after its admission window opens and
its two priority heaps allow it.

This avoids the demonstrated EDF starvation of the initial implementation in
feasible 5-coder / 800ms cooldown and 200-coder / 400ms cooldown workloads.
It is a **non-preemptive EDF policy with a deterministic admission schedule**,
not an arbitrary-job real-time scheduling theorem: high OS contention and
parameters with no feasible deadline slack still need careful testing.

## Blocking cases handled

### Deadlock and Coffman's conditions

The classic four Coffman conditions are mutual exclusion, hold-and-wait,
no preemption, and circular wait. Mutual exclusion is necessary for these
resources, but **hold-and-wait is prevented**: both dongles are reserved
atomically under the simulation mutex, or neither is reserved. There is no
partial dongle ownership while waiting. Per-dongle heap priorities are
strictly ordered, preventing queue-order cycles; mutex acquisitions are
ordered by dongle index.

### Starvation prevention

FIFO ranks requests in strict arrival order. EDF ranks requests by earliest
burnout deadline with a deterministic admission-phase tie-break. Two-dongle
atomic granting and EDF admission windows avoid fragmented grants that can
otherwise starve coders even when a coordinated solution is feasible.
After a coder has completed its required compilation count, it finishes the
current debug/refactor transition and stops requesting dongles. The monitor
no longer treats that coder as needing another compilation.

### Cooldown handling

Each dongle stores `ready_at`, assigned at release as
`monotonic_now + cooldown`. Free ownership is not sufficient to grant it;
the current monotonic time must also have reached `ready_at`.
A waiting coder sleeps on a condition variable with a timed wait until the
latest of its two candidate dongle cooldowns expires. Ordinary releases,
new requests, compilation starts, and simulation stop all notify waiters.

### Precise burnout detection

An independent monitor thread scans the coders' absolute deadlines while
holding the simulation mutex. It performs a `pthread_cond_timedwait` for the
nearest deadline, waking early when any compilation changes a deadline. When
a deadline is missed it sets the shared stop flag, logs the burnout event,
and broadcasts to all sleepers. The program uses `CLOCK_MONOTONIC` for elapsed
time and converts relative waits into `CLOCK_REALTIME` deadlines because
normal `pthread_cond_t` objects use the real-time condition clock by default.
No busy-waiting is needed. Logged timestamps have millisecond resolution;
OS scheduling may introduce small delays.

### Log serialization

A dedicated logging mutex guards each complete line. Messages are assembled
in a small stack buffer and written with `write(1, ...)`, avoiding stdio
buffering delays. Event logging is called while the simulation mutex is held,
so the stop state cannot change mid-message. No messages are printed after
an observed burnout.

## Thread synchronization mechanisms

**Locking order:** `sim.mutex` is always acquired before any dongle mutex;
for two distinct dongles, the lower index is locked first. A print mutex
may be taken while holding `sim.mutex`, never the other way round. This
fixed order avoids lock-inversion deadlocks.

- `sim.mutex`: protects the shared stop/start flags, coder compilation
  counters, deadlines, request sequence and arbitration transactions.
- `dongle[i].mutex`: protects that dongle's owner, ready-at timestamp, and
  request heap. All access to these fields happens with this mutex held.
- `print_mutex`: prevents messages being interleaved during output.
- `changed` condition: prevents CPU-intensive polling; it coordinates
  request waiters, the monitor, sleeps, the start barrier, and shutdown.

For example, two adjacent coders can simultaneously attempt to request the
same dongle. Their requests are serialized by `sim.mutex`, recorded in
per-dongle heaps, and granted only by a transaction holding the dongles'
mutexes, so the physical resource cannot be duplicated or stolen. The
monitor accesses `last_start` under the same simulation mutex, so it cannot
observe a torn update while a worker starts a compilation.

## Testing

Run `make test` for the 34 fast behavioral checks. Run `make test-scale`
for the longer benchmarks based on the correction sheet (including the
400/800ms cooldown scenarios and the 200-coder EDF stress case). The suite
checks input errors, single-coder burnout, zero durations, serialized logs,
two-dongle logging, adjacent-coder exclusion, cooldown inferred from logs,
state transitions, completion targets, and sampled timing bounds. The long
suite can take roughly a minute on ordinary hardware.

Use additional stress tests, the campus `norminette` checker, and memory
checkers on the target machine before submission. The tests are not a
formal proof of fairness, precise 10-ms burnout logging on every machine,
or conformance to the Norm.

## Resources

- Project subject: **Codexion v1.5**, Chapters IV–VIII.
- POSIX threads: `pthread_create(3)`, `pthread_mutex_lock(3)`,
  `pthread_cond_wait(3)`, `pthread_cond_timedwait(3)` manual pages.
- Time functions: `clock_gettime(2)` and `clock_gettime(3)` manual pages.
- Operating Systems: Three Easy Pieces, chapters on locks and condition
  variables (Remzi H. Arpaci-Dusseau and Andrea C. Arpaci-Dusseau).
- Cormen et al., *Introduction to Algorithms*, binary heaps and priority
  queues.
- Coffman et al. (1971), conditions for resource deadlocks.

### AI usage disclosure

AI assistance was used to review the subject, propose the concurrency
architecture, draft the C implementation, design a custom heap and pair
arbitration mechanism, assemble the Makefile, write documentation, and
generate initial automated tests. The generated code and explanations must
be independently reviewed, tested and understood by the submitting student,
as required by the subject. AI output is not evidence of correctness.

## Submission / defense notes

Commit source code, headers, `Makefile`, and `README.md` to your assigned
Git repository. Do not submit only a binary. Verify Norm, run on the campus
machines, and be ready to explain the locking order, both-dongle grant,
EDF tie-break, heap operations, cooldown wake-up, monitor clock, and each
termination path. Recode tasks may be requested during peer evaluation.

### FIFO -> LIFO defense practice

See `DEFENSE_LIFO.md`. The official program intentionally continues accepting
only `fifo` and `edf`; the LIFO comparator is a **temporary defense recode**,
not a third accepted command-line scheduler. Revert it before committing.
# codexion
# codexion
