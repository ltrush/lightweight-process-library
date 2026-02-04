#ifndef RR_H
#define RR_H

#include "lwp.h"

typedef struct Node {
    thread my_thread;
    struct Node *next;
    struct Node *prev;
} Node;

void rr_admit(thread new);     /* add a thread to the pool      */
void rr_remove(thread victim); /* remove a thread from the pool */
thread rr_next(void);            /* select a thread to schedule   */
int rr_qlen(void);            /* number of ready threads       */

#endif