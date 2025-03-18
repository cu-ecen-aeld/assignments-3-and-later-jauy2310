/**************************************************************************************************
 * INCLUDES
 **************************************************************************************************/

// include standard libraries
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>

// include network libraries
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

// queue
#include <sys/queue.h>

// multithreading
#include <pthread.h>

/**************************************************************************************************
 * CONSTANTS AND GLOBALS
 **************************************************************************************************/

// build switch - either use a char device or a file in filesystem
#define USE_AESD_CHAR_DEVICE 1
#if USE_AESD_CHAR_DEVICE
    #define TMPDATA_PATH        "/dev/aesdchar"
#else
    #define TMPDATA_PATH        "/var/tmp/aesdsocketdata"
#endif

// define constants
#define PORT                "9000"
#define NUM_CONNECTIONS     10
#define BUFFER_SIZE         1024 * 1024
#define TIMER_FREQ_S        10

// server details
static int server_socket_fd;
struct addrinfo *server_address_info;

// tmpdata file mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// timestamps
timer_t timer_id;

// signal actions
struct sigaction sigact;

/**************************************************************************************************
 * FUNCTION PROTOTYPES
 **************************************************************************************************/
/**
 * initialize_server()
 * 
 * Calls functions to initialize utilities, such as syslog, signal handlers, etc.
 * 
 * @return none
 */
void initialize_server();

/**
 * initialize_timer()
 * 
 * Initializes timer for SIGALRM
 * 
 * @return none
 */
void initialize_timer();

/**
 * cleanup_server()
 * 
 * Cleans server
 * 
 * @return none
 */
void cleanup_server();

/**
 * accept_connections()
 * 
 * Main program loop that accepts connections and spawns client threads
 * 
 * @return none
 */
void accept_connections();

/**
 * client_handler()
 * 
 * Threading function to handle a client connection
 */
void *client_handler(void *arg);

/**
 * append_timestamp
 * 
 * Writes timestamp to a file
 * 
 * @return none
 */
void append_timestamp();

/**
 * signal_handler()
 * 
 * Handles signals such as SIGINT or SIGTERM to gracefully shut down socket server
 * 
 * @return none
 */
void signal_handler(int sig);

/**
 * start_daemon()
 * 
 * Checks command line arguments to see if a "-d" flag was passed, and if so, starts
 * the socket server in daemon mode
 * 
 * @param argc      Number of command line arguments, passed through main()
 * @param argv      String array of command line arguments, passed through main()
 * 
 * @return none
 */
void start_daemon(int argc, char **argv);


/**************************************************************************************************
 * THREAD MANAGER - Tracks threads for entire application
 **************************************************************************************************/

/**
 * struct thread_entry_t
 * 
 * @brief holds a single thread's information, meant to be used in a linked list (SLIST)
 */
typedef struct thread_entry_t {
    pthread_t                       thread_id;          // thread ID
    char *                          client_ip;          // client IP address
    int                             client_fd;          // client connection fd
    bool                            is_complete;        // thread completion boolean
    SLIST_ENTRY(thread_entry_t)     entries;            // next thread entry
} thread_entry_t;

// define a single head of the linked list, called thread_manager
SLIST_HEAD(thread_manager_t, thread_entry_t) thread_manager;

// mutex for thread_manager
pthread_mutex_t manager_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * thread_entry_create()
 * 
 * Creates a new thread entry that will be used to track the threads and corresponding client info
 * 
 * @param new_thread_id             Thread ID
 * @param new_client_ip             Client's IP address
 * @param new_client_fd             File descriptor to access client connection
 * 
 * @return new thread entry
 */
thread_entry_t *thread_entry_create(pthread_t new_thread_id, const char *new_client_ip, int new_client_fd);

/**
 * thread_entry_copy()
 * 
 * Creates a copy of a thread entry
 * 
 * @note caller must free() this copy
 * 
 * @param copy_thread               Thread entry to copy
 * 
 * @return the malloc'd copy of the thread entry
 */
thread_entry_t *thread_entry_copy(thread_entry_t *copy_thread);

/**
 * thread_entry_free()
 * 
 * Frees a thread entry
 * 
 * @param entry                     Thread entry to free
 * 
 * @return 0 on success, -1 on failure
 */
int thread_entry_free(thread_entry_t *entry);

/**
 * thread_entry_add()
 * 
 * Adds a thread entry to the thread manager
 * 
 * @param entry                     Thread entry to add
 * 
 * @return 0 on success, -1 on failure
 */
int thread_entry_add(thread_entry_t *entry);

/**
 * thread_entry_remove()
 * 
 * Removes a thread entry from the thread manager
 * 
 * @param thread_id                 The entry containing the thread_id to remove
 * 
 * @return 0 on success, -1 on failure
 */
int thread_entry_remove(pthread_t thread_id);

/**
 * thread_entry_markcomplete()
 * 
 * Marks a thread in the thread manager as complete
 * 
 * @param thread_id                 The entry containing the thread_id to remove
 * 
 * @return 0 on success, -1 on failure
 */
int thread_entry_markcomplete(pthread_t thread_id);

/**
 * thread_entry_freeall()
 * 
 * Frees all the threads in the thread manager
 * 
 * @return none
 */
void thread_entry_freeall();

/**
 * thread_entry_print()
 * 
 * Prints thread entry info
 * 
 * @param current_entry             The entry to print
 * 
 * @return none
 */
void thread_entry_print(thread_entry_t *current_entry);

/**
 * thread_entry_printall()
 * 
 * Prints all threads in thread manager
 * 
 * @return none
 */
void thread_entry_printall();