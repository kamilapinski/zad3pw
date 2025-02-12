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

typedef struct {
    Future base;
    Future* fut;
    bool completed;
} JoinPackFuture;

static FutureState join_pack_progress(Future* base, Mio* mio, Waker waker)
{
    JoinPackFuture* self = (JoinPackFuture*)base;
    debug("JoinPackFuture %p progress\n", self);

    FutureState fut_state = self->fut->progress(self->fut, mio, waker);
    
    if (fut_state == FUTURE_COMPLETED || fut_state == FUTURE_FAILURE) {
        self->completed = true;
        return fut_state;
    }
    else {
        waker_wake(&waker);
        return FUTURE_PENDING;
    }
}

JoinPackFuture future_join_pack(Future* fut)
{
    return (JoinPackFuture) {
        .base = future_create(join_pack_progress),
        .fut = fut,
        .completed = false,
    };
}

static FutureState join_progress(Future* base, Mio* mio, Waker waker)
{
    JoinFuture* self = (JoinFuture*)base;
    debug("JoinFuture %p progress\n", self);

    JoinPackFuture* pack1 = (JoinPackFuture*)self->fut1;
    JoinPackFuture* pack2 = (JoinPackFuture*)self->fut2;

    if (!self->fut1_completed && pack1->completed) {
        self->fut1_completed = true;
    }
    if (!self->fut2_completed && pack2->completed) {
        self->fut2_completed = true;
    }

    if (self->fut1_completed && self->fut2_completed) {
        self->result.fut1.errcode = pack1->fut->errcode;
        self->result.fut2.errcode = pack2->fut->errcode;
        self->result.fut1.ok = pack1->fut->ok;
        self->result.fut2.ok = pack2->fut->ok;

        free(pack1);
        self->fut1 = NULL;
        free(pack2);
        self->fut2 = NULL;

        if (self->result.fut1.errcode || self->result.fut2.errcode) {
            return FUTURE_FAILURE;
        }
        return FUTURE_COMPLETED;
    }

    if (self->fut1 && !self->fut1->is_active)
        executor_spawn(waker.executor, self->fut1);
    if (self->fut2 && !self->fut1->is_active)
        executor_spawn(waker.executor, self->fut2);

    waker_wake(&waker);
    return FUTURE_PENDING;
}

JoinFuture future_join(Future* fut1, Future* fut2)
{
    JoinPackFuture* pack1 = malloc(sizeof(JoinPackFuture));
    if (pack1 == NULL) {
        debug("PackFuture malloc failed\n");
        return (JoinFuture){};
    }

    JoinPackFuture* pack2 = malloc(sizeof(JoinPackFuture));
    if (pack2 == NULL) {
        debug("PackFuture malloc failed\n");
        free(pack1);
        return (JoinFuture){};
    }

    *pack1 = future_join_pack(fut1);
    *pack2 = future_join_pack(fut2);

    return (JoinFuture) {
        .base = future_create(join_progress),
        .fut1 = (Future*)pack1,
        .fut2 = (Future*)pack2,
        .fut1_completed = FUTURE_PENDING,
        .fut2_completed = FUTURE_PENDING,
        .result = {
            .fut1 = { .errcode = 0, .ok = NULL },
            .fut2 = { .errcode = 0, .ok = NULL },
        },
    };
}

typedef struct {
    Future base;
    Future* fut;
    int nr;
    bool finished;
    bool stop;
    int* which_finished;
} SelectPackFuture;

static FutureState select_pack_progress(Future* base, Mio* mio, Waker waker) {
    UNIMPLEMENTED;

    SelectPackFuture* self = (SelectPackFuture*)base;
    debug("SelectPackFuture %p progress\n", self);

    if (self->stop) {
        if (self->which_finished == -1) {
            free(self);
        }
        else {
            self->which_finished = -1;
        }
        return FUTURE_COMPLETED;
    }

    FutureState fut_state = self->fut->progress(self->fut, mio, waker);

    if (fut_state != FUTURE_PENDING) {
        self->finished = true;
        if (self->which_finished == 0) {
            self->which_finished = self->nr;
        }
        return fut_state;
    }

    waker_wake(&waker);
    return FUTURE_PENDING;
}

SelectPackFuture future_select_pack(Future* fut, int nr, int* which_finished) {
    return (SelectPackFuture) {
        .base = feature_create(future_select_pack),
        .fut = fut,
        .nr = nr,
        .finished = false,
        .stop = false,
        .which_finished = which_finished,
    };
}

static FutureState select_progress(Future* base, Mio* mio, Waker waker)
{
    SelectFuture* self = (SelectFuture*)base;
    debug("SelectFuture %p progress\n", self);

    SelectPackFuture* pack1 = (SelectPackFuture*)self->fut1;
    SelectPackFuture* pack2 = (SelectPackFuture*)self->fut2;

    if (!pack1 || !pack2) {
        debug("Invalid progress");
        return FUTURE_FAILURE;
    }

    FutureState fut1_state = FUTURE_PENDING;
    FutureState fut2_state = FUTURE_PENDING;

    if (pack1->finished) {
        if (pack1->fut->errcode)
            fut1_state = FUTURE_FAILURE;
        else
            fut1_state = FUTURE_COMPLETED;
    }

    if (pack2->finished) {
        if (pack2->fut->errcode)
            fut2_state = FUTURE_FAILURE;
        else
            fut2_state = FUTURE_COMPLETED;
    }

    int which_finished = pack1->which_finished;
    if (fut1_state == FUTURE_COMPLETED && fut2_state == FUTURE_COMPLETED) {

        // tutaj which_finished jest równe 1 lub 2
        // o ile select_pack_progress dobrze działa

        if (which_finished == 1) {
            self->base.ok = pack1->fut->ok;
            self->which_completed = SELECT_COMPLETED_FUT1;
        }
        else {
            self->base.ok = pack2->fut->ok;
            self->which_completed = SELECT_COMPLETED_FUT2;
        }
    }
    else if (fut1_state == FUTURE_COMPLETED) {
        self->base.ok = pack1->fut->ok;
        self->which_completed = SELECT_COMPLETED_FUT1;
    }
    else if (fut2_state == FUTURE_COMPLETED) {
        self->base.ok = pack2->fut->ok;
        self->which_completed = SELECT_COMPLETED_FUT2;
    }
    else if (fut1_state == FUTURE_FAILURE && fut2_state == FUTURE_FAILURE) {
        self->which_completed = SELECT_FAILED_BOTH;
    }
    else {
        if (fut1_state == FUTURE_FAILURE) {
            self->which_completed = SELECT_FAILED_FUT1;
        }
        else if (fut2_state == FUTURE_FAILURE) {
            self->which_completed = SELECT_FAILED_FUT2;
        }
        else {
            self->which_completed = SELECT_COMPLETED_NONE;
        }

        // jeżeli jedno zfailowało, to już naturalnie nie trzeba tego stopować - samo się zastopuje

        waker_wake(&waker);
        return FUTURE_PENDING;
    }

    // tutaj oznacza to, że już musimy wszystko zastopować
    // TODO: deallokacja pack'ów musi nastąpić w ich progressie

    pack1->stop = true;
    pack2->stop = true;

    if (!self->fut1->is_active)
        executor_spawn(waker.executor, self->fut1);
    
    if (!self->fut2->is_active)
        executor_spawn(waker.executor, self->fut2);

    self->fut1 = NULL;
    self->fut2 = NULL;

    if (self->which_completed == SELECT_COMPLETED_FUT1 || self->which_completed == SELECT_COMPLETED_FUT2) {
        return FUTURE_COMPLETED;
    }
    else {
        return FUTURE_FAILURE;
    }
}

SelectFuture future_select(Future* fut1, Future* fut2)
{
    SelectPackFuture* pack1 = malloc(sizeof(SelectPackFuture));
    if (pack1 == NULL) {
        debug("PackFuture malloc failed\n");
        return (SelectFuture){};
    }

    SelectPackFuture* pack2 = malloc(sizeof(SelectPackFuture));
    if (pack2 == NULL) {
        debug("PackFuture malloc failed\n");
        free(pack1);
        return (SelectFuture){};
    }

    int* which_finished = malloc(sizeof(int));
    if (which_finished == NULL) {
        debug("which_completed malloc failed\n");
        free(pack1);
        free(pack2);
        return (SelectFuture){};
    }

    // 0 - nothing finished
    // 1 - pack1 finished
    // 2 - pack2 finished
    *which_finished = 0;
    *pack1 = future_select_pack(fut1, 1, which_finished);
    *pack2 = future_select_pack(fut2, 2, which_finished);

    // TODO: zdeallocowac

    return (SelectFuture) {
        .base = future_create(join_progress),
        .fut1 = (Future*)pack1,
        .fut2 = (Future*)pack2,
        .which_completed = SELECT_COMPLETED_NONE,
    };
}