#include <errno.h>
#include <fcntl.h>
// Apparently not technically required but might be useful
#include <sys/types.h>
#include <sys/stat.h>
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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
                          stuff (like "<", "blah.txt", ">", foo.txt")
                          with the filenames separate from the triangle
                          brackets.
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
invoke: Takes a command string and forks a child process to run the first 
command and (recursively) fork further child processes to run the remaining 
piped commands, etc.

Input:
    - char *command: The command string

Output (int): 0 if success, -1 if something failed
*/
int invoke(char *command) {
    // Count the number of pipes
    char *cur_cmd_ptr = command;
    int num_pipes = 0;
    while(strchr(cur_cmd_ptr, '|') != NULL)
    {
        num_pipes = num_pipes + 1;
        cur_cmd_ptr = strchr(cur_cmd_ptr, '|') + 1;
    }

    // TODO: Logic to split the string (use parse_pipes)

    // Actually do the command and forking etc.
    char *arg_strings[] = {"cat", "<", "mysh.c", ">", "test.txt"};
    char *rest = "";
    // Get the argv array that'll be passed to the process.
    int num_args = sizeof(arg_strings) / sizeof(arg_strings[0]);
    int arg_idx = num_args;
    int num_redir = 0;
    // Assuming that any < and > commands are at the end.
    if(arg_strings[arg_idx-2][0] == '<' || 
       arg_strings[arg_idx-2][0] == '>') {
        arg_idx = arg_idx - 2;
        num_redir = num_redir + 1;
    }
    // If the last command was a redirect, then arg_idx was decremented and 
    // this is the second to last, else this is the same comparison
    if(arg_strings[arg_idx-2][0] == '<' || 
       arg_strings[arg_idx-2][0] == '>') {
        arg_idx = arg_idx - 2;
        num_redir = num_redir + 1;
    }
    char **real_argv = (char **)malloc((num_args+1) * sizeof(char *));
    if(real_argv == NULL) {
        printf(stderr, "malloc error\n");
        return -1;
    }
    if(memcpy((void *)real_argv, (const void*)arg_strings, 
               arg_idx * sizeof(char *)) == NULL) {
        perror("memcpy error");
        exit(EXIT_FAILURE);
    }
    real_argv[arg_idx] = NULL;
    if(strcmp(arg_strings[0], "cd") == 0 || 
       strcmp(arg_strings[0], "chdir") == 0) {
        if(num_args != 2) { 
            fprintf(stderr, "cd only takes one parameter\n");
        }
        chdir(arg_strings[1]);
    } else if(strcmp(arg_strings[0], "exit") == 0 ) {
        if(num_args != 1) { 
            fprintf(stderr, "exit does not take parameters\n");
            return -1;
        }
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
            // Assuming that pipes and I/O redirection aren't being 
            // simultaneously used in a way where something would stomp over 
            // things.
            // I/O indirection
            int bracket_idx = num_args - 2;
            int filename_idx = num_args - 1;
            while(num_redir > 0) {
                if(arg_strings[bracket_idx][0] == '<') { // In
                   int in_fd = open((const char*) arg_strings[filename_idx], 
                                    O_RDONLY); 
                   dup2(in_fd, STDIN_FILENO); // Replace stdin
                   close(in_fd);
                } else { // Out
                   int out_fd = open((const char*) arg_strings[filename_idx],
                                     O_CREAT | O_TRUNC | O_WRONLY,
                                     S_IRUSR | S_IWUSR);
                   dup2(out_fd, STDOUT_FILENO); // Replace stdout
                   close(out_fd);
                }
                num_redir = num_redir - 1;
                bracket_idx = bracket_idx - 2;
                filename_idx = filename_idx - 2;
            } 

            // Run the process
            if(execvp(arg_strings[0], real_argv) == -1) {
                perror("Execvp error");
            }
        } else { // Parent
            int status;
            // Wait for all piped children
            int i;
            for(i=0; i < num_pipes + 1; ++i) {
                waitpid(-getgid(), &status, 0);
            }
            // TODO: Free the strings inside arg_strings 
            // (want to implement parse_pipes first)
            //free(arg_strings);
            free(real_argv);
        }       
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Create a new group for later
    setpgid(0, 0); // Set group to current PID
    // Loop FOREVER
    while(1) {
        char *uname = getlogin();
        char host[250];
        gethostname(host, 250);
        char cwd[250];
        getcwd(cwd, 250);
        fprintf(stdout, "%s@%s:%s$$$ ", uname, host, cwd);
        char buf[1024];
        fgets(buf, 1024, stdin);
        invoke(buf);
    }

    return 0;
}
