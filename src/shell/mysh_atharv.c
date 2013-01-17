#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

char **tokenize_pipes(char *input) {
    int i = 0;
    int j = 0;
    int num_pipes = 0;
    char *curr_input = input;
    char *substr;
    char **pipe_tokens;

    curr_input = input;
    while (strchr(curr_input, '|') != NULL) {
        num_pipes++;
        curr_input = strchr(curr_input, '|') + 1;
    }

    pipe_tokens = malloc(sizeof(char *) * (num_pipes + 1));
    if (!pipe_tokens) {
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int cpy_idx = 0;
    for (i = 0; i < strlen(input); i++) {
        if (input[i] == '|') {
            substr = malloc(sizeof(char) * (i - cpy_idx + 1));
            strncpy(substr, input + cpy_idx, i - cpy_idx);
            substr[i] = '\0';
            pipe_tokens[j] = substr;
            j++;
            cpy_idx = i + 1;
        }
    }
    substr = malloc(sizeof(char) * (i - cpy_idx + 1));
    strncpy(substr, input + cpy_idx, i - cpy_idx);
    substr[i] = '\0';
    pipe_tokens[j] = substr;

    return pipe_tokens;
}

char **tokenize(char *input) {
    unsigned int i;
    int new_index = 0;
    int quote = 0;
    int num_tokens = 0;
    char *substr;
    
    for (i = 0; i < strlen(input); i++) {
        if (quote) {
            if (input[i] == '"') {
                quote = 0;
            }
            input[new_index] = input[i];
            new_index++;
        }
        else {
            if (input[i] == ' ') {
                if (i > 0 && input[i - 1] != ' ') {
                    input[new_index] = input[i];
                    new_index++;
                }
            }
            else {
                if (input[i] == 10 || input[i] == '\0') {
                    if (input[new_index - 1] != ' ') {
                        input[new_index] = '\0';
                    }
                    else {
                        input[new_index - 1] = '\0';
                    }
                    break;
                }
                if (i == 0 || input[i - 1] == ' ') {
                    num_tokens++;
                }
                if (input[i] == '"') {
                    quote = 1;
                }
                input[new_index] = input[i];
                new_index++;
            }
        }
    }
    
    char** tokens = malloc(sizeof(char *) * (num_tokens + 1));
    if (!tokens) {
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    quote = 0;
    int tok_start = 0;
    int count = 0;
    
    for (i = 0; i < strlen(input); i++) {
        if (input[i] == ' ' && !quote) {
            substr = malloc(sizeof(char) * (i - tok_start + 1));
            if (!substr) {
                fprintf(stderr, "malloc failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            
            if (input[tok_start] != '"') {
                strncpy(substr, input + tok_start, i - tok_start);
                substr[i - tok_start] = '\0';
            }
            else {
                strncpy(substr, input + tok_start + 1, i - tok_start - 2);
                substr[i - tok_start - 2] = '\0';
            }
            
            tokens[count] = substr;
            
            tok_start = i + 1;
            count++;
        }
        else if (input[i] == '"') {
            quote = !quote;
        }
    }
    
    substr = malloc(sizeof(char) * (i - tok_start + 1));
    if (!substr) {
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (input[tok_start] != '"') {
        strncpy(substr, input + tok_start, i - tok_start);
        substr[i - tok_start] = '\0';
    }
    else {
        strncpy(substr, input + tok_start + 1, i - tok_start - 2);
        substr[i - tok_start - 2] = '\0';
    }
    tokens[count] = substr;
    tokens[num_tokens] = NULL;
    
    return tokens;
}

void exec_cd(char **tokens) {
    int stat = 0;
    
    if (tokens[1]) {
        if (strcmp(tokens[1], "~") == 0) {
            stat = chdir(getenv("HOME"));
        }
        else
        {
            stat = chdir(tokens[1]);
        }
    }
    else {
        stat = chdir(getenv("HOME"));
    }
    if (stat) {
        fprintf(stderr, "chdir failed: %s\n", strerror(errno));
    }
    return;
}

int pipe_invoke(char *input) {
    unsigned int i, j;
    char **pipe_tokens = NULL;
    char **tokens = NULL;

    pipe_tokens = tokenize_pipes(input);
    printf("Pipe tokens:\n");
    for (i = 0; pipe_tokens[i] != NULL; i++) {
        printf("%s\n", pipe_tokens[i]);
    }
    printf("End\n");
    
    for (i = 0; pipe_tokens[i] != NULL; i++) {
        tokens = tokenize(pipe_tokens[i]);
        printf("+++\n");
        for (j = 0; tokens[j] != NULL; j++) {
            printf("%s\n", tokens[j]);
        }
    }

    return 0;
}

int invoke(char *input) {
    unsigned int i;
    int input_idx = -1;
    int output_idx = -1;
    char *input_pos = strchr(input, '<');
    char *output_pos = strchr(input, '>');
    
    char **tokens = NULL;
    char **files = NULL;
    char *infile = NULL;
    char *outfile = NULL;
    char *temp = NULL;
    
    if (input_pos && output_pos) {
        
    }
    else if (input_pos) {
        input_idx = input_pos - input;
        
        temp = malloc(sizeof(char) * (strlen(input) - input_idx));
        if (!temp) {
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        strncpy(temp, input_pos + 1, strlen(input) - input_idx - 1);
        temp[strlen(input) - input_idx - 1] = '\0';
        files = tokenize(temp);
        infile = files[0];
        free(files);
        free(temp);
        
        temp = malloc(sizeof(char) * (input_pos - input + 1));
        if (!temp) {
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        strncpy(temp, input, input_pos - input);
        temp[input_pos - input] = '\0';
        tokens = tokenize(temp);
        free(temp);
    }
    else if (output_pos) {
        output_idx = output_pos - input;
        
        temp = malloc(sizeof(char) * (strlen(input) - output_idx));
        if (!temp) {
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        strncpy(temp, output_pos + 1, strlen(input) - output_idx - 1);
        temp[strlen(input) - output_idx - 1] = '\0';
        files = tokenize(temp);
        outfile = files[0];
        free(files);
        free(temp);
        
        temp = malloc(sizeof(char) * (output_pos - input + 1));
        if (!temp) {
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        strncpy(temp, input, output_pos - input);
        temp[output_pos - input] = '\0';
        tokens = tokenize(temp);
        free(temp);
    }
    else {
        tokens = tokenize(input);
    }
    
    int status;
    
    if (strcmp(tokens[0], "cd") == 0 || strcmp(tokens[0], "chdir") == 0) {
        exec_cd(tokens);
    }
    else if (strcmp(tokens[0], "exit") == 0) {
        exit(EXIT_SUCCESS);
    }
    else {
        pid_t pid = fork();
        printf("pid: %i\n", pid);
        if (pid == -1) {
            fprintf(stderr, "fork failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) {
            if (infile) {
                int in_fd = open(infile, O_RDONLY);
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            if (outfile) {
                int out_fd = open(outfile, O_CREAT | O_TRUNC | O_WRONLY, 0);
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }
            if (execvp(tokens[0], tokens) == -1) {
                fprintf(stderr, "execvp failed: %s\n", strerror(errno));
            }
        }
        else {
            wait(&status);
        }
    }
    
    for (i = 0; tokens[i] != NULL; i++) {
        printf("%s\n", tokens[i]);
        free(tokens[i]);
    }
    free(tokens);
    
    if (infile) {
        free(infile);
    }
    
    if (outfile) {
        free(outfile);
    }
    
    return 0;
}

int main() {
    while(1) {
        char *uname = getlogin();
        if (!uname) {
            fprintf(stderr, "getlogin failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        char *cwd = getcwd(0, 0);
        if (!cwd) {
            fprintf(stderr, "getcwd failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        fflush(stdout);
        fprintf(stdout, "%s:%s> ", uname, cwd);
        char buf[1024];
        fgets(buf, 1024, stdin);
        int status = invoke(buf);
        //int status = pipe_invoke(buf);
        if (status) {
            fprintf(stderr, "something failed. \n");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}
