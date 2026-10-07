#include "task.h"
#include "timer.h"
#include "vga.h"

task_t tasks[MAX_TASKS];
static semaphore_t semaphores[MAX_SEMAPHORES];
static mutex_t mutexes[MAX_MUTEXES];
static condvar_t condvars[MAX_CONDVARS];

int current_task = -1;
int task_count = 0;
static int next_sem_id = 0;
static int next_mutex_id = 0;
static int next_cond_id = 0;
int preempt_count = 0;

void preempt_disable() {
    preempt_count++;
}

void preempt_enable() {
    if (preempt_count > 0) preempt_count--;
}

void tasking_init() {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_FINISHED;
    }
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        semaphores[i].active = 0;
    }
    for (int i = 0; i < MAX_MUTEXES; i++) {
        mutexes[i].active = 0;
    }
    for (int i = 0; i < MAX_CONDVARS; i++) {
        condvars[i].active = 0;
    }
    current_task = -1;
    task_count = 0;
    next_sem_id = 0;
    next_mutex_id = 0;
    next_cond_id = 0;
}

int task_create(void (*entry)(), const char *name, int priority) {
    if (task_count >= MAX_TASKS) return -1;
    int tid = task_count++;
    task_t *t = &tasks[tid];
    t->context.eip = (uint32_t)entry;
    t->context.eflags = 0x202;
    t->context.esp = (uint32_t)&t->stack[STACK_SIZE - 16];
    t->context.ebp = t->context.esp;
    t->context.cs = 0x08;
    t->context.ds = 0x10;
    t->context.es = 0x10;
    t->context.fs = 0x10;
    t->context.gs = 0x10;
    t->context.ss = 0x10;
    t->state = TASK_READY;
    t->tid = tid;
    t->priority = priority;
    t->sleep_ticks = 0;
    t->blocked_on = -1;
    t->block_type = 0;
    for (int i = 0; name[i] && i < 31; i++) t->name[i] = name[i];
    t->name[31] = '\0';
    return tid;
}

void task_yield() {
    asm volatile("int $62");
}

void task_exit() {
    if (current_task >= 0) {
        tasks[current_task].state = TASK_FINISHED;
        for (int i = 0; i < MAX_SEMAPHORES; i++) {
            semaphore_t *s = &semaphores[i];
            if (s->active) {
                for (int j = 0; j < MAX_TASKS; j++) {
                    if (s->waiting[j] == current_task) {
                        s->waiting[j] = -1;
                    }
                }
            }
        }
        for (int i = 0; i < MAX_MUTEXES; i++) {
            mutex_t *m = &mutexes[i];
            if (m->active && m->owner == current_task) {
                m->locked = 0;
                m->owner = -1;
                m->count = 0;
            }
        }
        task_yield();
    }
}

void task_sleep(int ticks) {
    if (current_task >= 0 && ticks > 0) {
        tasks[current_task].state = TASK_SLEEPING;
        tasks[current_task].sleep_ticks = ticks;
        task_yield();
    }
}

static void wait_queue_add(int *queue, int *head, int *tail, int tid) {
    queue[*tail] = tid;
    *tail = (*tail + 1) % MAX_TASKS;
}

static int wait_queue_remove(int *queue, int *head, int *tail) {
    if (*head == *tail) return -1;
    int tid = queue[*head];
    queue[*head] = -1;
    *head = (*head + 1) % MAX_TASKS;
    return tid;
}

static void wait_queue_remove_tid(int *queue, int *head, int *tail, int tid) {
    int h = *head;
    while (h != *tail) {
        if (queue[h] == tid) {
            queue[h] = -1;
            break;
        }
        h = (h + 1) % MAX_TASKS;
    }
}

void task_block(int type, int id) {
    if (current_task < 0) return;
    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].blocked_on = id;
    tasks[current_task].block_type = type;
    task_yield();
}

void task_wake(int tid) {
    if (tid >= 0 && tid < MAX_TASKS && tasks[tid].state == TASK_BLOCKED) {
        tasks[tid].state = TASK_READY;
        tasks[tid].blocked_on = -1;
        tasks[tid].block_type = 0;
    }
}

void schedule(task_context_t *ctx) {
    if (current_task >= 0 && tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].context = *ctx;
        tasks[current_task].state = TASK_READY;
    }

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].sleep_ticks > 0) {
                tasks[i].sleep_ticks--;
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = TASK_READY;
                }
            }
        }
    }

    int best = -1;
    int best_priority = -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_READY) {
            if (tasks[i].priority > best_priority) {
                best_priority = tasks[i].priority;
                best = i;
            }
        }
    }

    if (best >= 0) {
        current_task = best;
        tasks[current_task].state = TASK_RUNNING;
        *ctx = tasks[current_task].context;
        return;
    }

    if (current_task >= 0 && tasks[current_task].state != TASK_FINISHED) {
        tasks[current_task].state = TASK_RUNNING;
        *ctx = tasks[current_task].context;
        return;
    }

    asm volatile("cli; hlt");
}

int sem_create(int initial) {
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if (!semaphores[i].active) {
            semaphores[i].count = initial;
            semaphores[i].wait_head = 0;
            semaphores[i].wait_tail = 0;
            for (int j = 0; j < MAX_TASKS; j++) semaphores[i].waiting[j] = -1;
            semaphores[i].active = 1;
            return i;
        }
    }
    return -1;
}

void sem_wait(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEMAPHORES || !semaphores[sem_id].active) return;
    semaphore_t *s = &semaphores[sem_id];
    if (s->count > 0) {
        s->count--;
    } else {
        wait_queue_add(s->waiting, &s->wait_head, &s->wait_tail, current_task);
        task_block(1, sem_id);
    }
}

void sem_signal(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEMAPHORES || !semaphores[sem_id].active) return;
    semaphore_t *s = &semaphores[sem_id];
    int tid = wait_queue_remove(s->waiting, &s->wait_head, &s->wait_tail);
    if (tid >= 0) {
        task_wake(tid);
    } else {
        s->count++;
    }
}

void sem_destroy(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEMAPHORES || !semaphores[sem_id].active) return;
    semaphore_t *s = &semaphores[sem_id];
    int tid;
    while ((tid = wait_queue_remove(s->waiting, &s->wait_head, &s->wait_tail)) >= 0) {
        task_wake(tid);
    }
    s->active = 0;
}

int mutex_create() {
    for (int i = 0; i < MAX_MUTEXES; i++) {
        if (!mutexes[i].active) {
            mutexes[i].locked = 0;
            mutexes[i].owner = -1;
            mutexes[i].count = 0;
            mutexes[i].original_priority = 0;
            mutexes[i].wait_head = 0;
            mutexes[i].wait_tail = 0;
            for (int j = 0; j < MAX_TASKS; j++) mutexes[i].waiting[j] = -1;
            mutexes[i].active = 1;
            return i;
        }
    }
    return -1;
}

void mutex_lock(int mutex_id) {
    if (mutex_id < 0 || mutex_id >= MAX_MUTEXES || !mutexes[mutex_id].active) return;
    mutex_t *m = &mutexes[mutex_id];
    if (!m->locked) {
        m->locked = 1;
        m->owner = current_task;
        m->count = 1;
        m->original_priority = tasks[current_task].priority;
    } else if (m->owner == current_task) {
        m->count++;
    } else {
        wait_queue_add(m->waiting, &m->wait_head, &m->wait_tail, current_task);
        if (tasks[current_task].priority > m->original_priority) {
            tasks[m->owner].priority = tasks[current_task].priority;
        }
        task_block(2, mutex_id);
    }
}

void mutex_unlock(int mutex_id) {
    if (mutex_id < 0 || mutex_id >= MAX_MUTEXES || !mutexes[mutex_id].active) return;
    mutex_t *m = &mutexes[mutex_id];
    if (m->owner != current_task) return;
    m->count--;
    if (m->count == 0) {
        m->locked = 0;
        int tid = wait_queue_remove(m->waiting, &m->wait_head, &m->wait_tail);
        if (tid >= 0) {
            m->owner = tid;
            m->count = 1;
            m->original_priority = tasks[tid].priority;
            tasks[m->owner].priority = m->original_priority;
            task_wake(tid);
        } else {
            m->owner = -1;
            tasks[current_task].priority = m->original_priority;
        }
    }
}

void mutex_destroy(int mutex_id) {
    if (mutex_id < 0 || mutex_id >= MAX_MUTEXES || !mutexes[mutex_id].active) return;
    mutex_t *m = &mutexes[mutex_id];
    int tid;
    while ((tid = wait_queue_remove(m->waiting, &m->wait_head, &m->wait_tail)) >= 0) {
        task_wake(tid);
    }
    m->active = 0;
}

int condvar_create() {
    for (int i = 0; i < MAX_CONDVARS; i++) {
        if (!condvars[i].active) {
            condvars[i].wait_head = 0;
            condvars[i].wait_tail = 0;
            for (int j = 0; j < MAX_TASKS; j++) condvars[i].waiting[j] = -1;
            condvars[i].active = 1;
            return i;
        }
    }
    return -1;
}

void condvar_wait(int cond_id, int mutex_id) {
    if (cond_id < 0 || cond_id >= MAX_CONDVARS || !condvars[cond_id].active) return;
    if (mutex_id < 0 || mutex_id >= MAX_MUTEXES || !mutexes[mutex_id].active) return;
    
    condvar_t *cv = &condvars[cond_id];
    mutex_t *m = &mutexes[mutex_id];
    
    if (m->owner != current_task) return;
    
    m->count = 0;
    m->locked = 0;
    int tid = wait_queue_remove(m->waiting, &m->wait_head, &m->wait_tail);
    if (tid >= 0) {
        m->owner = tid;
        m->count = 1;
        m->original_priority = tasks[tid].priority;
        task_wake(tid);
    } else {
        m->owner = -1;
    }
    
    wait_queue_add(cv->waiting, &cv->wait_head, &cv->wait_tail, current_task);
    task_block(3, cond_id);
    
    mutex_lock(mutex_id);
}

void condvar_signal(int cond_id) {
    if (cond_id < 0 || cond_id >= MAX_CONDVARS || !condvars[cond_id].active) return;
    condvar_t *cv = &condvars[cond_id];
    int tid = wait_queue_remove(cv->waiting, &cv->wait_head, &cv->wait_tail);
    if (tid >= 0) {
        task_wake(tid);
    }
}

void condvar_broadcast(int cond_id) {
    if (cond_id < 0 || cond_id >= MAX_CONDVARS || !condvars[cond_id].active) return;
    condvar_t *cv = &condvars[cond_id];
    int tid;
    while ((tid = wait_queue_remove(cv->waiting, &cv->wait_head, &cv->wait_tail)) >= 0) {
        task_wake(tid);
    }
}

void condvar_destroy(int cond_id) {
    if (cond_id < 0 || cond_id >= MAX_CONDVARS || !condvars[cond_id].active) return;
    condvar_t *cv = &condvars[cond_id];
    int tid;
    while ((tid = wait_queue_remove(cv->waiting, &cv->wait_head, &cv->wait_tail)) >= 0) {
        task_wake(tid);
    }
    cv->active = 0;
}