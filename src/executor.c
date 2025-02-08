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

    queue->size = max_queue_size;
    queue->front = 0;
    queue->back = 0;

    return queue;
}

int _futures_queue_push(FuturesQueue* queue, Future* future) {
    if ((queue->back + 1) > queue->size) {
        return -1;
    }

    queue->futures[queue->back] = future;
    queue->back++;

    return 0;
}

int _futures_queue_pop(FuturesQueue* queue, Future** future) {
    if (queue->front >= queue->back) {
        return -1;
    }

    *future = queue->futures[queue->front];
    queue->front++;

    return 0;
}

Future* _futures_queue_front(FuturesQueue* queue) {
    if (queue->front >= queue->back) {
        return NULL;
    }

    return queue->futures[queue->front];
}

size_t _futures_queue_size(FuturesQueue* queue) {
    return queue->back - queue->front;
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
    // musi trzymać zadania do wykonania
    // zakończonych zadań nie musi trzymać
    // jeżeli zadanie, czeka aż zostanie wywołany Waker, to też executor nie musi trzymać,
    // ale jeżeli Executor nie ma aktywnych zadań, to musi wywołać mio_poll()
    FuturesQueue* queue;
    size_t count_of_pending_tasks;
};

// TODO: delete this once not needed.
#define UNIMPLEMENTED (exit(42))

// Executor tworzy dla siebie instancję Mio
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
    executor_spawn(waker->executor, waker->future); // TODO: sprawdzić czy to jest ok
}

void executor_spawn(Executor* executor, Future* fut) { 
    if (_futures_queue_push(executor->queue, fut) == -1) {
        fprintf(stderr, "Failed to push future to the queue\n");
        return;
    }
    fut->is_active = true;
    executor->count_of_pending_tasks++;
 }

// Executor w pętli przetwarza zadania:
    // jeśli nie ma już niezakończonych (PENDING) zadań, kończy pętlę.

    // jeśli nie ma aktywnych zadań, woła mio_poll() żeby uśpić wątek egzekutora 
    // aż się to zmieni.
    
    // dla każdego aktywnego zadania future, woła future.progress(future, waker)
    // (tworząc przy tym Waker który doda zadanie z powrotem do kolejki, jeśli 
    // będzie potrzeba).

void executor_run(Executor* executor) { 
    while (executor->count_of_pending_tasks > 0) {
        if (_futures_queue_size(executor->queue) == 0) {
            mio_poll(executor->mio);
        }

        Future* future = _futures_queue_front(executor->queue);
        if (future == NULL) {
            continue;
        }

        Waker waker;
        waker.executor = executor;
        waker.future = future;

        FutureState future_state = future->progress(future, executor->mio, waker);
        
        if (future_state == FUTURE_COMPLETED || future_state == FUTURE_FAILURE) {
            future->is_active = false;
            // TODO: czy nie trzeba ustawiać future->ok
            future->errcode = future_state == FUTURE_COMPLETED ? 0 : 1; // TODO: czy tak trzeba ustawiać?
            executor->count_of_pending_tasks--;
        }
    }
 }

void executor_destroy(Executor* executor) { 
    mio_destroy(executor->mio);
    _futures_queue_destroy(executor->queue);
    free(executor);
 }
