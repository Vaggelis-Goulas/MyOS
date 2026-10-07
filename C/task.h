#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 16
#define STACK_SIZE 4096
#define MAX_SEMAPHORES 32
#define MAX_MUTEXES 16
#define MAX_CONDVARS 16

typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_FINISHED
} task_state_t;

typedef struct {
    uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t cs, ds, es, fs, gs, ss;
} task_context_t;

typedef struct {
    int count;
    int waiting[MAX_TASKS];
    int wait_head, wait_tail;
    int active;
} semaphore_t;

typedef struct {
    int locked;
    int owner;
    int count;        // for recursive locking
    int original_priority;
    int waiting[MAX_TASKS];
    int wait_head, wait_tail;
    int active;
} mutex_t;

typedef struct {
    int waiting[MAX_TASKS];
    int wait_head, wait_tail;
    int active;
} condvar_t;

typedef struct {
    task_context_t context;
    task_state_t state;
    uint8_t stack[STACK_SIZE];
    int tid;
    char name[32];
    int priority;
    int sleep_ticks;
    int blocked_on;   // semaphore/mutex/condvar ID
    int block_type;   // 0=none, 1=sem, 2=mutex, 3=condvar
} task_t;

void tasking_init();
int task_create(void (*entry)(), const char *name, int priority);
void task_yield();
void task_exit();
void task_sleep(int ticks);
void task_block(int type, int id);
void task_wake(int tid);
void schedule(task_context_t *ctx);

void preempt_disable();
void preempt_enable();

int sem_create(int initial);
void sem_wait(int sem_id);
void sem_signal(int sem_id);
void sem_destroy(int sem_id);

int mutex_create();
void mutex_lock(int mutex_id);
void mutex_unlock(int mutex_id);
void mutex_destroy(int mutex_id);

int condvar_create();
void condvar_wait(int cond_id, int mutex_id);
void condvar_signal(int cond_id);
void condvar_broadcast(int cond_id);
void condvar_destroy(int cond_id);

extern int task_count;
extern int current_task;

#endif