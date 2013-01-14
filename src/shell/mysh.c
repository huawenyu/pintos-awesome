#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 
parse_pipes: Takes a command string, parses out an array of argument strings
(across pipes), and returns a pointer to the unparsed remainder of the command
string.

Input:
    - char *command: The command string.
    - char **arg_strings: The array of argument strings from the first 
                          command. Caller should free this array when 
                          done. This array will include redirection
                          stuff (like "< blah.txt", "> foo.txt").
    - size_t *num_arg_strings: size(arg_strings)

Output (char *): Pointer to the unused remainder of the string. 
*/
char *parse_pipes(char *command, char **arg_strings, size_t *num_arg_strings) {
    // Keep track of array size so we can resize.
    *num_arg_strings = 0; 

    //TODO (oh god this is annoying)

    return NULL;
}

/*
invoke: Takes a command string and forks a child process to run the first command and
(recursively) fork further child processes to run the remaining piped commands, etc.

Input:
    - char *command: The command string

Output (int): 0 if success, -1 if something failed
*/
int invoke(char *command) {
    // TODO: Logic to split the string (use parse_pipes)

    // Actually do the command and forking etc.
    //char *arg_strings[] = {"cat", "< blah.txt"};
    char *arg_strings[] = {"ls"};
    char *real_argv[] = {"ls", NULL};
    char *rest = "";
    if(strcmp(arg_strings[0], "cd") == 0 || strcmp(arg_strings[0], "chdir") == 0) {
    } else if(strcmp(arg_strings[0], "exit") == 0 ) {
        exit(EXIT_SUCCESS);
    } else { // Not an internal command
        // If the rest is not empty, need to do the pipe stuff TODO
        if(strlen(rest) != 0) {

        }
        // Fork!
        pid_t pid;
        pid = fork();
        if(pid == -1) {
            perror("Fork error");
            exit(EXIT_FAILURE);
        } else if(pid == 0) { // Child process
            // Output indirection TODO

            // Run the process
            if(execvp(arg_strings[0], real_argv) == -1) {
                perror("Execvp error");
            }
        } else { // Parent
            int status;
            waitpid(pid, &status, 0);
        }       
    }

    // TODO

    return 0;
}

int main(int argc, char *argv[]) {
    invoke("");
    fprintf(stdout, "Shell successfully invoked\n");
    return 0;
}
