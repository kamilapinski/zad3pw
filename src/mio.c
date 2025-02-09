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
};

// TODO: delete this once not needed.
#define UNIMPLEMENTED (exit(42))

Mio* mio_create(Executor* executor) { 
    if (executor == NULL) {
        fprintf(stderr, "Executor is NULL\n");
        return NULL;
    }

    Mio* mio = malloc(sizeof(Mio));
    if (mio == NULL) {
        return NULL;
    }

    mio->epoll_fd = epoll_create1(0);
    if (mio->epoll_fd == -1) {
        fprintf(stderr, "Failed to create epoll file descriptor\n");
        free(mio);
        return NULL;
    }

    for (size_t i = 0; i < MAX_FDS; i++) {
        mio->events[i] = NULL;
    }

    mio->executor = executor;

    return mio;
}

// Mio pozwala w funkcji mio register zarejestrować, że dany waker ma zostać 
// wywołany, gdy zajdzie odpowiednie zdarzenie; te zdarzenia to gotowość danego 
// deskryptora do odczytu  lub zapisu (w przypadku tego zadania domowego). 

// Mio ma za zadanie zapewnić, że zawoła wakera dopiero gdy na danym zasobie 
// (deskryptorze) można wykonać operację read/write w sposób nieblokujący.

int mio_register(Mio* mio, int fd, uint32_t events, Waker waker)
{
    debug("Registering (in Mio = %p) fd = %d with events = %d\n", mio, fd, events);

    int epoll_fd = mio->epoll_fd;

    struct epoll_event* event = malloc(sizeof(struct epoll_event));

    if (event == NULL) {
        fprintf(stderr, "Failed to allocate memory for epoll event\n");
        return -1;
    }

    event->events = events;
    event->data.fd = fd;
    event->data.ptr = waker.future;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event)) {
        free(event);
        fprintf(stderr, "Failed to add file descriptor to epoll\n");
        return -1;
    }

    mio->events[fd] = event;

    return 0;
}

int mio_unregister(Mio* mio, int fd)
{
    debug("Unregistering (from Mio = %p) fd = %d\n", mio, fd);

    if (mio->events[fd] == NULL) {
        fprintf(stderr, "File descriptor %d is not registered\n", fd);
        return -1;
    }
    free(mio->events[fd]);
    mio->events[fd] = NULL;

    int epoll_fd = mio->epoll_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
        fprintf(stderr, "Failed to remove file descriptor from epoll\n");
        return -1;
    }

    return 0;
}

// Mio pozwala w funkcji mio_poll: uśpić wołający wątek aż dojdzie do co najmniej 
// jednego z zarejestrowanych zdarzeń i wywołać dla nich odpowiedniego wakera.

void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    int epoll_fd = mio->epoll_fd;

    struct epoll_event events[MAX_EVENTS];
    int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < event_count; i++) {
        struct epoll_event* event = &events[i];
        int fd = event->data.fd;
        debug("Mio (%p) polling event %d on fd %d\n", mio, i, fd);

        Future* future = event->data.ptr;
        Waker waker = { .future = future, .executor = mio->executor };

        waker_wake(&waker);
    }
}

int _mio_unregister_descriptors(Mio* mio) {
    for (size_t i = 0; i < MAX_FDS; i++) {
        if (mio->events[i] != NULL) {
            if (mio_unregister(mio, i)) {
                fprintf(stderr, "Failed to unregister file descriptor %zu\n", i);
                return -1;
            }
        }
    }
    return 0;
}

void mio_destroy(Mio* mio) {
    if(_mio_unregister_descriptors(mio)) { // czy trzeba zamykać deskryptory?
        fprintf(stderr, "Failed to unregister descriptors\n");
    }

    if (close(mio->epoll_fd)) {
        fprintf(stderr, "Failed to close epoll file descriptor\n");
    }

    free(mio);
}