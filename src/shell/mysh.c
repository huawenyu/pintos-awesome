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
                          done. 

Output: Pointer to the unused remainder of the string. 
*/
char *parse_pipes(char *command, char **arg_strings) {
    // Keep track of array size so we can resize.
    size_t num_arg_strings = 0; 

    //TODO
}


int main(int argc, char* argv[]) {

}
