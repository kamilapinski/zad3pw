#include "executor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "future.h"
#include "mio.h"
#include "waker.h"

typedef struct {
    Future** futures;
    size_t max_size;
    size_t size;
    size_t front;
    size_t back;
} FuturesQueue;

FuturesQueue* _futures_queue_create(size_t max_queue_size) {
    FuturesQueue* queue;
    queue = malloc(sizeof(FuturesQueue));
    if (queue == NULL) {
        return NULL;
    }

    queue->futures = malloc(max_queue_size * sizeof(Future*));
    if (queue->futures == NULL) {
        free(queue);
        return NULL;
    }

    queue->max_size = max_queue_size;
    queue->size = 0;
    queue->front = 0;
    queue->back = 0;

    return queue;
}

int _futures_queue_push(FuturesQueue* queue, Future* future) {
    if (queue->size == queue->max_size) {
        return -1;
    }

    queue->futures[queue->back] = future;
    queue->size++;

    if (queue->back == queue->max_size - 1) {
        queue->back = 0;
    } else {
        queue->back++;
    }

    return 0;
}

int _futures_queue_pop(FuturesQueue* queue, Future** future) {
    if (queue->size == 0) {
        return -1;
    }

    *future = queue->futures[queue->front];
    queue->size--;

    if (queue->front == queue->max_size - 1) {
        queue->front = 0;
    } else {
        queue->front++;
    }

    return 0;
}

Future* _futures_queue_front(FuturesQueue* queue) {
    if (queue->size == 0) {
        return NULL;
    }

    return queue->futures[queue->front];
}

size_t _futures_queue_size(FuturesQueue* queue) {
    return queue->size;
}

void _futures_queue_destroy(FuturesQueue* queue) {
    free(queue->futures);
    free(queue);
}

/**
 * @brief Structure to represent the current-thread executor.
 */
struct Executor {
    Mio* mio;
    FuturesQueue* queue;
    size_t count_of_pending_tasks;
};

Executor* executor_create(size_t max_queue_size) { 
    Executor* executor = malloc(sizeof(Executor));
    if (executor == NULL) {
        return NULL;
    }

    Mio* mio = mio_create(executor);
    if (mio == NULL) {
        free(executor);
        return NULL;
    }

    FuturesQueue* queue = _futures_queue_create(max_queue_size);
    if (queue == NULL) {
        mio_destroy(mio);
        free(executor);
        return NULL;
    }

    executor->mio = mio;
    executor->queue = queue;
    executor->count_of_pending_tasks = 0;

    return executor;
 }

void waker_wake(Waker* waker) {
    if (waker->future == NULL) {
        debug("Waker %p has no future\n", waker);
        return;
    }
    debug("Waking up Waker %p\n", waker);
    executor_spawn(waker->executor, waker->future);
}

void executor_spawn(Executor* executor, Future* fut) { 
    if (_futures_queue_push(executor->queue, fut) == -1) {
        debug("Failed to push future to the queue\n");
        return;
    }
    fut->is_active = true;
    executor->count_of_pending_tasks++;
}

void executor_run(Executor* executor) { 
    debug("Executor %p running\n", executor);
    while (_futures_queue_size(executor->queue) > 0) {
        debug("Executor %p processing tasks\n", executor);


        Future* future;
        
        _futures_queue_pop(executor->queue, &future);
        if (future == NULL) {
            debug("Executor %p failed to pop future from the queue\n", executor);
            continue;
        }

        Waker waker;
        waker.executor = executor;
        waker.future = future;

        FutureState future_state = future->progress(future, executor->mio, waker);
        
        
        if (future_state == FUTURE_COMPLETED || future_state == FUTURE_FAILURE) {
            debug("Executor %p task completed with future_state=%d\n", executor, future_state);
            future->is_active = false;
            executor->count_of_pending_tasks--;
        }
        else {
            debug("Executor %p task pending\n", executor);
        }

        if (_futures_queue_size(executor->queue) == 0 && executor->count_of_pending_tasks > 0) {
            mio_poll(executor->mio);
        }
    }
 }

void executor_destroy(Executor* executor) { 
    debug("Destroying Executor %p\n", executor);
    _futures_queue_destroy(executor->queue);
    mio_destroy(executor->mio);
    free(executor);
 }
