#include "mio.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "debug.h"
#include "executor.h"
#include "waker.h"

// Maximum number of events to handle per epoll_wait call.
#define MAX_EVENTS 64

struct Mio {
    // TODO: add required fields
};

// TODO: delete this once not needed.
#define UNIMPLEMENTED (exit(42))

Mio* mio_create(Executor* executor) { UNIMPLEMENTED; }

void mio_destroy(Mio* mio) { UNIMPLEMENTED; }

int mio_register(Mio* mio, int fd, uint32_t events, Waker waker)
{
    debug("Registering (in Mio = %p) fd = %d with", mio, fd);

    UNIMPLEMENTED;
}

int mio_unregister(Mio* mio, int fd)
{
    debug("Unregistering (from Mio = %p) fd = %d\n", mio, fd);

    UNIMPLEMENTED;
}

void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    UNIMPLEMENTED;
}
