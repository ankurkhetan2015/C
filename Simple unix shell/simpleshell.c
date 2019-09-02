#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

#define MAX_PROMPT 512
#define MAX_LINE   1024
#define MAX_PROCS  20
#define MAX_DIR    1024

enum {RESET, RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN, WHITE};

char *colors[] = {
    "\x1B[0m", "\x1B[31m", "\x1B[32m", "\x1B[34m", "\x1B[33m", "\x1B[35m", "\x1B[36m", "\x1B[37m"
};


/* struct to hold internal data for the shell */
typedef struct
{
    int  quit;
    char prompt[MAX_PROMPT];
    int  show_prompt;
    char old_dir[MAX_DIR];
}shell_data_t;

/* struct to hold the arrays used for the commands */
typedef struct
{
    char **data;
    int  n;
}array_t;

/* malloc with memory error handling */
void *_malloc(size_t size)
{
    void *ptr;
    
    ptr = malloc(size);
    if (ptr == NULL)
    {
        printf("Error: out of memory!\n");
        exit(-1);
    }
    return ptr; 
}

/* realloc with memory error handling */
void *_realloc(void *oldptr, size_t size)
{
    void *ptr;
    
    ptr = realloc(oldptr, size);
    if (ptr == NULL)
    {
        printf("Error: out of memory!\n");
        exit(-1);
    }
    return ptr; 
}


/* Remove spaces at the start and end of the string, returns a pointer to the 
   start of the trimmed string
*/
char *trim_spaces(char *str)
{
    int start;
    int end;

    start = 0;
    while(str[start] != '\0' && isspace(str[start]))
        start++;
    if (str[start] == '\0') /* if it's an empty string, return it*/
        return &str[start];
    end = strlen(str) - 1;
    while(isspace(str[end]))
        str[end--] = '\0'; /* trim string overwriting the space with a zero */
    return &str[start]; /* return the start of the trimmed string */
}

/* create a new empty array of pointers */
array_t *array_new()
{
    array_t *array;
    array = (array_t *) _malloc(sizeof(array_t));    
    array->n = 0;
    array->data = NULL;
    return array;
}

/* delete the array of pointers by freeing all allocated space */
void array_delete(array_t *array)
{
    int i;
    
    for (i = 0; i < array->n; i++)
        free(array->data[i]);
    free(array->data);
    free(array);
}

/* add an element to an array of pointers */
void array_insert(char *element, array_t *array)
{
    if (array->n == 0)
        array->data = _malloc(2*sizeof(char *));
    else
        array->data = _realloc(array->data, (array->n + 2)*sizeof(char *));
    array->data[array->n++] = element;
    array->data[array->n] = NULL;    
}

/* add a token to the array */
void add_token(char *str, int start, int end, array_t *commands, int trim)
{
    char tmp[MAX_LINE];
    char *cmd;

    strncpy(tmp, &str[start], end - start);
    tmp[end - start] = '\0';
    cmd = (trim)? trim_spaces(tmp) : tmp;
    array_insert(strdup(cmd), commands);
}

/* split command line using the given delimiter, it detects quoted strings and 
avoids splitting them */
array_t *split(char *str, char delim)
{
    int i;
    int state;
    int start;
    array_t *commands;
    char *command;

    commands = array_new();
    i = 0;
    state = 0;
    start = 0;
    while(i <= strlen(str))
    {
        switch(state)
        {
        case 0:
            if (str[i] == 0)
                add_token(str, start, i, commands, 1);
            else if(str[i] == delim)
            {
                add_token(str, start, i, commands, 1);
                start = i + 1;
            }
            else if(str[i] == '"')
                state = 1;
            else if(str[i] == '\'')
                state = 2;
            break;
        case 1:
            if(str[i] == '"')
                state = 0;
            break;
        case 2:
            if(str[i] == '\'')
                state = 0;
            break;
        }
        i++;
    }
    if (state) /* open quotes, invalid command */
    {
        array_delete(commands);
        fprintf(stderr, "Error: invalid command line.\n");
        return NULL;
    }
    return commands;
}

/* split command line using spaces, it detects quoted strings and 
avoids splitting them */
array_t *split_spaces(char *str)
{
    int i, j;
    int state;
    int start;
    array_t *commands;
    char *command;
    int trim;

    commands = array_new();
    i = 0;
    state = 0;
    start = 0;
    while(i <= strlen(str))
    {
        switch(state)
        {
        case 0:
            trim = 1;
            start = i;
            if (str[i] == 0)
                state = 0;
            else if(str[i] == '\'')
            {
                start++;
                state = 1;
            }
            else if(str[i] == '"')
            {
                start++;
                state = 2;
            }
            else if(!isspace(str[i]))
                state = 3;
            break;
        case 1:
            if(str[i] == '\'')
            {
                for (j = i; j < strlen(str); j++)
                    str[j] = str[j + 1]; 
                state = 3;
                i--;
                trim = 0;   /* avoid trimming quoted strings */
            }
            break;
        case 2:
            if(str[i] == '"')
            {
                for (j = i; j < strlen(str); j++)
                    str[j] = str[j + 1]; 
                state = 3;
                i--;
                trim = 0;   /* avoid trimming quoted strings */
            }
            break;
        case 3:
            if(str[i] == 0 || isspace(str[i]))
            {
                add_token(str, start, i, commands, trim);
                state = 0;
            }
            else
                trim = 1;
            break;
        }
        i++;
    }
    if (state) /* open quotes, invalid command */
    {
        array_delete(commands);
        fprintf(stderr, "Error: invalid command line.\n");
        return NULL;
    }
    return commands;
}

/* gets the redirections found in the string */
array_t *parse_redirections(char *str)
{
    int i;
    int end;
    array_t *redirs;
    char buffer[MAX_LINE];
    int quote;
    
    redirs = array_new();

    end = strlen(str);
    i = end - 1;
    quote = 0;
    while(i>=0)
    {
        if (!quote)
        {
            if (str[i] == '>' || str[i] == '<')
            {
                add_token(str, i, end, redirs, 1);
                str[i] = 0;
                end = i;
            }
            else if(str[i] == '\'')
                quote = 1;
            else if(str[i] == '"')
                quote = 2;
        }
        else if ((quote == 1 && str[i] == '\'') || (quote == 2 && str[i] == '"'))
            quote = 0;
        i--;
    }
    return redirs;
}

/* executes the cd command */
void execute_cd(char **command, shell_data_t *data)
{
    char cur_dir[MAX_DIR];

    getcwd(cur_dir, MAX_DIR); /* get current directory  */
    if (command[1] == NULL)
        fprintf(stderr, "Error: missing argument for cd\n");
    else if(command[2] != NULL) 
        fprintf(stderr, "Error: too many argument for cd\n");
    else if (!strcmp(command[1], "-"))
    {
        if(chdir(data->old_dir) == -1)
            fprintf(stderr,"Error: unable to change to previous directory\n");
        strcpy(data->old_dir, cur_dir); /* set initial directory as the old one */
    }
    else if(chdir(command[1]) == -1)
    {
        fprintf(stderr,"Error: unable to change to directory %s\n", command[1]);
    }
    else
        strcpy(data->old_dir, cur_dir); /* set initial directory as the old one */
}

/* executes the prompt command */
void execute_prompt(char **command, shell_data_t *data)
{
    if (command[1] == NULL)
        fprintf(stderr, "Error: missing argument for prompt\n");
    else if(command[2] != NULL) 
        fprintf(stderr, "Error: too many argument for prompt\n");
    else
        strcpy(data->prompt, command[1]);
}



/* finds the last redirection of the given type in the redirection array */
int get_first_redirection(char type, array_t *redirs)
{
    int i;

    i = 0;
    while(i < redirs->n && redirs->data[i][0] != type)
        i++;
    if (i >= redirs->n) /* no redirection of the given type */
        return -1;
    return i;
}

/* executes an arbitrary command creating another process for it */
pid_t execute_command(array_t *args, array_t *redirs, int pipe_in, int pipe_out, shell_data_t *data)
{
    pid_t pid;
    int infd,outfd;
    int rin, rout;
    char *filename;

    pid = fork();
    if (pid < 0)
        fprintf(stderr, "Error: unable to fork\n");
    else if (pid == 0)  /* child */
    {
        rin = get_first_redirection('<', redirs);
        rout = get_first_redirection('>', redirs);
        if (rin != -1) /* it has input redirection */
        {
            filename = trim_spaces(&(redirs->data[rin][1]));
            if((infd = open(filename, O_RDONLY))==-1)
            {
                fprintf(stderr, "Error: unable to open file: %s\n",filename);
                exit(1);
            }
            dup2(infd, STDIN_FILENO);
            close(infd);
            if(pipe_in != -1)   /* we won't use the pipe input */
                close(pipe_in);
        }
        else if (pipe_in != -1)
            dup2(pipe_in, STDIN_FILENO);
        if (rout != -1) /* output redirection > */
        {
            filename = trim_spaces(&(redirs->data[rout][1]));

            if((outfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666))==-1)
            {
                fprintf(stderr, "Error: unable to open file: %s\n", filename);
                exit(1);
            }
            dup2(outfd, STDOUT_FILENO);
            close(outfd);
            if(pipe_out!=-1)    /* we won't use the pipe output */
                close(pipe_out);
        }
        else if(pipe_out != -1)
            dup2(pipe_out, STDOUT_FILENO);
        if(execvp(args->data[0],args->data) == -1)
        {
            fprintf(stderr,"Error: %s: command not found\n",args->data[0]);
            exit(1);
        }
        exit(0);
    }
    return pid;
}

/* executes a single command, either internal or external */
pid_t execute_single_command(array_t *args, array_t *redirs, shell_data_t *data)
{
    if (!strcmp(args->data[0], "exit"))
        data->quit = 1;
    else if (!strcmp(args->data[0], "cd"))
        execute_cd(args->data, data);
    else if (!strcmp(args->data[0], "prompt"))
        execute_prompt(args->data, data);
    else
        return execute_command(args, redirs, -1, -1, data);
    return -1;
}

/* split the command line using the pipes, execute each piped command */
void execute_command_line(char *line, shell_data_t *data)
{
    int i;
    array_t *commands;
    array_t *redirs;
    array_t *args;
    int **pipes;
    int pipe_in, pipe_out;
    int background;
    pid_t pids[MAX_PROCS];
    int nprocs, status;

    background = 0;
    nprocs = 0;
    if (line[strlen(line) - 1] == '&')  
    {
        background = 1;
        line[strlen(line) - 1] = 0;
    }
    commands = split(line, '|');   /* split line by pipes */
    if (commands == NULL)
        return;
    if (commands->n == 1)
        pipes = NULL;
    else
    {
        /* creates pipes */
        pipes = (int **) _malloc(commands->n*sizeof(int*));
        for (i = 0; i < commands->n - 1; i++)
        {
            pipes[i] = (int *) _malloc(2*sizeof(int));
            pipe(pipes[i]); /* create pipe */
        }
    }

    for (i = 0; i < commands->n && !data->quit; i++)
    {
        pipe_in = pipe_out = -1;
        if (pipes) /* connect pipe ends */
        {
            if(i > 0)
                pipe_in = pipes[i - 1][0];
            if(i < commands->n - 1)
                pipe_out = pipes[i][1];
        }
        redirs = parse_redirections(commands->data[i]); /* get redirections */
        args = split_spaces(commands->data[i]);   /* split command arguments by spaces */
        if(args)
        {
            if (commands->n == 1)
                pids[nprocs] = execute_single_command(args, redirs, data);
            else
                pids[nprocs] = execute_command(args, redirs, pipe_in, pipe_out, data);
            if (pids[nprocs] != -1)
                nprocs++;
            array_delete(args);
        }
        array_delete(redirs);
        if (pipe_out != -1) /* close unused end */
            close(pipe_out);
    }
    if (pipes != NULL) /* free all pipe memory and close all remaining pipe ends */
    {
        for (i = 0; i < commands->n - 1; i++)
        {
            close(pipes[i][1]);
            free(pipes[i]);
        }
        free(pipes);
    }
    array_delete(commands);

    /* if the command was not bg, wait for processes to end */
    if (!background)
        for(i = 0; i < nprocs; i++) /* wait for all processes to exit */
            (void)waitpid(pids[i], &status, 0);
}

/* prints the prompt, recognizes the following special prompt formats:
    \w to print the current directory
    \u to print the current user name
    \d to print the current date
    \@ to print the current time in 12h format
    \A to print the current time in 24h format
 */
void print_prompt(shell_data_t *data)
{
    char buffer[MAX_DIR];
    char prompt[MAX_PROMPT];
    time_t t;
    struct tm tm;
    int i, len;

    if (data->show_prompt)
    {
        prompt[0] = 0; /* clear prompt */
        for (i = 0; i < strlen(data->prompt); i++)
        {
            if (data->prompt[i] == '\\')
            {
                switch (data->prompt[i + 1])
                {
                case 'w':
                    getcwd(buffer, MAX_DIR); /* get current directory  */
                    break;
                case 'u':
                    getlogin_r(buffer, MAX_DIR); /* get user name*/
                    break;
                case 'd':
                    t = time(NULL);         /* get date */
                    tm = *localtime(&t);    
                    strftime(buffer, MAX_DIR, "%a %b %d", &tm);
                    break;
                case '@':
                    t = time(NULL); /* get time */
                    tm = *localtime(&t);
                    strftime(buffer, MAX_DIR, "%I:%M %p", &tm);
                    break;
                case 'A':
                    t = time(NULL); /* get time */
                    tm = *localtime(&t);
                    strftime(buffer, MAX_DIR, "%R", &tm);
                    break;
                case '0': case '1': case '2': case '3':  /* colors */
                case '4': case '5': case '6': case '7':
                    sprintf(buffer, "%s", colors[data->prompt[i + 1] - '0']);
                    break;
                default:
                    sprintf(buffer, "%c", data->prompt[i]);
                    i--;
                }
                i++;
            }
            else
                sprintf(buffer, "%c", data->prompt[i]);
            strcat(prompt, buffer);
        }
        printf("%s", prompt);
        fflush(stdout);
    }
}


void get_file_data(char *filename, char *buffer)
{
    FILE *f;
    char buf[BUFSIZ];
    size_t size;
    char *ptr;
    int i;
    
    f=fopen(filename,"rt");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(buffer, 1, size, f);
    for (i = 0; i < size; i++)
        if(buffer[i] == '\n')
            buffer[i] = ' ';
    buffer[size] = 0;
    while(isspace(buffer[strlen(buffer) -1]))
        buffer[strlen(buffer) -1] = 0;
}

/* expands the signs in the command line */
int expand_command_line(char *str, shell_data_t *data)
{
    int i, j;
    int state;
    int start;
    char cmd[BUFSIZ];
    char buffer[BUFSIZ];

    i = 0;
    state = 0;
    start = 0;
    while(i <= strlen(str))
    {
        switch(state)
        {
        case 0:
            if (str[i] == 0)
                state = 0;
            else if(str[i] == '\'')
                state = 1;
            else if(str[i] == '"')
                state = 2;
            else if(str[i] == '$')
                state = 3;
            break;
        case 1:
            if(str[i] == '\'')
                state = 0;
            break;
        case 2:
            if(str[i] == '"')
                state = 0;
            break;
        case 3:
            if (str[i] == '(')
            {
                start = i + 1;
                state = 4;
            }
            else
                state = 0;
            break;
        case 4:
            if(str[i] == '\'')
                state = 5;
            else if(str[i] == '"')
                state = 6;
            else if(str[i] == ')')  /* command */
            {
                strncpy(cmd, &str[start], i - start);
                cmd[i - start] = '\0';
          
                strcat(cmd, " > /tmp/simpleshell1"); 
                execute_command_line(cmd, data);
                get_file_data("/tmp/simpleshell1", buffer);
                unlink("/tmp/simpleshell1");
                strncpy(cmd, str, start - 2);
                cmd[start - 2] = 0;
                strcat(cmd, buffer);
                j = strlen(cmd);
                strcat(cmd, &str[i + 1]);
                strcpy(str, cmd);
                state = 0;
                i = j;
            }
            break;
        case 5:
            if(str[i] == '\'')
                state = 4;
            break;
        case 6:
            if(str[i] == '"')
                state = 4;
            break;
        }
        i++;
    }
    if (state) /* open quotes, invalid command */
    {
        fprintf(stderr, "Error: invalid command line.\n");
        return 0;
    }
    else
        return 1;
}


/* signal handler to catch zombie processes */
void sigchld_hnd()
{
    waitpid(-1, NULL, WNOHANG);
}

int main(int argc, char **argv)
{
    shell_data_t data;
    int quit;
    char buffer[MAX_LINE];
    char *line;
    
    data.show_prompt = 1;       /* enable prompt by default */
    strcpy(data.prompt, "> ");  /* set default prompt */ 
    getcwd(data.old_dir, MAX_DIR); /* set current directory as the old one */
    
    if (argc == 2) /* if the user provided options */
    {
        if (!strcmp(argv[1], "-t"))
            data.show_prompt = 0;       /* disable prompt */
        else
        {
            printf("Invalid option!\n");
            printf("Usage:\n\t%s [-t]\n", argv[0]);
            return 1;
        }
    }
    else if (argc > 2)
    {
        printf("Invalid number of arguments!\n");
        printf("Usage:\n\t%s [-t]\n", argv[0]);
        return 1;
    }
    /* install child signal handler */
    if (signal(SIGCHLD, sigchld_hnd) == SIG_ERR)
    {
        fprintf(stderr,"Error: unable to install SIGCHLD handler\n");
        exit(1);
    } 

    data.quit = 0;
    while(!data.quit)
    {
        print_prompt(&data);
        if(fgets(buffer, MAX_LINE, stdin) != NULL) /* read a line from stdin */
        {
            line = trim_spaces(buffer); /* remove spaces at the start and end of the string */
            if (line[0] != 0)
            {
                if (expand_command_line(line, &data))
                    execute_command_line(line, &data);
            }
        }
        else
            data.quit = 1;   /* if there was an error reading the input, quit */
    }
    printf("%s", colors[RESET]);    
    return 0;
}
