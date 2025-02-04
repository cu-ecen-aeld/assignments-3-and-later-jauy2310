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

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

	// check if the command given is an absolute path
	if(command[0][0] != '/') {
		return false;
	}

	// fork here and check status
	pid_t forked_pid = fork();
	if(forked_pid == -1) {
		return false; 
	}

	// execute if successful
	int execv_result = execv(command[0], command);
	if(execv_result != 0) {
		return false;
	}

	// waitpid
	int status;
	if(waitpid(forked_pid, &status, 0) == -1) {
		return false;
	}

	// check child process status
	if(WIFEXITED(status)) {
		return false;
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

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
	
	// check if path is absolute
	if(command[0][0] != '/') {
		return false;
	}

	// open the file to write to
	int f = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if(f < 0) {
		return false;
	}

	// invoke fork()
	int forked_pid = fork();
	if(forked_pid == -1) {
		return false;
	}

	// execute if successful
	if(dup2(f, 1) < 0) {
		close(f);
		return false;
	}
	int execv_result = execv(command[0], command);
	if(execv_result != 0) {
		return false;
	}

	// waitpid
	int status;
	if(waitpid(forked_pid, &status, 0) == -1) {
		return false;
	}

	// check status of child process
	if(WIFEXITED(status)) {
		return false;
	}

	// close file
	close(f);

	// end of user code; exit function here
    va_end(args);

    return true;
}
