#include "future_combinators.h"
#include <stdlib.h>

#include "future.h"
#include "waker.h"
#include "executor.h"

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

static FutureState join_progress(Future* base, Mio* mio, Waker waker)
{
    JoinFuture* self = (JoinFuture*)base;
    debug("JoinFuture %p progress\n", self);

    // TODO: chyba nie musi być ustawiony ok
    if (self->fut1->ok || self->fut1->errcode) {
        self->fut1_completed = !self->fut1->errcode ? FUTURE_COMPLETED : FUTURE_FAILURE;

        self->result.fut1.ok = self->fut1->ok;
        self->result.fut1.errcode = self->fut1->errcode;
    }
    else {
        if (!self->fut1->is_active)
            executor_spawn(waker.executor, self->fut1);
    }
    
    if (self->fut2->ok || self->fut2->errcode) {
        self->fut2_completed = !self->fut2->errcode ? FUTURE_COMPLETED : FUTURE_FAILURE;

        self->result.fut2.ok = self->fut2->ok;
        self->result.fut2.errcode = self->fut2->errcode;
    }
    else {
        if (!self->fut2->is_active)
            executor_spawn(waker.executor, self->fut2);
    }

    if (self->fut1_completed && self->fut2_completed) {
        if (self->result.fut1.errcode || self->result.fut2.errcode) {
            return FUTURE_FAILURE;
        }
        return FUTURE_COMPLETED;
    }

    return FUTURE_PENDING;
}

JoinFuture future_join(Future* fut1, Future* fut2)
{
    return (JoinFuture) {
        .base = future_create(join_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .fut1_completed = FUTURE_PENDING,
        .fut2_completed = FUTURE_PENDING,
        .result = {
            .fut1 = { .errcode = 0, .ok = NULL },
            .fut2 = { .errcode = 0, .ok = NULL },
        },
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
