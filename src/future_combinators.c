#include "future_combinators.h"
#include <stdlib.h>

#include "future.h"
#include "waker.h"
#include "executor.h"

static FutureState then_progress(Future* base, Mio* mio, Waker waker)
{
    ThenFuture* self = (ThenFuture*)base;
    debug("ThenFuture %p progress. Arg=%p\n", self, self->base.arg);

    if (!self->fut1_completed) {
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
            return FUTURE_PENDING;
        }
    }

    if (self->base.errcode) {
        return FUTURE_FAILURE;
    }

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
        return FUTURE_PENDING;
    }
}

ThenFuture future_then(Future* fut1, Future* fut2)
{
    return (ThenFuture) {
        .base = future_create(then_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .fut1_completed = false,
    };
}

typedef struct {
    Future base;
    Future* fut;
    int nr;
    JoinFuture* father;
} JoinPackFuture;

static FutureState join_pack_progress(Future* base, Mio* mio, Waker waker)
{
    JoinPackFuture* self = (JoinPackFuture*)base;
    debug("JoinPackFuture %p progress\n", self);

    FutureState fut_state = self->fut->progress(self->fut, mio, waker);
    
    if (fut_state == FUTURE_COMPLETED || fut_state == FUTURE_FAILURE) {
        self->base.errcode = self->fut->errcode;
        self->base.ok = self->fut->ok;

        if (self->nr == 1)
            self->father->fut1_completed = true;
        else
            self->father->fut2_completed = true;

        if (self->father->fut1_completed && self->father->fut2_completed) {
            executor_spawn(waker.executor, (Future*)self->father);
        }
        
        return fut_state;
    }
    else {
        return FUTURE_PENDING;
    }
}

JoinPackFuture future_join_pack(Future* fut, int nr, JoinFuture* father)
{
    return (JoinPackFuture) {
        .base = future_create(join_pack_progress),
        .fut = fut,
        .nr = nr,
        .father = father,
    };
}

static FutureState join_progress(Future* base, Mio* mio, Waker waker)
{
    JoinFuture* self = (JoinFuture*)base;
    debug("JoinFuture %p progress\n", self);

    if (!self->fut1) {
        JoinPackFuture* fut1 = malloc(sizeof(JoinPackFuture));
        if (fut1 == NULL) {
            debug("JoinPackFuture malloc failed\n");
            return FUTURE_FAILURE;
        }

        *fut1 = future_join_pack(self->result.fut1.ok, 1, self);

        self->result.fut1.ok = NULL;

        self->fut1 = (Future*)fut1;
    }

    if (!self->fut2) {
        JoinPackFuture* fut2 = malloc(sizeof(JoinPackFuture));
        if (fut2 == NULL) {
            free(self->fut1);
            self->fut1 = NULL;
            debug("JoinPackFuture malloc failed\n");
            return FUTURE_FAILURE;
        }

        *fut2 = future_join_pack(self->result.fut2.ok, 2, self);

        self->result.fut2.ok = NULL;

        self->fut2 = (Future*)fut2;
    }

    if (self->fut1 && self->fut2) {
        if (self->fut1_completed && self->fut2_completed) {
            self->result.fut1.ok = self->fut1->ok;
            self->result.fut2.ok = self->fut2->ok;
            self->result.fut1.errcode = self->fut1->errcode;
            self->result.fut2.errcode = self->fut2->errcode;

            JoinPackFuture* pack1 = (JoinPackFuture*)self->fut1;
            JoinPackFuture* pack2 = (JoinPackFuture*)self->fut2;

            self->fut1 = pack1->fut;
            self->fut2 = pack2->fut;

            free(pack1);
            free(pack2);

            if (self->result.fut1.errcode || self->result.fut2.errcode) {
                return FUTURE_FAILURE;
            }
            else {
                return FUTURE_COMPLETED;
            }
        }

        if (!self->fut1->is_active)
            executor_spawn(waker.executor, self->fut1);
        
        if (!self->fut2->is_active)
            executor_spawn(waker.executor, self->fut2);

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
            .fut1 = { .errcode = 0, .ok = fut1 },
            .fut2 = { .errcode = 0, .ok = fut1 },
        },
    };
}

typedef struct {
    Future base;
    Future* fut;
    int nr;
    bool stop;
    SelectFuture* father;
} SelectPackFuture;

static FutureState select_pack_progress(Future* base, Mio* mio, Waker waker) {

    SelectPackFuture* self = (SelectPackFuture*)base;
    debug("SelectPackFuture %p progress\n", self);

    FutureState fut_state = self->fut->progress(self->fut, mio, waker);

    if (self->father->which_completed == SELECT_COMPLETED_FUT1 ||
    self->father->which_completed == SELECT_COMPLETED_FUT2 ||
    self->father->which_completed == SELECT_FAILED_BOTH ||
    self->stop) {
        if (fut_state == FUTURE_PENDING) {
            return FUTURE_PENDING;
        }
        executor_spawn(waker.executor, (Future*)self->father);
        return fut_state;
    }


    if (fut_state == FUTURE_COMPLETED) {
        self->base.ok = self->fut->ok;
        self->base.errcode = self->fut->errcode;

        if (self->nr == 1 && self->father->which_completed != SELECT_COMPLETED_FUT2) {
            self->father->which_completed = SELECT_COMPLETED_FUT1;
            executor_spawn(waker.executor, (Future*)self->father);
        }
        else if (self->father->which_completed != SELECT_COMPLETED_FUT1) {
            self->father->which_completed = SELECT_COMPLETED_FUT2;
            executor_spawn(waker.executor, (Future*)self->father);
        }
        return FUTURE_COMPLETED;
    }
    else if (fut_state == FUTURE_FAILURE) {
        self->base.ok = self->fut->ok;
        self->base.errcode = self->fut->errcode;

        if (self->nr == 1) {
            if (self->father->which_completed == SELECT_COMPLETED_NONE)
                self->father->which_completed = SELECT_FAILED_FUT1;
            else {
                self->father->which_completed = SELECT_FAILED_BOTH;
                executor_spawn(waker.executor, (Future*)self->father);
            }
        }
        else {
            if (self->father->which_completed == SELECT_COMPLETED_NONE)
                self->father->which_completed = SELECT_FAILED_FUT2;
            else {
                self->father->which_completed = SELECT_FAILED_BOTH;
                executor_spawn(waker.executor, (Future*)self->father);
            }
        }
        return FUTURE_FAILURE;
    }

    return FUTURE_PENDING;
}

SelectPackFuture future_select_pack(Future* fut, int nr, SelectFuture* father) {
    return (SelectPackFuture) {
        .base = future_create(select_pack_progress),
        .fut = fut,
        .nr = nr,
        .stop = false,
        .father = father,
    };
}

static FutureState select_progress(Future* base, Mio* mio, Waker waker)
{
    SelectFuture* self = (SelectFuture*)base;
    debug("SelectFuture %p progress\n", self);

    SelectPackFuture* pack1 = (SelectPackFuture*)self->fut1;
    SelectPackFuture* pack2 = (SelectPackFuture*)self->fut2;

    if (pack1->father == NULL) {
        pack1->father = self;
        executor_spawn(waker.executor, self->fut1);
    }

    if (pack2->father == NULL) {
        pack2->father = self;
        executor_spawn(waker.executor, self->fut2);
    }

    if (!self->fut1->is_active && !self->fut2->is_active) {
        self->fut1 = pack1->fut;
        self->fut2 = pack2->fut;

        free(pack1);
        free(pack2);
        
        if (self->which_completed == SELECT_FAILED_BOTH)
            return FUTURE_FAILURE;
        else
            return FUTURE_COMPLETED;
    }

    if (self->which_completed == SELECT_COMPLETED_FUT1) {
        self->base.ok = self->fut1->ok;
    }

    if (self->which_completed == SELECT_COMPLETED_FUT2) {
        self->base.ok = self->fut2->ok;
    }

    if (self->which_completed == SELECT_COMPLETED_FUT1 ||
    self->which_completed == SELECT_COMPLETED_FUT2) {
        pack1->stop = true;
        pack2->stop = true;
    }
    else if (self->which_completed == SELECT_FAILED_BOTH) {
        pack1->stop = true;
        pack2->stop = true;
    }

    return FUTURE_PENDING;
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

    *pack1 = future_select_pack(fut1, 1, NULL);
    *pack2 = future_select_pack(fut2, 2, NULL);

    return (SelectFuture) {
        .base = future_create(select_progress),
        .fut1 = (Future*)pack1,
        .fut2 = (Future*)pack2,
        .which_completed = SELECT_COMPLETED_NONE,
    };
}