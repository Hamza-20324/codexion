*This project has been created as part of the 42 curriculum by zhamza.*

# Codexion

Codexion is a multithreaded simulation written in C using POSIX threads. It is inspired by the dining philosophers problem: coders compete for shared USB dongles, must obtain two adjacent dongles to compile, and can use each dongle again only after its cooldown expires. Requests are prioritized using FIFO or EDF scheduling.

## Description

The simulation contains `N` coders and `N` dongles arranged in a ring. Coder `i` (zero-based) needs dongles `i` and `(i + 1) % N`. Two neighboring coders therefore compete for one shared dongle.

A coder follows these phases:

1. Request the two adjacent dongles and wait until both can be acquired together.
2. Compile for `time_to_compile` milliseconds while holding both dongles.
3. Release both dongles and start their cooldown periods.
4. Debug for `time_to_debug` milliseconds.
5. Refactor for `time_to_refactor` milliseconds.
6. Repeat until the required compilation count has been reached or the simulation stops.

Each coder must start another compilation before `last_compile_start + time_to_burnout`, unless the coder has already completed the required number of compilations. The monitor detects missed deadlines. A successful simulation ends after every coder has completed the required number of compilations.

## Instructions

### Requirements

- A C compiler supporting C and POSIX threads
- `make`
- A Unix-like operating system with `pthread` support

No external C library or `libft` is required.

### Build

```sh
make
```

The Makefile compiles the sources with `-Wall -Wextra -Werror -pthread` and creates `./codexion`.

Available Makefile targets:

```sh
make          # Build
make clean    # Remove object files
make fclean   # Remove objects and executable
make re       # Rebuild everything
```

### Run

```sh
./codexion number_of_coders time_to_burnout time_to_compile time_to_debug time_to_refactor number_of_compiles_required dongle_cooldown scheduler
```

All eight arguments are mandatory and must appear in this order:

| Position | Argument | Meaning |
| --- | --- | --- |
| 1 | `number_of_coders` | Number of coders and shared dongles (positive integer) |
| 2 | `time_to_burnout` | Maximum time in ms between the starts of consecutive compilations |
| 3 | `time_to_compile` | Duration of compilation in ms |
| 4 | `time_to_debug` | Duration of debugging in ms |
| 5 | `time_to_refactor` | Duration of refactoring in ms |
| 6 | `number_of_compiles_required` | Number of compilations each coder must complete |
| 7 | `dongle_cooldown` | Minimum ms after release before a dongle can be used again |
| 8 | `scheduler` | `fifo` or `edf` |

For example:

```sh
./codexion 5 2000 200 200 200 10 0 fifo
./codexion 5 2000 200 200 200 7 0 edf
```

The parser rejects missing or extra arguments, invalid scheduler names, negative/non-numeric values, numeric overflow, and zero coders. Nonnegative durations and a zero compilation target are accepted by the implementation. A zero target exits immediately.

### Output

The program writes timestamped events to standard output:

```text
0 1 has taken a dongle
0 1 has taken a dongle
0 1 is compiling
200 1 is debugging
400 1 is refactoring
```

The exact timestamps depend on OS scheduling. When a coder misses a compilation deadline, the monitor prints a final `burned out` event and stops the simulation. With only one coder there is only one dongle; that coder cannot acquire two and eventually burns out.

## Architecture

| Source file | Responsibility |
| --- | --- |
| `src/main.c` | CLI and simulation lifecycle |
| `src/parsing.c` | Parsing and validating arguments |
| `src/init.c`, `src/cleanup.c` | Allocating and releasing resources |
| `src/threads.c` | Creating, starting and joining coder/monitor threads |
| `src/worker.c`, `src/worker_phases.c` | Compile, debug and refactor phases |
| `src/acquire.c` | Enqueuing dongle requests and waiting for grants |
| `src/grant.c`, `src/grant_wait.c` | Checking resource eligibility, arbitration and cooldown waits |
| `src/dongle.c` | Ordered dongle locking and release |
| `src/heap.c`, `src/heap_pop.c` | Per-dongle binary heaps and priority comparisons |
| `src/edf_schedule.c` | Computing EDF phase-slot values for tie-breaking |
| `src/timing.c` | Monotonic time and condition-variable waiting |
| `src/logging.c` | Serialized timestamped output |
| `src/monitor.c` | Deadline monitoring and burnout detection |
| `src/codexion.h` | Shared structures and function declarations |

### Queues, heaps and scheduling

Every dongle has a waiting queue implemented as a binary heap. When a coder requests a pair of dongles, its request is inserted into both relevant queues. The coder does not hold just one dongle while waiting for the other.

- **FIFO** compares request sequence numbers: older requests receive higher priority.
- **EDF** compares absolute burnout deadlines: earlier deadlines receive higher priority. The implementation uses an EDF phase slot, then the request sequence and coder ID, to break ties.

The grant logic also checks whether both dongles needed by a competing request are currently available and past cooldown. As implemented, a later **eligible** request may proceed when an earlier competing request cannot currently acquire its entire pair. This is not a single global strict-FIFO queue; the distinction matters when explaining the scheduler during evaluation.

The `edf_slot()` function calculates a deterministic schedule-related value used by the EDF comparator. In the current source it is **not** a separate admission gate that delays compilation until a reserved time window.

## Blocking cases handled

### Deadlock avoidance

A coder never waits while holding only one dongle. The two dongles are checked and granted together while the simulation state is synchronized. When two dongle mutexes must be locked, the smaller index is locked first. This avoids partial resource ownership and inconsistent lock ordering.

### Shared resource protection

Ownership and cooldown timestamps are checked before a grant. Shared simulation state and per-dongle data are protected by the corresponding mutexes. Two neighboring coders cannot legitimately compile with the same dongle at once.

### Waiting without busy loops

Waiting threads use condition variables. A timed wait is used when the next relevant event is a dongle's cooldown expiration. A broadcast wakes waiters after state changes or when the simulation stops.

### Cooldown

On release, each dongle receives a `ready_at` timestamp equal to the current monotonic time plus `dongle_cooldown`. A dongle may be unowned but still unavailable until that timestamp is reached.

### Burnout and completion

Each compilation start updates the coder's next deadline. The monitor checks active coders and logs the earliest missed deadline. Coders that already reached their required compilation count are excluded from further burnout checks. Completion and burnout both trigger an orderly stop and thread join before cleanup.

### Scheduling limitations

FIFO ordering does not by itself guarantee that every coder avoids starvation or completes within its deadline under every contention pattern. EDF prioritizes earlier deadlines but does not guarantee survival for infeasible workloads. Repeated stress testing is important, especially with odd coder counts and cooldown.

## Thread synchronization mechanisms

- `sim->mutex` protects shared simulation state: stop/start flags, request sequence values, coder progress and arbitration decisions.
- `dongles[i].mutex` protects each dongle's owner, cooldown timestamp and request heap.
- `sim->print_mutex` serializes complete log messages.
- `sim->changed` is a condition variable used by worker threads, the monitor, the startup barrier and shutdown notifications.
- `pthread_join()` ensures worker and monitor threads have exited before their shared resources are freed.

The normal lock order is: **simulation mutex first, then dongle mutexes in ascending index order**. Logging can take the print mutex while holding the simulation mutex; the reverse lock order must be avoided.

Elapsed times come from `CLOCK_MONOTONIC`. The condition variables use their default clock, so timed waits construct absolute deadlines using `CLOCK_REALTIME`.

## Testing

The 42 correction sheet includes the following benchmark commands:

```sh
# One coder: expected burnout at approximately 800 ms
./codexion 1 800 200 200 200 10 0 fifo

# Feasible FIFO and EDF workloads: all coders should complete
./codexion 5 2000 200 200 200 10 0 fifo
./codexion 5 2000 200 200 200 7 0 edf

# Expected burnout: final log line near 500 ms
./codexion 5 500 200 200 200 10 0 fifo

# Cooldown validation: should complete without burnout
./codexion 5 3000 200 200 200 10 400 fifo

# Compare FIFO versus EDF grant ordering under heavy contention
./codexion 5 3000 200 200 200 10 800 fifo
./codexion 5 3000 200 200 200 10 800 edf
```

In the `800` ms cooldown comparison, the correction sheet asks for differences in grant ordering; it does not explicitly require both policies to complete without burnout. Verify each grant's eligibility, FIFO/EDF priority, two-dongle ownership, cooldown, phase durations and serialized logs.

**Run benchmarks repeatedly.** A single successful run does not exclude timing-dependent failures. In a previous review of this source, the `400` ms FIFO case was reported to fail intermittently; recheck and address any such failure before submission.

### Memory and concurrency checks (optional tools)

```sh
# Memory errors and leaks
valgrind --leak-check=full --show-leak-kinds=all \
  ./codexion 5 10000 200 200 200 2 0 fifo

# Data races and synchronization errors
valgrind --tool=helgrind ./codexion 5 10000 200 200 200 2 0 fifo
valgrind --tool=drd ./codexion 5 10000 200 200 200 2 0 fifo

# Check 42 code style if Norminette is installed
norminette src
```

The Makefile provided with this source contains `all`, `clean`, `fclean` and `re`. It **does not** provide `make test` or `make test-scale` targets. Use the benchmark commands above unless you add a test runner.

## Resources

- Official 42 **Codexion v1.5** subject and correction sheet
- POSIX `pthread_create`, `pthread_join`, `pthread_mutex_*`, `pthread_cond_*` manual pages
- `clock_gettime(2)` and related POSIX time documentation
- *Operating Systems: Three Easy Pieces* — locks, concurrency and condition variables
- *Introduction to Algorithms* — heaps and priority queues
- Coffman et al. — deadlock conditions

### AI usage

AI assistance was used during the project for subject review, suggestions about concurrency and heap-based scheduling .

## Submission and defense

Commit the C sources, header, `Makefile` and `README.md` to the assigned repository. Before the evaluation, compile from a clean checkout, verify Norm, run the correction sheet benchmarks several times and investigate any reported race, deadlock or memory leak.

During the defense, be prepared to explain the shared-state lock order, why dongles are granted as a pair, heap comparator behavior, FIFO/EDF priorities, cooldown, condition-variable waiting, burnout monitoring and the cleanup path.

### FIFO to LIFO recode practice

The correction sheet may ask you to temporarily make the FIFO scheduler serve the newest waiting request first. In this implementation, the priority comparison is in `heap_before()` in `src/heap.c`. The comparator must be changed in the scheduling logic (not just in the logs), and the changed behavior should be checked under contention. Restore FIFO afterward; the official CLI accepts only `fifo` and `edf`.
