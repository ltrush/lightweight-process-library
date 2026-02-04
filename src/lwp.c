#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include "lwp.h"
#include "rr.h"

#define DEFAULT_STACK_SIZE 8388608 // 8 MB
#define next_terminated	lib_one // terminated list
#define next_waiting lib_two // waiting list 
#define STACK_ERROR 1

typedef struct MainList {
    thread my_thread;
    struct MainList *next; 
} MainList; 

// global pointers to mainlist, head and tail
MainList * ml_head = NULL;
MainList * ml_tail = NULL;

// globals to hold page size to find stack size
long mem_page_size;
rlim_t stack_size;
int stack_size_initialized = 0;

//initializing the scheduler for round robin
struct scheduler rr_publish = {NULL, NULL, rr_admit, rr_remove, 
                                                rr_next, rr_qlen};

//active_scheduler holds current scheulder, init to round robin
scheduler active_scheduler = &rr_publish;

thread active_thread = NULL; 

//curr_tid is our value that is given to all tids, incremented when needed 
tid_t curr_tid = 0;


//pointers for head and tail of terminated and waiting fifo
thread terminated_head = NULL;
thread terminated_tail = NULL;
thread waiting_head = NULL;
thread waiting_tail = NULL;


static int init_stack_size(void);
static void dealloc_stack(void *addr, size_t size);
static void lwp_wrap(lwpfun fun, void *arg);
static tid_t dealloc_thread(thread terminated, int *status);
static unsigned long *push_word(unsigned long *sp, unsigned long val);
void push_waiting(thread t);
thread pop_waiting(void);
thread pop_terminated(void);
void push_terminated(thread t);
void add_to_main_list(thread my_thread);
void remove_from_main_list(thread my_thread);

/*
	Create a new thread, admit to scheduler
	thread contains a stack and context
*/
tid_t lwp_create(lwpfun func, void * param) {
	// if we haven't initialized a stack, call this function
	// this function is used to find what size our stacks should be
    if (!stack_size_initialized) {
        if (init_stack_size() == STACK_ERROR) {
            fprintf(stderr, "Couldn't get stack size\n");
            return NO_THREAD;
        }
    }
    // allocate stack using mmap
	// don't care where in memeory, stack size big
    unsigned long *base = (unsigned long *)mmap(NULL, 
			stack_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	// if mmap failed return NO_THREAD
    if ((void *)base == MAP_FAILED) {
        perror("mmap failed");
        return NO_THREAD;
    }

    // allocate context for thread 
    thread new_thread = (thread)malloc(sizeof(context));
    if (new_thread == NULL) {
        fprintf(stderr, "Couldn't allocate context");
        dealloc_stack(base, stack_size);
        return NO_THREAD;
    }

    // stack_size is in bytes and a word is sizeof(unsigned long) bytes big
    // so stack_size / sizeof(unsigned long) is # of words in stack
    unsigned long *beginning_of_stack = base + 
                        (stack_size / sizeof(unsigned long));
    // moves the stack pointer down 8 bytes as expected when a RA is added to
	// top of the stack during a call, expected to not be 16 byte aligned
    beginning_of_stack--;
	//add the wrapper function to top of the stack for our "Return address"
    unsigned long *sp = push_word(beginning_of_stack, (unsigned long)lwp_wrap);

	//doesn't matter what base pointer is pushed, because never used
	// but somethign needs to be there 
    sp = push_word(sp,(unsigned long)beginning_of_stack);
    new_thread->state.rbp = (unsigned long)sp;
    new_thread->state.rsp = (unsigned long)sp;

	//init state to FPU, add function and param pointers to rdi and rsi
	// only ever going to have 1 param
    new_thread->state.fxsave = FPU_INIT;
    new_thread->state.rdi = (unsigned long)func;
    new_thread->state.rsi = (unsigned long)param;

	// update curr_tid, set tid, stack, stack_size
	// set status
    curr_tid++;
    new_thread->tid = curr_tid;
    new_thread->stack = base;
    new_thread->stacksize = stack_size;
    new_thread->status = MKTERMSTAT(LWP_LIVE, 0);

	//never used in library set to NULL here
    new_thread->sched_one = NULL; 
    new_thread->sched_two = NULL; 

	//add new_thread to main list of all threads
	add_to_main_list(new_thread);
	//admit to scheduler 
    active_scheduler->admit(new_thread);
	return new_thread->tid;
}

/*
	Finds page size and stack size
	makes stack size a multiple of page size
	only called once
*/
int init_stack_size(void) {
	// use sysconf to look up _SC_PAGESIZE
	// stack needs to be a multiple of this
    mem_page_size = sysconf(_SC_PAGESIZE);
    if (mem_page_size == -1) {
        perror("sysconf(_SC_PAGESIZE) failed");
        return STACK_ERROR;
    }

	// create a rlimit struct initalize all fields to zero
	// gets filled anyways
    struct rlimit my_rlimit = {0};
    int remainder = 0;
	//calls getrlimit to find the size of stack, we use the soft limit
    if (getrlimit(RLIMIT_STACK, &my_rlimit) == -1 || 
                                    my_rlimit.rlim_cur == RLIM_INFINITY) {
		// if DNE or is infinity set stack_size to default 8MB
        stack_size = DEFAULT_STACK_SIZE;
    } else {
		//set stack size to soft limit 
        stack_size = my_rlimit.rlim_cur;
        if (stack_size == 0) {
            fprintf(stderr, "Couldn't allocate stack because \
                                                stack soft limit of 0\n");
            return STACK_ERROR;
        }

		// fixes stack size to be a multiple of mem_page_size
        remainder = stack_size % mem_page_size;
        if (remainder != 0) {
			// if not multiple, then add the difference to get there
            stack_size += mem_page_size - remainder;
        }
    }

	//flag used to avoid calling this function everytime
	stack_size_initialized = 1;
    return 0;
}

/*
Starts the threading system, converts the calling
thread into an lwp and yields to next process
based off next in scheduler
*/
void lwp_start(void) {

	if (active_thread != NULL){
		fprintf(stderr, "Called lwp_start while threads are active \n");
		return;
	}
	//allocate context for the main (calling thread)
	thread main_thread = malloc(sizeof(context));
	 if (main_thread == NULL) {
        fprintf(stderr, "Couldn't allocate main thread context");
        return;
    }
	//set the active thread to main
	active_thread = main_thread;

	//fill the context, tid, status
	// dont need a stack or stack_size (already has one)
	curr_tid++;
	main_thread->tid = curr_tid;
	main_thread->stack = NULL;
	main_thread->stacksize = 0;
	main_thread->status = MKTERMSTAT(LWP_LIVE, 0);
    main_thread->sched_one = NULL; 
    main_thread->sched_two = NULL; 

	//adds main thread to scheduler and list of all threads
	add_to_main_list(main_thread);
	active_scheduler->admit(main_thread);
	//yields control to next in scheduler 
	lwp_yield();
}
/*
 Yields control to the next thread as stated by scheduler
 if no next, calls exit
*/
void lwp_yield() {
	// gets the next thread from scheduler 
    thread next_thread = active_scheduler->next();

	// if none, exit
    if (next_thread == NULL) {
        // terminate program
        exit(LWPTERMSTAT(active_thread->status));
    }

	// swap registers from the current(old) to next thread 
	thread old_thread = active_thread;
	active_thread = next_thread;
    swap_rfiles(&(old_thread->state), &(next_thread->state));
}

/* 
	deallocs stacks that were allocated for a thread
	only reports that deallocation failed and nothing else
    because if it fails then we messed up somewhere.
    Munmap failing shouldn't change program behavior
*/
void dealloc_stack(void *addr, size_t size) {
	//given an addr and size of stack, will dealloc it
    if (munmap(addr, size) == -1) {
        perror("munmap failed to deallocate stack");
    }
}

/*
Deallocs the resources of terminated LWP
If threads are running but not terminated
will block until ready 
*/
tid_t lwp_wait(int *status) {
	//pop a terminated thread from the terminated list
	thread terminated = pop_terminated();
	if (terminated) {
		//if there was a thread, dealloc it
		return dealloc_thread(terminated, status);
	} else if (active_scheduler->qlen() <= 1) {
		// if there wasn't a thread, and scheduler is empty
		// return NO_THREAD;
		return NO_THREAD;
	} else {
		//now the current thread must block
        thread waiter = active_thread;
		// removes itself from scheduler and adds to waiting list
        active_scheduler->remove(waiter);
        push_waiting(waiter);
		// yields to another lwp (blocking)
        lwp_yield();
		// finds what thread exited and deallocs it
        thread terminated = waiter->exited;
        if (terminated) {
            return dealloc_thread(terminated, status);
        }
		//shouldn't get here 
        fprintf(stderr, "waiting thread still doesn't have a \
                                    terminated thread after blocking\n");
        return NO_THREAD;
    }
}

// called to dealloc thread main list entry, stack, and the context allocation
tid_t dealloc_thread(thread terminated, int *status) {
    if (status) {
		// reports status back
        *status = terminated->status;
    }
	// gets the tid of thread to deallocated
    tid_t tid = terminated->tid;
   
    if (terminated->stack) {
		//if it had a stack, dealloc it
        dealloc_stack(terminated->stack, terminated->stacksize);
    }
	remove_from_main_list(terminated);
    free(terminated);
    return tid;
}
/*
Terminates the calling thread
termination status becomes the low 8 bits of int status
yields control, adds to terminated list
*/
void lwp_exit(int status) {
	//converts the lower 8 bits for status
    active_thread->status = MKTERMSTAT(LWP_TERM, status);
	//removes the terminated from scheduler
	active_scheduler->remove(active_thread);
	//pushes to the terminated list 
    push_terminated(active_thread);
	//checks if there is a thread waiting to dealloc
	thread waiter = pop_waiting();
	if (waiter){
		// set the exited and admit the waiter back to scheduler
		waiter->exited = pop_terminated();
		active_scheduler->admit(waiter);
	}
	//yield control
    lwp_yield();
}


// ensures all threads will call lwp_exit, and fakes the function call
static void lwp_wrap(lwpfun fun, void *arg) {
    /* Call the given lwpfunction with the given argument.
    * Calls lwp exit() with its return value
    */
    int rval;
    rval=fun(arg);
    lwp_exit(rval);
}

// function that pushes a word to the stack 
static unsigned long *push_word(unsigned long *sp, unsigned long val) {
    *(--sp) = val;
    return sp;
}

// function used to add threads to list with all threads 
void add_to_main_list(thread my_thread) {
	MainList  * new = malloc(sizeof(MainList));
	if (new == NULL) {
        fprintf(stderr, "Couldn't allocate mainlist node");
        return;
    }
	//head points to next in list, tail points to NULL
	new->my_thread = my_thread;
	new->next = NULL;
	//if start of list, set both to new
	if (ml_head == NULL){
		ml_head = new;
		ml_tail = new;
	} else {
		//set current tail to point to now new tail
		ml_tail->next = new;
		//update tail
		ml_tail = new;
	}
	return;
}

//function used to remove a thread from list with all threads
void remove_from_main_list(thread my_thread) {
	MainList * curr = ml_head;
	MainList * prev = NULL;
	while (curr != NULL) {
		//if we have found thread
		if (curr->my_thread == my_thread) {
			// if there was a previous, update next to remove curr
			if (prev) {
				prev->next = curr->next;
			} else {
				// update head, (head is only without a prev)
				ml_head = curr->next;
			}
			// if we are removing the tail, then update the tail
			if (curr == ml_tail) {
				ml_tail = prev;
			}
			//dealloc 
			free(curr);
			return;
		}
		//save previous and move to next in queue
		prev = curr;
		curr = curr->next;
	}
}

// uses lib_one to hold the pointer to the next
// if tail it points to NULL, we know the head because
// we keep track of it
// this way we can easily set the next head after popping
void push_terminated(thread t){
	t->next_terminated = NULL;
	if (terminated_tail == NULL) {
		//if start of list, set both tail to new
		terminated_head = t;
		terminated_tail = t;
	} else {
		//set current tail to point to now new tail
		terminated_tail->next_terminated = t;
		terminated_tail = t;
	}
}
// pops a thread from the front of the terminated list (FIFO)
thread pop_terminated(void){
	// if list is empty return
	if (terminated_head == NULL) {
		return NULL;
	}
	//retrieve the head of list to pop
	thread t = terminated_head;
	//set next head to next in line
	terminated_head = t->next_terminated;
	//if the head was the tail then also set the tail to NULL
	if (terminated_head == NULL) {
		terminated_tail = NULL;
	}
	//the thread just popped is now no longer in list, so next is NULL
	t->next_terminated = NULL;
	return t;
}
// same as push_terminated but for waiting list
// pushes to the waiting list
void push_waiting(thread t){
	t->next_waiting= NULL;
	//if start of list, set both tail to new
	if (waiting_tail == NULL) {
		waiting_head = t;
		waiting_tail = t;
	} else {
		//set current tail to point to now new tail
		waiting_tail->next_waiting = t;
		waiting_tail = t;
	}
}

// pops a thread from the front of the waiting list (FIFO)
thread pop_waiting(void){
	if (waiting_head == NULL) {
		return NULL;
	}
	//retrieve the head of list to pop
	thread t = waiting_head;
	//set next head to next in line
	waiting_head = t->next_waiting;
	//if the head was the tail then also set the tail to NULL
	if (waiting_head == NULL) {
		waiting_tail = NULL;
	}
	//the thread just popped is now no longer in list, so next is NULL
	t->next_waiting = NULL;
	return t;
}

// returns tid of active thread or NO_THREAD if no active thread
tid_t lwp_gettid(void) {
	if (active_thread == NULL) {
		return NO_THREAD;
	} else {
		return active_thread->tid;
	}
}

//returns the thread corresponding to the given tid, NULL if not valid/not found
thread tid2thread(tid_t tid) {
	MainList * n = ml_head;
	if (tid == NO_THREAD){
		return NULL;
	}
	// if we have an active thread and our tid match, return active thread
	if (active_thread && active_thread->tid == tid ){
		return active_thread;
	}
	//starting at head, go thru main list checking every thread for tid
	for (n = ml_head; n != NULL; n = n->next){
		if (n->my_thread->tid == tid) {
			return n->my_thread;
		}
	}
	//returns NULL if tid couldn't be found
	return NULL;
}

//updates the scheduler that chooses the next process to run
//transfers threads from old to new scheduler 
void lwp_set_scheduler(scheduler s) {
	//if scheduler is NULL, set to round robin
	if (s == NULL) {
		active_scheduler = &rr_publish;
        return;
	}
	//if scheduler is the same as what we currently have return
	if (s == active_scheduler) {
		return;
	}

	//init the new scheduler
	if (s->init) {
		s->init();
	}

	if (active_scheduler) {
		// for every thread in old scheduler
		// admit to the new, and remove from the old
        thread t = active_scheduler->next();
		while (t != NULL) {
			active_scheduler->remove(t);
			s->admit(t);
            t = active_scheduler->next();
		}
		//if shutdown is valid, shutdown old scheduler
		if (active_scheduler->shutdown) {
			active_scheduler->shutdown();
		}
	} 
	//update scheduler
	active_scheduler = s;

}

//returns a pointer to the current scheduler
scheduler lwp_get_scheduler(void){
	return active_scheduler;
}
