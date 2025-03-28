#include "aesdsocket.h"

/**************************************************************************************************
 * MAIN
 **************************************************************************************************/
int main(int argc, char *argv[]) {
    // return values for functions
    int rc = 0;

    // initialize server functions
    initialize_server();

    // getaddrinfo setup - hints
    syslog(LOG_INFO, "Retrieving server address info.");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // get address info, storing it into server_address_info
    rc = getaddrinfo(NULL, PORT, &hints, &server_address_info);
    if (rc != 0) goto exit_free_addrinfo_struct;

    // create server socket
    syslog(LOG_INFO, "Creating server socket.");
    rc = socket(
        server_address_info->ai_family,
        server_address_info->ai_socktype,
        server_address_info->ai_protocol
    );
    if (rc == -1) goto exit_socket_creation;
    server_socket_fd = rc;

    // set up bind options for better debugging
    int optval = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval));

    // bind name to socket
    syslog(LOG_INFO, "Binding server socket.");
    rc = bind(server_socket_fd, server_address_info->ai_addr, server_address_info->ai_addrlen);
    if (rc == -1) goto exit_socket_bind;

    // listen on port
    syslog(LOG_INFO, "Socket listening on port %s", PORT);
    rc = listen(server_socket_fd, NUM_CONNECTIONS);
    if (rc == -1) goto exit_socket_listen;

    // start daemon if -d flag was passed
    start_daemon(argc, argv);

    // start timer
    #if !USE_AESD_CHAR_DEVICE
    initialize_timer();
    #endif

    // accept connections - main program loop
    accept_connections();

// cleanup labels; makes it easier to read code and keep track of frees/closes
exit_socket_listen:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket listen. (errno %d)", errno);
exit_socket_bind:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket bind. (errno %d)", errno);
    close(server_socket_fd);
exit_socket_creation:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket creation. (errno %d)", errno); 
exit_free_addrinfo_struct:
    freeaddrinfo(server_address_info);

    // server cleanup
    cleanup_server();

    // return
    return 0;
}


/**************************************************************************************************
 * FUNCTION DEFINITIONS - SERVER
 **************************************************************************************************/
void initialize_server() {
    // open syslog
    openlog(NULL, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_USER);

    // initialize thread manager
    SLIST_INIT(&thread_manager);

    // return
    return;
}

void initialize_timer() {
    // create the signal event for the interval timer
    struct sigevent timer_sig_event;
    memset(&timer_sig_event, 0, sizeof(timer_sig_event));
    timer_sig_event.sigev_notify = SIGEV_SIGNAL;
    timer_sig_event.sigev_signo = SIGALRM;
    timer_sig_event.sigev_value.sival_ptr = &timer_id;

    if (timer_create(CLOCK_REALTIME, &timer_sig_event, &timer_id) == -1) {
        syslog(LOG_ERR, "Creating timer failed");
        return;
    }

    // configure the timer's interval value
    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = TIMER_FREQ_S;
    timer_spec.it_value.tv_nsec = 0;
    timer_spec.it_interval.tv_sec = TIMER_FREQ_S;
    timer_spec.it_interval.tv_nsec = 0;

    if (timer_settime(timer_id, 0, &timer_spec, NULL) == -1) {
        syslog(LOG_ERR, "Setting timer failed");
        return;
    }

    // success; return
    syslog(LOG_INFO, "Timer set successfully.");
    
    // set up signal handler
    // https://stackoverflow.com/questions/2485028/signal-handling-in-c
    // https://pubs.opengroup.org/onlinepubs/009695399/functions/sigaction.html
    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGALRM, &sigact, (struct sigaction *)NULL);
    sigaction(SIGINT, &sigact, (struct sigaction *)NULL);
    sigaction(SIGTERM, &sigact, (struct sigaction *)NULL);
}

void cleanup_server() {
    // clean thread manager
    thread_entry_freeall();

    // attempt to close files
    close(server_socket_fd);

    // attempt to free addrinfo struct
    freeaddrinfo(server_address_info);

    // remove tmpdata file if it is not a char driver
    #if !USE_AESD_CHAR_DEVICE
    remove(TMPDATA_PATH);
    #endif

    // close syslog
    closelog();
}

void accept_connections() {
    while (1) {
        // create client address info
        struct sockaddr client_address_info;
        socklen_t client_address_len = sizeof(client_address_info);
        char client_ip[INET_ADDRSTRLEN];

        // create client address info struct
        syslog(LOG_INFO, "Accepting socket connection.");
        int client_fd = accept(server_socket_fd, (struct sockaddr *)&client_address_info, &client_address_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "accept() failed. (errno %d)", errno);
            continue;
        }

        // log client connection
        struct sockaddr_in *client = (struct sockaddr_in *)&client_address_info;
        inet_ntop(client->sin_family, &client->sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // open file
        int tmpdata_client_fd = open(TMPDATA_PATH, O_APPEND | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (tmpdata_client_fd == -1) {
            syslog(LOG_ERR, "Failed to open %s", TMPDATA_PATH);
            close(client_fd);
            continue;
        }

        // create a new entry
        thread_entry_t *new_connection = thread_entry_create(0, client_ip, client_fd, tmpdata_client_fd);
        if (new_connection == NULL) {
            syslog(LOG_ERR, "Error malloc'ing memory for new thread entry");
            continue;
        }

        // create a new pthread
        if (pthread_create(&new_connection->thread_id, NULL, client_handler, new_connection) != 0 ) {
            close(client_fd);
            thread_entry_free(new_connection);
            continue;
        } else {
            // add thread to thread manager
            thread_entry_add(new_connection);
        }

        thread_entry_printall();

        // check if any of the current threads need to be joined
        thread_entry_t *current_entry = NULL;
        SLIST_FOREACH(current_entry, &thread_manager, entries) {
            if (current_entry->is_complete) {
                // join thread for completed threads
                pthread_join(current_entry->thread_id, NULL);
                close(current_entry->tmpdata_fd);
                thread_entry_remove(current_entry->thread_id);
            }
        }
    }
}

void *client_handler(void *arg) {
    // define the client fd
    thread_entry_t *connection = (thread_entry_t *)arg;
    pthread_t thread_id = connection->thread_id;
    int client_fd = connection->client_fd;
    int tmpdata_client_fd = connection->tmpdata_fd;

    // print
    syslog(LOG_DEBUG, "New connection:");
    thread_entry_print(connection);

    // read_buffer to store incoming data
    char read_buffer[BUFFER_SIZE] = {0};

    // write buffer to store outgoing data
    char write_buffer[BUFFER_SIZE] = {0};
    int write_buffer_index = 0;

    // infinite loop to process incoming data while connection is open
    while (1) {
        // receive data from socket
        ssize_t bytes_received = recv(client_fd, read_buffer, BUFFER_SIZE, 0);

        // client connection closed
        if (bytes_received <= 0) {
            if (connection->client_ip != NULL) {
                syslog(LOG_INFO, "Closed client connection from %s.", (char *)connection->client_ip);
            } else {
                syslog(LOG_INFO, "Closed client connection from unknown.");
            }
            break;
        }

        // for loop to process a single sub-buffer
        for (int i = 0; i < bytes_received; i++) {
            if (read_buffer[i] == '\n') {
                // packet completed; send to file
                syslog(LOG_DEBUG, "Packet complete. Data: %s", write_buffer);

                // lock mutex
                syslog(LOG_DEBUG, "Locking tmpdata mutex.");
                pthread_mutex_lock(&file_mutex);

                // switch behavior based on the presence of the IOCTL string
                if (USE_AESD_CHAR_DEVICE && strstr(write_buffer, AESD_IOCTL_SEEKTO)) {
                    // handle ioctl commands

                    // parse the index and offset from the command
                    size_t index, offset;
                    if (sscanf(read_buffer, AESD_IOCTL_SEEKTO_PARSE, &index, &offset) != 2) {
                        // parsing unsuccessful
                        syslog(LOG_ERR, "Parsing ioctl command unsuccessful.");
                    }

                    // save to struct
                    struct aesd_seekto seekto;
                    seekto.write_cmd = index;
                    seekto.write_cmd_offset = offset;

                    // send ioctl to device
                    syslog(LOG_DEBUG, "Received ioctl (index: %ld, offset: %ld)", index, offset);
                    if (ioctl(tmpdata_client_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0) {
                        syslog(LOG_ERR, "Error seeking to index %ld, offset %ld in client.", index, offset);
                        continue;
                    }
                } else {
                    // reset write buffer and write to file
                    if (write(tmpdata_client_fd, write_buffer, write_buffer_index) == -1) {
                        syslog(LOG_ERR, "Error writing buffer to client.");
                        continue;
                    }
                    if (write(tmpdata_client_fd, "\n", 1) == -1) {
                        syslog(LOG_ERR, "Error writing newline to client.");
                        continue;
                    }
                }

                // unlock mutex
                syslog(LOG_DEBUG, "Unlocking manager mutex.");
                pthread_mutex_unlock(&file_mutex);

                // reset write buffer
                memset(write_buffer, 0, BUFFER_SIZE);
                write_buffer_index = 0;

                // packet was received; send entire contents of file to client
                char file_content[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = read(tmpdata_client_fd, file_content, sizeof(file_content))) > 0) {
                    send(client_fd, file_content, bytes_read, 0);
                }
            } else {
                write_buffer[write_buffer_index++] = read_buffer[i];
            }
        }
    }

    // mark thread as complete
    thread_entry_markcomplete(thread_id);

    // cleanup
    syslog(LOG_DEBUG, "[CLEAN] Cleaning client connection.");
    close(client_fd);
    return NULL;
}

/**************************************************************************************************
 * THREAD MANAGER - Tracks threads for entire application
 **************************************************************************************************/
thread_entry_t *thread_entry_create(pthread_t new_thread_id, const char *new_client_ip,
    int new_client_fd, int new_tmpdata_fd) {
    // allocate memory
    thread_entry_t *new_thread_entry = (thread_entry_t *)malloc(sizeof(thread_entry_t));
    if (!new_thread_entry) {
        syslog(LOG_ERR, "Error malloc'ing thread_entry");
        return NULL;
    }

    // assign fields to new thread entry
    new_thread_entry->thread_id = new_thread_id;
    new_thread_entry->client_ip = strdup(new_client_ip);
    if (!new_thread_entry->client_ip) {
        syslog(LOG_ERR, "Error malloc'ing thread_entry->client_ip");
        free(new_thread_entry->client_ip);
        free(new_thread_entry);
        return NULL;
    }
    new_thread_entry->client_fd = new_client_fd;
    new_thread_entry->is_complete = false;
    new_thread_entry->tmpdata_fd = new_tmpdata_fd;

    // return
    return new_thread_entry;
}

int thread_entry_free(thread_entry_t *entry) {
    // check if entry is already null
    if (!entry) {
        syslog(LOG_ERR, "Thread being free'd is already NULL");
        return -1;
    }

    // free string in struct
    if (entry->client_ip != NULL) {
        free(entry->client_ip);
    }

    // free struct malloc
    free(entry);

    // return success
    return 0;
}

int thread_entry_add(thread_entry_t *entry) {
    // check if entry is valid
    if (!entry) {
        syslog(LOG_ERR, "Thread entry to add is NULL");
        return -1;
    }

    // add the thread entry
    syslog(LOG_DEBUG, "Locking manager mutex.");
    pthread_mutex_lock(&manager_mutex);
    SLIST_INSERT_HEAD(&thread_manager, entry, entries);
    syslog(LOG_DEBUG, "Unlocking manager mutex.");
    pthread_mutex_unlock(&manager_mutex);

    // return 
    return 0;
}

int thread_entry_remove(pthread_t thread_id) {
    // malloc a current node to use for checking
    thread_entry_t *current_entry = NULL;

    // lock the manager
    syslog(LOG_DEBUG, "Locking manager mutex.");
    pthread_mutex_lock(&manager_mutex);
    SLIST_FOREACH(current_entry, &thread_manager, entries) {
        if (current_entry->thread_id == thread_id) {
            // remove entry from list
            SLIST_REMOVE(&thread_manager, current_entry, thread_entry_t, entries);

            // free the removed struct
            thread_entry_free(current_entry);

            // unlock mutex before returning
            syslog(LOG_DEBUG, "Unlocking manager mutex.");
            pthread_mutex_unlock(&manager_mutex);

            // return
            return 0;
        }
    }
    syslog(LOG_DEBUG, "Unlocking manager mutex.");
    pthread_mutex_unlock(&manager_mutex);

    // no thread_ids matched
    return -1;
}

int thread_entry_markcomplete(pthread_t thread_id) {
    // malloc a current node to use for checking
    thread_entry_t *current_entry = NULL;

    // lock the manager
    syslog(LOG_DEBUG, "Locking manager mutex.");
    pthread_mutex_lock(&manager_mutex);
    SLIST_FOREACH(current_entry, &thread_manager, entries) {
        if (current_entry->thread_id == thread_id) {
            // set the is_complete field for this thread to true
            current_entry->is_complete = true;

            // unlock mutex before returning
            syslog(LOG_DEBUG, "Unlocking manager mutex.");
            pthread_mutex_unlock(&manager_mutex);

            // return
            return 0;
        }
    }
    syslog(LOG_DEBUG, "Unlocking manager mutex.");
    pthread_mutex_unlock(&manager_mutex);

    // no thread_ids matched
    return -1;
}

void thread_entry_freeall() {
    // create a current node to use for checking
    thread_entry_t *current_entry = NULL;

    while(!(SLIST_EMPTY(&thread_manager))) {
        // get head of thread manager
        current_entry = SLIST_FIRST(&thread_manager);

        // join thread
        pthread_join(current_entry->thread_id, NULL);

        // remove this from thread manager
        thread_entry_remove(current_entry->thread_id);
    }
}

void thread_entry_print(thread_entry_t *current_entry) {
    // print info
    syslog(LOG_DEBUG, "[CLIENT] Thread ID: %d | Client IP: %s | Client FD: %d | Client Data FD: %d | Completion Status: %s\n",
        (int)current_entry->thread_id, current_entry->client_ip, current_entry->client_fd, current_entry->tmpdata_fd, current_entry->is_complete ? "Yes" : "No");
}

void thread_entry_printall() {
    // create a current node to use for checking
    thread_entry_t *current_entry = NULL;

    // print header
    syslog(LOG_DEBUG, "===== [THREAD MANAGER] =====\n");

    // lock the manager
    syslog(LOG_DEBUG, "Locking manager mutex.");
    pthread_mutex_lock(&manager_mutex);
    SLIST_FOREACH(current_entry, &thread_manager, entries) {
        thread_entry_print(current_entry);
    }
    syslog(LOG_DEBUG, "Unlocking manager mutex.");
    pthread_mutex_unlock(&manager_mutex);

    // print footer
    syslog(LOG_DEBUG, "===== [THREAD MANAGER] =====\n");
}

/**************************************************************************************************
 * FUNCTIONS - TIMESTAMP HANDLER
 **************************************************************************************************/
#if USE_AESD_CHAR_DEVICE
void append_timestamp() {
    return;
}
#else
void append_timestamp()
{
    // set up variables
    char timestamp_buffer[64];
    struct tm *tm_info;
    time_t current_time;

    // retrieve current time and turn it into RFC 2822 formatted timestamp
    time(&current_time);
    tm_info = localtime(&current_time);
    strftime(timestamp_buffer, sizeof(timestamp_buffer), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

    // open file
    int timestamp_fd = open(TMPDATA_PATH, O_APPEND | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (timestamp_fd == -1) {
        syslog(LOG_ERR, "[TIMER] Error opening timestamp file. (errno %d)", errno);
        return;
    }

    // write to file
    syslog(LOG_DEBUG, "Locking tmpdata mutex.");
    pthread_mutex_lock(&file_mutex);
    if (write(timestamp_fd, timestamp_buffer, strlen(timestamp_buffer)) == -1) {
        syslog(LOG_ERR, "Error writing timestamp to file.");
    }
    syslog(LOG_DEBUG, "Unlocking tmpdata mutex.");
    pthread_mutex_unlock(&file_mutex);

    // close file
    close(timestamp_fd);
}
#endif

/**************************************************************************************************
 * FUNCTIONS - SIGNAL HANDLER
 **************************************************************************************************/
void signal_handler(int sig) {
    if(sig == SIGALRM)
    {
        syslog(LOG_INFO, "[TIMER] SIGALRM received, writing to file.");
        append_timestamp();
    } else {
        // log signal handler, using re-entrant write() instead of a normal printf/log
        syslog(LOG_INFO, "Caught signal, exiting");

        // cleanup
        cleanup_server();

        // exit
        exit(1);
    }
}

/**************************************************************************************************
 * FUNCTIONS - DAEMON
 **************************************************************************************************/
void start_daemon(int argc, char **argv) {
    // check command line options with getopt()
    // https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    int c;
    bool is_daemon = false;
    while ((c = getopt(argc, argv, "d")) != -1) {
        switch(c) {
            case 'd':
                is_daemon = true;
                break;
            case '?':
                printf("Unknown option `-%c'.\n", optopt);
                exit(-1);
        }
    }

    // start socket daemon, assuming user passed -d as a flag
    // https://stackoverflow.com/questions/17078947/daemon-socket-server-in-c
    // read the above post for classic steps on making a daemon from an executed process
    if (is_daemon) {
        // indicate we are in daemon mode
        syslog(LOG_INFO, "[DAEMON] Starting daemon...");

        // create first child (child A)
        pid_t pid;
        if ((pid = fork()) < 0) {
            // error creating child process
            syslog(LOG_ERR, "[DAEMON] Creating first child process fails.");
            cleanup_server();
            exit(1);
        } else if (pid != 0) {
            // this is the parent process; exit
            exit(0);
        }

        // change child A to be session leader
        setsid();

        // create second child (child B)
        if ((pid = fork()) < 0) {
            // error creating child process
            syslog(LOG_ERR, "[DAEMON] Creating second child process fails.");
            cleanup_server();
            exit(1);
        } else if (pid != 0) {
            // this is child A; exit
            exit(0);
        }

        // child B returns to original program
        return;
    } else {
        syslog(LOG_INFO, "[DAEMON] Starting in normal mode.");
    }
}