#include <stdio.h>
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

Output (char *): Pointer to the unused remainder of the string. 
*/
char *parse_pipes(char *command, char **arg_strings) {
    // Keep track of array size so we can resize.
    size_t num_arg_strings = 0; 

    //TODO
}

/*
invoke: Takes a command string and forks a child process to run the first command and
(recursively) fork further child processes to run the remaining piped commands, etc.

Input:
    - char *command: The command string

Output (int): Zero if success, -1 if something failed
*/
int invoke(char *command) {
    // TODO
}

int main(int argc, char* argv[]) {

}
