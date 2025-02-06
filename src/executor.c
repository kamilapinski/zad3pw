#include "executor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "future.h"
#include "mio.h"
#include "waker.h"

/**
 * @brief Structure to represent the current-thread executor.
 */
struct Executor {
    // TODO: add your needed fields
};

// TODO: delete this once not needed.
#define UNIMPLEMENTED (exit(42))

Executor* executor_create(size_t max_queue_size) { UNIMPLEMENTED; }

void waker_wake(Waker* waker) { UNIMPLEMENTED; }

void executor_spawn(Executor* executor, Future* fut) { UNIMPLEMENTED; }

void executor_run(Executor* executor) { UNIMPLEMENTED; }

void executor_destroy(Executor* executor) { UNIMPLEMENTED; }
