// include standard libraries
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>

// include network libraries
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

// define constants
#define PORT                "9000"
#define NUM_CONNECTIONS     1
#define TMPDATA_PATH        "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE         1024 * 1024

// file descriptors
static int tmpdata_fd;
static int server_socket_fd;
static int client_fd;

// function prototypes

/**
 * signal_handler()
 * 
 * Handles signals such as SIGINT or SIGTERM to gracefully shut down socket server
 * 
 * @return none
 */
void signal_handler();

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

