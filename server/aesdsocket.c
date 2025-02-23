#include "aesdsocket.h"

int main(int argc, char *argv[]) {
    // open syslog
    openlog(NULL, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_USER);

    // signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // return values for functions
    int rc = 0;

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
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // bind name to socket
    syslog(LOG_INFO, "Binding server socket.");
    rc = bind(server_socket_fd, server_address_info->ai_addr, server_address_info->ai_addrlen);
    if (rc == -1) goto exit_socket_bind;

    // listen on port
    syslog(LOG_INFO, "Socket listening on port %s", PORT);
    rc = listen(server_socket_fd, NUM_CONNECTIONS);
    if (rc == -1) goto exit_socket_listen;

    // set aside client variables accessible outside of loop (for cleanup)
    struct sockaddr client_address_info;
    socklen_t client_address_len = sizeof(client_address_info);
    char client_ip[INET_ADDRSTRLEN];

    // start daemon if -d flag was passed
    start_daemon(argc, argv);

    // main loop; keep accepting/closing connections 
    while (1) {
        // create client address info struct
        syslog(LOG_INFO, "Accepting socket connection.");
        rc = accept(server_socket_fd, (struct sockaddr *)&client_address_info, &client_address_len);
        if (rc == -1) goto exit_socket_accept;
        client_fd = rc;

        // log client connection
        struct sockaddr_in *client = (struct sockaddr_in *)&client_address_info;
        inet_ntop(client->sin_family, &client->sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // open file to write
        tmpdata_fd = open(TMPDATA_PATH, O_APPEND | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP); 
        if (tmpdata_fd == -1) goto retry;

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
            if (bytes_received <= 0) break;

            // for loop to process a single sub-buffer
            for (int i = 0; i < bytes_received; i++) {
                if (read_buffer[i] == '\n') {
                    // packet completed; send to file
                    syslog(LOG_DEBUG, "Packet complete. Data: %s", write_buffer);

                    // reset write buffer and write to file
                    lseek(tmpdata_fd, 0, SEEK_END);
                    write(tmpdata_fd, write_buffer, write_buffer_index);
                    write(tmpdata_fd, "\n", 1);
                    memset(write_buffer, 0, BUFFER_SIZE);
                    write_buffer_index = 0;

                    // packet was received; send entire contents of file to client
                    lseek(tmpdata_fd, 0, SEEK_SET);
                    char file_content[BUFFER_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = read(tmpdata_fd, file_content, sizeof(file_content))) > 0) {
                        send(client_fd, file_content, bytes_read, 0);
                    }
                } else {
                    write_buffer[write_buffer_index++] = read_buffer[i];
                }
            }
        }

retry:
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        close(tmpdata_fd);
    }

// cleanup labels; makes it easier to read code and keep track of frees/closes
exit_socket_accept:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket accept.");
exit_socket_listen:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket listen.");
exit_socket_bind:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket bind.");
    close(server_socket_fd);
exit_socket_creation:
    if (rc == -1) syslog(LOG_ERR, "Exiting socket creation."); 
exit_free_addrinfo_struct:
    freeaddrinfo(server_address_info);

    // return
    remove(TMPDATA_PATH);
    closelog();
    return rc;
}

void signal_handler() {
    // log signal handler, using re-entrant write() instead of a normal printf/log
    syslog(LOG_INFO, "Caught signal, exiting");

    // attempt to close files
    close(tmpdata_fd);
    close(client_fd);
    close(server_socket_fd);

    // attempt to free addrinfo struct
    freeaddrinfo(server_address_info);
    
    // delete file
    remove(TMPDATA_PATH);

    // exit
    exit(1);
}

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
        // change to root
        chdir("/");

        // exit if parent process
        if (fork() > 0) _exit(0);

        // redirect standard outputs to /dev/null
        close(0);
        close(1);
        close(2);
        open("/dev/null", O_RDWR);
        dup(0);
        dup(0);
    }
}