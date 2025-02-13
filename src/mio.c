#include "mio.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "debug.h"
#include "executor.h"
#include "waker.h"

#define MAX_FDS 1024

// Maximum number of events to handle per epoll_wait call.
#define MAX_EVENTS 64

struct Mio {
    int epoll_fd;
    Executor* executor;
    struct epoll_event* events[MAX_FDS];
    size_t count_of_registered_fds;
};


Mio* mio_create(Executor* executor) { 
    if (executor == NULL) {
        debug("Executor is NULL\n");
        return NULL;
    }

    Mio* mio = malloc(sizeof(Mio));
    if (mio == NULL) {
        return NULL;
    }

    mio->epoll_fd = epoll_create1(0);
    if (mio->epoll_fd == -1) {
        debug("Failed to create epoll file descriptor\n");
        free(mio);
        return NULL;
    }

    for (size_t i = 0; i < MAX_FDS; i++)
        mio->events[i] = NULL;

    mio->executor = executor;
    mio->count_of_registered_fds = 0;

    return mio;
}

int mio_register(Mio* mio, int fd, uint32_t events, Waker waker)
{
    debug("Registering (in Mio = %p) fd = %d with events = %d\n", mio, fd, events);

    int epoll_fd = mio->epoll_fd;

    struct epoll_event* event = malloc(sizeof(struct epoll_event));

    if (event == NULL) {
        debug("Failed to allocate memory for epoll event\n");
        return -1;
    }

    event->events = events;
    event->data.fd = fd;
    event->data.ptr = waker.future;

    if (mio->events[fd] != NULL) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, event)) {
            debug("Failed to modify file descriptor in epoll\n");
            free(event);
            return -1;
        }
        free(mio->events[fd]);
        mio->events[fd] = event;
    }
    else {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event)) {
            debug("Failed to add file descriptor to epoll\n");
            free(event);
            return -1;
        }
        mio->events[fd] = event;
        mio->count_of_registered_fds++;
    }

    return 0;
}

int mio_unregister(Mio* mio, int fd)
{
    debug("Unregistering (from Mio = %p) fd = %d\n", mio, fd);

    int epoll_fd = mio->epoll_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
        debug("Failed to remove file descriptor from epoll\n");
        return -1;
    }

    if (mio->events[fd] != NULL) {
        free(mio->events[fd]);
        mio->events[fd] = NULL;
    }

    mio->count_of_registered_fds--;

    return 0;
}

void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    if (mio->count_of_registered_fds == 0) {
        debug("No file descriptors registered\n");
        return;
    }

    int epoll_fd = mio->epoll_fd;

    struct epoll_event events[MAX_EVENTS];
    int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < event_count; i++) {
        struct epoll_event* event = &events[i];

        Future* future = event->data.ptr;
        Waker waker = { .future = future, .executor = mio->executor };

        waker_wake(&waker);
    }
}

int _mio_unregister_descriptors(Mio* mio) {
    for (size_t i = 0; i < MAX_FDS; i++) {
        if (mio->events[i] != NULL) {
            mio_unregister(mio, i);
        }
    }
    return 0;
}

void mio_destroy(Mio* mio) {
    if(_mio_unregister_descriptors(mio)) {
        debug("Failed to unregister descriptors\n");
    }

    if (close(mio->epoll_fd)) {
        debug("Failed to close epoll file descriptor\n");
    }

    free(mio);
}