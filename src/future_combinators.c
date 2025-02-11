#include "future_combinators.h"
#include <stdlib.h>

#include "future.h"
#include "waker.h"

// TODO: delete this once not needed.
#define UNIMPLEMENTED (exit(42))

static FutureState then_progress(Future* base, Mio* mio, Waker waker)
{
    ThenFuture* self = (ThenFuture*)base;
    debug("ThenFuture %p progress. Arg=%p\n", self, self->base.arg);

    if (!self->fut1_completed) {
        // podaję mojego waker'a, żebym ja został obudzony
        // i zostanę obudzony zarówno jeżeli fu1 wywoła waker_wake
        // jak i mio_register bo tam też podaję mojego waker'a
        debug("ThenFuture %p fut1_progress\n", self);

        FutureState fut1_state = self->fut1->progress(self->fut1, mio, waker);

        debug("ThenFuture %p fut1_state=%d\n", self, fut1_state);

        if (fut1_state == FUTURE_COMPLETED) {
            self->fut1_completed = true;
        }
        else if (fut1_state == FUTURE_FAILURE) {
            self->base.errcode = self->fut1->errcode;
            self->fut1_completed = true;
            return FUTURE_FAILURE;
        }
        else {
            // chyba nie możemy zrobić mio_register, bo nie wiadomo na co czekać
            // TODO: chyba jednak trzeba zrobić opakowanie na fut, który będzie
            // wywoływać mio_register
            waker_wake(&waker);
            return FUTURE_PENDING;
        }
    }

    if (self->base.errcode) {
        return FUTURE_FAILURE;
    }

    // fut1 completed
    self->fut2->arg = self->fut1->ok;

    debug("ThenFuture %p fut2_progress\n", self);
    FutureState fut2_state = self->fut2->progress(self->fut2, mio, waker);

    if (fut2_state == FUTURE_COMPLETED) {
        self->base.ok = self->fut2->ok;
        return FUTURE_COMPLETED;
    }
    else if (fut2_state == FUTURE_FAILURE) {
        self->base.errcode = self->fut2->errcode;
        return FUTURE_FAILURE;
    }
    else {
        // chyba nie możemy zrobić mio_register, bo nie wiadomo na co czekać
        waker_wake(&waker);
        return FUTURE_PENDING;
    }
}

ThenFuture future_then(Future* fut1, Future* fut2)
{
    return (ThenFuture) {
        // TODO: correct initialization.
        .base = future_create(then_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .fut1_completed = false,
    };
}

JoinFuture future_join(Future* fut1, Future* fut2)
{
    UNIMPLEMENTED;
    return (JoinFuture) {
        // TODO: correct initialization.
        // .base = ... ,
        // .fut1 = ... ,
        // ...
    };
}

SelectFuture future_select(Future* fut1, Future* fut2)
{
    UNIMPLEMENTED;
    return (SelectFuture) {
        // TODO: correct initialization.
        // .base = ... ,
        // .fut1 = ... ,
        // ...
    };
}
