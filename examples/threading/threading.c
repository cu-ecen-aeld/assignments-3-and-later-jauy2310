#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
	// typecast void input arg to a thread_data struct
	struct thread_data* thread_func_args = (struct thread_data *) thread_param;

	// wait to obtain mutex
	sleep(thread_func_args->wait_to_obtain_ms);

    // obtain mutex, exiting on failure
    int rc;
    rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0)
    {
        DEBUG_LOG("Obtaining mutex failed, exiting...");
        return NULL;
    }
    
    // wait to release mutex
    sleep(thread_func_args->wait_to_release_ms);

    // release mutex, exiting on failure
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0)
    {
        DEBUG_LOG("Releasing mutex failed, exiting...");
        return NULL;
    }

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    return thread_func_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     * 
     * return true if successful.
     * 
     * See implementation details in threading.h file comment block
     */

    // guard clause
    if (thread == NULL || mutex == NULL)
    {
        ERROR_LOG("Attempted to create a thread using a NULL reference, exiting.");
        return false;
    }

    // malloc thread_data struct
    struct thread_data* tdata = (struct thread_data *)malloc(sizeof(struct thread_data));
    if (tdata == NULL)
    {
        ERROR_LOG("malloc() for struct thread_data failed, exiting.");
        return false;
    }

    // fill fields for struct data
    tdata->thread = thread;
    tdata->mutex = mutex;
    tdata->wait_to_obtain_ms = wait_to_obtain_ms;
    tdata->wait_to_release_ms = wait_to_release_ms;
    
    // call thread function
    struct thread_data *ret_data = threadfunc(tdata);
    if (ret_data == NULL)
    {
        ERROR_LOG("threadfunc() failed, exiting.");
        return false;
    }

    return true;
}

