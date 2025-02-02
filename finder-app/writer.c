#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>

int main(int argc, char **argv) {
    // open syslog
    openlog(NULL, 0, LOG_USER);

    // guard clause for argument count 
    if(argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
        syslog(LOG_ERR, "Please specify exactly 2 arguments.");
        return 1;
    }

    // inflate arguments in argv
    char *file_to_write = argv[1];
    char *string_to_write = argv[2];

    // open the file specified
    FILE *f = fopen(file_to_write, "w");
    if(f == NULL) {
        syslog(LOG_ERR, "File open unsuccessful at %s", file_to_write);
        return 1;
    }

    // write to file
    int fwrite_ret = fprintf(f, "%s", string_to_write);
	if(fwrite_ret < 0) {
        syslog(LOG_ERR, "Writing to file unsuccessful.");
        return 1 ;
    }
    syslog(LOG_DEBUG, "Writing %s to %s", string_to_write, file_to_write);
    
	// close file and syslog
    fclose(f);
    closelog();	
}
