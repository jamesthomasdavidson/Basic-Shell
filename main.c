#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#define bool int
#define TRUE 1
#define FALSE 0

#define MAX_JOBS 5 
#define MAX_INPUT_PARAMETERS 10
#define MAX_INPUT_SIZE 256
#define MAX_DIR_SIZE 256

/* Global variables used by the shell. */
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;
static char* current_directory;  
int num_bg_jobs;                              

/* A job as defined by A1 specs.  */
typedef struct job{    
    char **args;                /* command line, used for messages */
    char* dir;                  /* the dir at which the file was executed from */
    pid_t pid;                  /* process ID */
    pid_t pgid;                 /* process group ID */
    int status;                 /* 0 stopped, 1 if running */
    int job_num;                /* the current job number */
    bool foreground;            /* true if process is to be run in the foreground */
}job;

job *bg_list[MAX_JOBS];         /* background job list */

/* Function declarations. */
char* display_shell(char* str);
void assign_job_num(job * j);

/* Make sure the shell is running interactively as the foreground job before proceeding. */
void init_shell ()
{
    /* See if we are running interactively.  */
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty (shell_terminal);

    if (shell_is_interactive)
    {
    /* Loop until we are in the foreground.  */
        while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
            kill (- shell_pgid, SIGTTIN);

        /* Ignore interactive and job-control signals.  */
        signal (SIGINT, SIG_IGN);
        signal (SIGQUIT, SIG_IGN);
        signal (SIGTSTP, SIG_IGN);
        signal (SIGTTIN, SIG_IGN);
        signal (SIGTTOU, SIG_IGN);
        signal (SIGCHLD, SIG_IGN);

        /* Put ourselves in our own process group.  */
        shell_pgid = getpid ();
        if (setpgid (shell_pgid, shell_pgid) < 0)
        {
            perror ("Couldn't put the shell in its own process group");
            exit (1);
        }

        /* Grab control of the terminal.  */
        tcsetpgrp (shell_terminal, shell_pgid);

        /* Save default terminal attributes for shell.  */
        tcgetattr (shell_terminal, &shell_tmodes);
    }
}

/* Executes the job by calling fork() and then execvp(). */
void execute_job(job* j)
{
    if(!j->foreground)
    {
        bg_list[num_bg_jobs++] = j;
    }
    j->status = TRUE;
    pid_t pid = fork();
    pid_t pgid = j->pgid;

    if(!pid)
    {
        pid_t cpid;
        if (shell_is_interactive){
            /* Put the process into the process group and give the process group
            the terminal, if appropriate.
            This has to be done both by the shell and in the individual
            child processes because of potential race conditions.  */
            cpid = getpid ();
            if (pgid == 0) pgid = cpid;
            setpgid (cpid, pgid);
            if (j->foreground){
                tcsetpgrp (shell_terminal, pgid);
            }

            /* Set the handling for job control signals back to the default.  */
            signal (SIGINT, SIG_DFL);
            signal (SIGQUIT, SIG_DFL);
            signal (SIGTSTP, SIG_DFL);
            signal (SIGTTIN, SIG_DFL);
            signal (SIGTTOU, SIG_DFL);
            signal (SIGCHLD, SIG_DFL);  
        }

        /* Exec the new process.  Make sure we exit.  */
        execvp (j->args[0], j->args);
        perror ("execvp");
        exit (1);     
    }
    else if(pid < 0)
    {
        /* The fork failed. */
        perror ("fork");
        exit (1);
    }
    else
    {
        /* This is the parent process. */
        j->pid = pid;
        j->status = -1;
        if (shell_is_interactive)
        {
            if (!j->pgid)
            {
                j->pgid = pid;
            }
            setpgid (pid, j->pgid);
        }
    }
    if (j->foreground)
    {
        wait(NULL);
    }
}

/* Allocates memory and populates the job struct, taking in args and a boolean specifying the foreground/background. */
job* build_job(char** args, bool fg)
{
    job* j = malloc(sizeof(job));

    /* Copy the args to a new char** without the bg parameter if "bg"  exists. */
    if(!strcmp(args[0],"bg"))
    {
        char** bg_args = malloc(sizeof(char*)*MAX_INPUT_PARAMETERS);
        int i; int p = 0;
        free(args[0]);

        for(i = 1; args[i] && i < MAX_INPUT_PARAMETERS; i++)
        {
            bg_args[p] = malloc(sizeof(char)*(strlen(args[i]))+1);
            strcpy(bg_args[p++],args[i]);
            free(args[i]);
        }
        bg_args[p] = NULL;
        free(args);
        j->args = bg_args;
    }
    else
    {
        j->args = args;
    }
    /* Assign the job a copy of the directory in which it was executed from. */
    j->dir = malloc(sizeof(char)*strlen(current_directory)+1);
    strcpy(j->dir, current_directory);

    /* Populate integer fields. */
    j->foreground = fg;
    j->pgid = 0;
    assign_job_num(j);
    
    return j;
}

/* Frees the job from memory. */
void free_job(job* j)
{
    int i;

    for(i = 0; j->args[i]; i++)
    {
        free(j->args[i]);
    }
    if(j->args)
        free(j->args);
    if(j->dir)
        free(j->dir);
    free(j);
}

/* Takes in a reference to a job, removes and frees said job, then sorts the list and decrements the background job count.  */
void remove_job(job *j)
{
    int i, p;
    bool prev = FALSE;

    for(i = 0; i < num_bg_jobs; i++)
    {
        if(j == bg_list[i]){
            bg_list[i] = NULL;
            break;
        }
    }
    for(i = MAX_JOBS-1; i >= 0; i--)
    {
        if(bg_list[i])
        {
            prev = TRUE;
            continue;
        }
        else if(!bg_list[i] && prev)
        {
            for(p = i; p < MAX_JOBS-1; p++)
            {
                bg_list[p] = bg_list[p+1];
            }
        }
    }
    num_bg_jobs--;
    free_job(j);
}

/* Assigns the job a job ID. */
void assign_job_num(job * j){
    int highest = 0;
    int i;

    for (i = 0; i < num_bg_jobs; i++)
    {
        if(bg_list[i]->job_num >= highest)
        {
            highest = bg_list[i]->job_num + 1;
        }
    }
    j->job_num = highest;
}

/* Kills all background jobs. */
void kill_all_jobs()
{
    job* j;

    while((j = bg_list[0]) != NULL)
    {
        kill(j->pid, SIGKILL);
        remove_job(j);
    }
}

/* Used to check input parameters for shell. */
bool job_exists(int job_num)
{
    int i;
    for (i = 0; i < num_bg_jobs; i++)
    {
        if(job_num == bg_list[i]->job_num)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* Returns the index of a job in bg_list via its process ID. Returns -1 if the specified pid does not exist in the background job list */
int get_job_num_by_pid(pid_t pid)
{
    int i;

    for(i = 0; i < num_bg_jobs; i++)
    {
        if(pid == bg_list[i]->pid)
        {
            return bg_list[i]->job_num;
        }
    }
    return -1;
}

/* Returns a reference to a job via its job number. */
job* get_job_by_num(int job_num)
{
    if(!job_exists(job_num))
    {
        char t[MAX_INPUT_SIZE];
        sprintf(t, "Job %d doesnt exist", job_num);
        display_shell(t);
    }
    else
    {
        int i;

        for(i = 0; i < num_bg_jobs; i++)
        {
            if(job_num == bg_list[i]->job_num)
            {
                return bg_list[i];
            }
        }
    }   
    return NULL;
}

/* Checks to see if any child/background process's have terminated, if so removes them from the background job list and notifies the user. */
void update_list()
{
    pid_t cpid;
    int status;

    while ((cpid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int job_num = get_job_num_by_pid(cpid);
        if(job_num == -1)
        {
            return;
        }
        job* j = get_job_by_num(job_num);
        printf("Job %d finished\n", job_num);
        remove_job(j);
    }
}

/* Tokenizes the string "buffer" and allocates a set of char*'s to copy the tokenized buffer into. */
void format_args(char** args, char* buffer)
{
    char* buffer_pointer;
    int i = 0;

    for(buffer_pointer = strtok(buffer, " \n"); buffer_pointer; buffer_pointer = strtok(NULL, " \n"))
    {
        args[i] = malloc(sizeof(char)*strlen(buffer_pointer)+1);
        strcpy(args[i++], buffer_pointer);
    }
    args[i] = NULL;
}

/* Returns the appropiate char for the operating status of a job */
char status(int status)
{
    if (status)
    {
        return 'r';
    }
    else
    {
        return 's';
    }
}

/* Displays the shell, and returns user input as a pointer. */
char* display_shell(char* str)
{
    if(!str)
    {
        char t[MAX_INPUT_SIZE];
        sprintf(t, "shell: %s > ",getcwd(current_directory, MAX_DIR_SIZE));
        return readline(t);    
    }
    else
    {   
        printf("shell: %s > %s\n", getcwd(current_directory, MAX_DIR_SIZE), str);
        return NULL; 
    }   
}

/* Performs all the string comparisons for shell functionality. */
void run_commands(char** args)
{
    int i;

    if (strcmp("exit", args[0]) == 0){
        kill_all_jobs();
        exit(EXIT_SUCCESS);
    }
    /* Change Directory. */
    if (strcmp("cd", args[0]) == 0)
    {
        if ((args[1] == NULL) || (strcmp(args[1], "~") == 0))
        {
            chdir(getenv("HOME"));
        }else if(chdir(args[1]) == -1) {
            char er[MAX_DIR_SIZE*2];
            strcpy(er, "No such directory ");
            strcat(er, args[1]);
            display_shell(er);
        }
    }
    /* Run process in the background. */
    else if(strcmp("bg", args[0]) == 0)
    {
        if(num_bg_jobs >= MAX_JOBS){
            display_shell("Maximum amount of background jobs already active");
            return;
        }
        job* j = build_job(args, FALSE);
        execute_job(j);
    }
    /* Lists all jobs in background list. */
    else if(strcmp("bglist", args[0]) == 0)
    {
        for(i = 0; i < num_bg_jobs; i++)
        {
            char* t = bg_list[i]->args[0];

            if(t[0] == '.' && t[1] == '/')
            {
                t = t + 2;
            }
            printf("%d[%c]:  %s/%s\n", bg_list[i]->job_num, status(bg_list[i]->status), bg_list[i]->dir, t);
        }        
        printf("Total background jobs: %d\n", num_bg_jobs);
    }
    /* Sends SIGKILL signal to the specified job number. */
    else if(strcmp("bgkill", args[0]) == 0)
    {
        if(!args[1])
        {
            display_shell("No job specified");
            return;
        }
        int n = atoi((args[1]));
        job* j = get_job_by_num(n);

        if (job_exists(n))
        {
            if(j->pid){
                kill(j->pid, SIGKILL);
                remove_job(j);
            }
        }
        else
        {
            char t[MAX_INPUT_SIZE];
            sprintf(t, "Job %d doesnt exist", n);
            display_shell(t);
        }
    }
    /* Sends stop signal to the specified job. */
    else if(strcmp("stop", args[0]) == 0)
    {
        if(!args[1])
        {
            display_shell("No job specified");
            return;
        }        
        int n = atoi(args[1]);

        if(job_exists(n))
        {
            job * j = get_job_by_num(n);
            j->status = FALSE;
            kill(j->pid,SIGTSTP);
        }
        else
        {
            char t[MAX_INPUT_SIZE];
            sprintf(t, "Job %d doesnt exist", n);
            display_shell(t);
        }
    }
    /* Sends start signal to the specified job. */
    else if(strcmp("start", args[0]) == 0)
    {
        if(!args[1])
        {
            display_shell("No job specified");
            return;
        }
        int n = atoi(args[1]);

        if(job_exists(n)){
            job * j = get_job_by_num(n);
            j->status = TRUE;
            kill(j->pid,SIGCONT);
        }else{
            char t[MAX_INPUT_SIZE];;
            sprintf(t, "Job %d doesnt exist", n);
            display_shell(t);
        }
    }
    /* args specify a command, run execvp by calling execute_job().  Job will be run in the foreground. */
    else
    {
        job* j = build_job(args, TRUE);
        execute_job(j);
        free_job(j);
    }
}

int main(int argc, char* argv[])
{
    current_directory = malloc(sizeof(char)*MAX_DIR_SIZE);
    num_bg_jobs = 0;

    while(TRUE){
        char *buffer = display_shell(NULL);
        if(buffer[0] == '\0'){
            update_list();
            free(buffer);
            continue;
        }else{
            char **args;
            args = malloc(sizeof(char*)*MAX_INPUT_PARAMETERS);
            format_args(args, buffer);
            run_commands(args);
        }
        free(buffer);
        update_list();
    }
    printf("\n");
    free(current_directory);
	return 0;
}