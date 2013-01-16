#include <assert.h>
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
    - char ***arg_string_ptr: Pointer to the array of argument strings from the first 
                          command. Caller should free this array when 
                          done. This array will include redirection
                          stuff (like "<", "blah.txt", ">", foo.txt")
                          with the filenames separate from the triangle
                          brackets.
    - size_t *num_arg_strings: size(arg_strings)

Output (char *): Pointer to the unused remainder of the string. 
*/
char *parse_pipes(char *command, char ***arg_string_ptr, size_t *num_arg_strings) {
    char **arg_strings = *arg_string_ptr;
    // Keep track of array size so we can resize.
    *num_arg_strings = 0; 

    // Kill leading whitespace
    for(; *command == ' ' || *command == '\t'; ++command) {}
    char* last_cmd_ptr = command;
    int idx;
    int num_false_spaces = 0;
    int inside_quotes = 0;
    for(idx = 0; idx < strlen(command); ++idx) {
        char *cur_cmd_ptr = command + idx;

        if(idx == strlen(command)-1){
             void *ptr = realloc(arg_strings, 
                                 ((*num_arg_strings) + 1)*sizeof(char *));
             assert(ptr != NULL);
             arg_strings = ptr;

             *num_arg_strings = (*num_arg_strings) + 1;
             arg_strings[(*num_arg_strings) - 1] = 
                 strndup(last_cmd_ptr, (int)(cur_cmd_ptr - last_cmd_ptr));

             *arg_string_ptr = arg_strings;
             return command + idx; // Pointer to the \0 in command (no 'rest')
        } else if(*cur_cmd_ptr == ' ' || *cur_cmd_ptr == '\t') { 
             if(inside_quotes == 1) { continue; }
             // Make sure there are no other delimiting characters before
             // next information (look ahead)
             char *delim_test = cur_cmd_ptr;
             int false_space = 0;
             while(*delim_test == ' ' || *delim_test == '\t' ||
                   *delim_test == '<' ||
                   *delim_test == '>' || *delim_test == '|') {
                if(*delim_test == '<' || *delim_test == '>' ||
                   *delim_test == '|') {
                    false_space = 1;
                    break;
                }
                delim_test = delim_test + 1;
                num_false_spaces = num_false_spaces + 1;
             }

             // delim_test is now at the correct delimiter; set the idx
             // so that the next go-around deals with that
             if(false_space == 1) {
                idx = (int)(delim_test - command) - 1;
                continue;
             }
             num_false_spaces = 0;

             void *ptr = realloc(arg_strings, 
                                 ((*num_arg_strings) + 1)*sizeof(char *));
             assert(ptr != NULL);
             arg_strings = ptr;
             *num_arg_strings = (*num_arg_strings) + 1;
             arg_strings[(*num_arg_strings) - 1] = 
                 strndup(last_cmd_ptr, (int)(cur_cmd_ptr - last_cmd_ptr));

             // Set last_cmd_ptr and idx knowing that delim_test is at the next
             // non-delim character:
             last_cmd_ptr = delim_test;
             idx = (int)(delim_test - command) - 1;
             if(delim_test == command+strlen(command)-1) {
                 // Command ends in spaces
                 *arg_string_ptr = arg_strings;
                 return command + strlen(command)-1;
             }
        } else if (*cur_cmd_ptr == '<') {
             void *ptr = realloc(arg_strings, 
                                 ((*num_arg_strings) + 2)*sizeof(char *));
             assert(ptr != NULL);
             arg_strings = ptr;
             *num_arg_strings = (*num_arg_strings) + 2;
             arg_strings[(*num_arg_strings) - 1] = strndup("<", 1);
             arg_strings[(*num_arg_strings) - 2] = 
                 strndup(last_cmd_ptr, 
                         (int)(cur_cmd_ptr - last_cmd_ptr) - num_false_spaces);
             num_false_spaces = 0;

             // Find the next non-space character (assume that there are no
             // other delimeters before the next word, cause we need something
             // to redirect to/from)
             char *delim_test = cur_cmd_ptr + 1;
             for(; *delim_test == ' ' || *delim_test == '\t'; ++delim_test) {}
             last_cmd_ptr = delim_test;
             idx = (int)(delim_test - command) - 1;
        } else if (*cur_cmd_ptr == '>') {
             void *ptr = realloc(arg_strings, 
                                 ((*num_arg_strings) + 2)*sizeof(char *));
             assert(ptr != NULL);
             arg_strings = ptr;
             *num_arg_strings = (*num_arg_strings) + 2;
             arg_strings[(*num_arg_strings) - 1] = strndup(">", 1);
             arg_strings[(*num_arg_strings) - 2] = 
                 strndup(last_cmd_ptr, 
                         (int)(cur_cmd_ptr - last_cmd_ptr) - num_false_spaces);
             num_false_spaces = 0;
             
             // Find the next non-space character (assume that there are no
             // other delimeters before the next word, cause we need something
             // to redirect to/from)
             char *delim_test = cur_cmd_ptr + 1;
             for(; *delim_test == ' ' || *delim_test == '\t'; ++delim_test) {}
             last_cmd_ptr = delim_test;
             idx = (int)(delim_test - command) - 1;
        } else if (*cur_cmd_ptr == '|') {
             void *ptr = realloc(arg_strings, 
                                 ((*num_arg_strings) + 1)*sizeof(char *));
             assert(ptr != NULL);
             arg_strings = ptr;
             *num_arg_strings = (*num_arg_strings) + 1;
             arg_strings[(*num_arg_strings) - 1] = 
                 strndup(last_cmd_ptr, 
                         (int)(cur_cmd_ptr - last_cmd_ptr) - num_false_spaces);
             num_false_spaces = 0;

            *arg_string_ptr = arg_strings;
            return command+idx+1; // Pointer to one past the pipe
        } else if (*cur_cmd_ptr == '"') { 
            if(inside_quotes == 0) { inside_quotes = 1; }
            if(inside_quotes == 1) { inside_quotes = 0; }
        }
        // Otherwise do nothing -- we're in the middle of a word

    }

    *arg_string_ptr = arg_strings;

    return NULL; // If it gets here, something went wrong.
}

/*
invoke: Takes a command string and forks a child process to run the first 
command and (recursively) fork further child processes to run the remaining 
piped commands, etc.

Input:
    - char *command: The command string
    - int pipe_fd: If invoke is being called recursively, the file descriptor
                   for the read side of the pipe. -1 otherwise.

Output (int): 0 if success, -1 if something failed
*/
int invoke(char *command, int pipe_fd) {
    // Count the number of pipes
    char *cur_cmd_ptr = command;
    int num_pipes = 0;
    while(strchr(cur_cmd_ptr, '|') != NULL)
    {
        num_pipes = num_pipes + 1;
        cur_cmd_ptr = strchr(cur_cmd_ptr, '|') + 1;
    }

    // Parse the command
    char **arg_strings = malloc(sizeof(char *));
    char ***arg_string_ptr = &arg_strings;
    size_t num_args = 0;
    char *rest = parse_pipes(command, arg_string_ptr, &num_args);
    arg_strings = *arg_string_ptr;

    // Actually do the command and forking etc.
    // Get the argv array that'll be passed to the process.
    int arg_idx = num_args;
    int num_redir = 0;
    // Assuming that any < and > commands are at the end.
    if(arg_idx > 2 && (arg_strings[arg_idx-2][0] == '<' || 
       arg_strings[arg_idx-2][0] == '>')) {
        arg_idx = arg_idx - 2;
        num_redir = num_redir + 1;
    }
    // If the last command was a redirect, then arg_idx was decremented and
    // this is the second to last, else this is the same comparison
    if(arg_idx > 2 && (arg_strings[arg_idx-2][0] == '<' || 
       arg_strings[arg_idx-2][0] == '>')) {
        arg_idx = arg_idx - 2;
        num_redir = num_redir + 1;
    }

    // Make the argv array
    char **real_argv = (char **)malloc((arg_idx+1) * sizeof(char *));
    if(real_argv == NULL) {
        fprintf(stderr, "malloc error\n");
        return -1;
    }
    if(memcpy((void *)real_argv, (const void*)arg_strings, 
               arg_idx * sizeof(char *)) == NULL) {
        perror("memcpy error");
        exit(EXIT_FAILURE);
    }

    real_argv[arg_idx] = NULL;

    // Strip quotes
    int i;
    for(i = 0; i < arg_idx; ++i) {
        if(real_argv[i][0] == '"') { 
            char *temp = real_argv[i][0];
            real_argv[i] = strndup(real_argv[i]+1, strlen(real_argv[i])-2);
            free(temp);
        }
    }
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

            // If pipe_fd != -1, then this invoke was called from a process
            // that had stdout to pipe to us and this is the child that
            // should be reading that.
            if(pipe_fd != -1) {
                dup2(pipe_fd, STDIN_FILENO);
                close(pipe_fd);
            }

            // Deal with pipes if this process is to output things to
            // another. In that case, this will be the parent that should
            // be writing it.
            if(num_pipes != 0) {
                int fd[2];
                if(pipe(fd) < 0) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
                invoke(rest, fd[0]);
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
            }

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
            // Wait for all piped children, but only if main shell
            int i;
            if(pipe_fd == -1) {
                for(i = 0; i < num_pipes + 1; ++i) {

                    pid_t w = waitpid(-getpgrp(), &status, 0);
                    //wait(&status);
                    if(w == -1) { perror("waitpid"); exit(EXIT_FAILURE); }
                }
            }
            for(i = 0; i < num_args; ++i) {
                free(arg_strings[i]);
            }
            free(arg_strings);
            free(real_argv);
        }       
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Create a new group for later
    setpgid(getpid(), getpid()); // Set group to current PID
    // Loop FOREVER
    while(1) {
        char *uname = getlogin();
        char host[250];
        gethostname(host, 250);
        char cwd[250];
        getcwd(cwd, 250);
        fflush(stdout);
        fprintf(stdout, "%s@%s:%s$$$ ", uname, host, cwd);
        char buf[1024];
        fgets(buf, 1024, stdin);
        invoke(buf, -1);
    }

    return 0;
}
