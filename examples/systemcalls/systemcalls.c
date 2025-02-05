#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
	// return false if the CMD is empty
	if(cmd == NULL) {
		return false;
	}

	// execute system()
	int rc = system(cmd);
	
	// handle errors returned from system()
	if(rc != 0) {
		return false;
	}
	
	// if program reaches here, no errors should be returned
    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

	// check if command is absolute
	char *command_path = command[0];
	if(command_path[0] != '/') return false;

	// begin calling execv
	int status;
	pid_t pid;

	pid = fork();
	if(pid == -1) {
		return false;
	} else if(pid == 0) {
		execv(command[0], command);
		return false;
	}

	// waitpid
	if(waitpid(pid, &status, 0) == -1) return false;

	// check status
	if(WIFEXITED(status)) {
		if(WEXITSTATUS(status) == 0) {
			return true;
		} else {
			return false;
		}
	}

	// end of user code; exit function here
    va_end(args);

    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

	command[count] = command[count];
/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

	// check if command is absolute
	char *command_path = command[0];
	if(command_path[0] != '/') return false;

	// begin calling execv
	int status;
	pid_t pid;
	int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if(fd < 0) return false;

	pid = fork();
	if(pid == -1) {
		return false;
	} else if(pid == 0) {
		if(dup2(fd,1) < 0) return false;
		execv(command[0], command);
		return false;
	}

	// waitpid
	if(waitpid(pid, &status, 0) == -1) return false;

	// check status
	if(WIFEXITED(status)) {
		if(WEXITSTATUS(status) == 0) {
			return true;
		} else {
			return false;
		}
	}

	// close file
	close(fd);

	// end of user code; exit function here
    va_end(args);

    return true;
}
