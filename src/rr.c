#include "rr.h"
#include "lwp.h"
#include <stdlib.h>
#include <stdio.h>

Node *head = NULL;
Node *tail = NULL;
int len = 0;

//next and prev are used kind of like left and right
// if looked at as 

// head <-> thread1 <-> thread2 <-> tail

//round robin admit, adding to the list
void rr_admit(thread new) {
	//allocate a node for admitted thread
    Node *n = malloc(sizeof(Node));

	// set the thread in the struct
    n->my_thread = new;
    n->next = tail;
    n->prev = NULL;
	// if it has a tail, put the new at the end, else make it the head
    if (tail) {
        tail->prev = n;
    } else {
        head = n;
    }
	//set tail to newest
    tail = n;
	//update len
    len++;
}

//removes a thread from the scheduler
void rr_remove(thread victim) {
	//starting at head, search down the list checking all the threads
    Node *curr = head;
    while (curr != NULL) {
        if (curr->my_thread == victim) {
			// if the curr is the head in the list, then move head
            if (curr == head) {
                if (head->prev) {
                    head = head->prev;
                    head->next = NULL;
                } else {
					//if it was the head, scheduler is empty
                    head = NULL;
                    tail = NULL;
                }
            } else if (curr == tail) {
                // if curr is tail, it must have a next 
                //otherwise tail == head and it would've been handled above
				//set tail to whatever was next to it
                    tail->next->prev = NULL;
                    tail = tail->next;
            } else {
				// remove it in place setting pointers 
                curr->next->prev = curr->prev;
                curr->prev->next = curr->next;
            }
			//dealloc the struct 
            free(curr);
            len--;
			return;
        }

        curr = curr->prev;
    }
    return;
}


//returns next thread in the list and place at back
thread rr_next(void) {
    if (head) {
		//if there is head, get thread, remove and admit it back
		//return thread
        thread next_thread = head->my_thread;
        rr_remove(next_thread);
        rr_admit(next_thread);
        return next_thread;
    } else {
        return NULL;
    }
}

//return len of scheduler 
int rr_qlen(void) {
    return len;
}