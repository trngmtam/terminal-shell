#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>


#define MAX_PIPES    16
#define DELIMITERS   " \t\r\n"


// Command: holds a parsed command with its arguments and redirects
typedef struct {
    char *args[MAX_ARGS];   // argument list, NULL-terminated
    int   argc;             // number of arguments
    char *in_file;          // redirect stdin  (<)
    char *out_file;         // redirect stdout (>)
    int   append;           // 1 = append (>>), 0 = truncate (>)
} Command;


// Part 1: Tokenizer

// Split `input` on whitespace and fill `args`.
// Returns the number of tokens found.
int tokenize(char *input, char **args)
{
    int count = 0;
    char *token = strtok(input, DELIMITERS);
    while (token != NULL && count < MAX_ARGS - 1) {
        args[count++] = token;
        token = strtok(NULL, DELIMITERS);
    }
    args[count] = NULL;
    return count;
}

// Build a Command from a raw token list, stripping redirect
// operators (>, >>, <) and recording their target file names.
static void parse_command(char **tokens, int ntok, Command *cmd)
{
    memset(cmd, 0, sizeof(Command));
    int j = 0;

    for (int i = 0; i < ntok; i++) {
        if (strcmp(tokens[i], ">") == 0 && i + 1 < ntok) {
            cmd->out_file = tokens[++i];
            cmd->append   = 0;
        } else if (strcmp(tokens[i], ">>") == 0 && i + 1 < ntok) {
            cmd->out_file = tokens[++i];
            cmd->append   = 1;
        } else if (strcmp(tokens[i], "<") == 0 && i + 1 < ntok) {
            cmd->in_file  = tokens[++i];
        } else {
            cmd->args[j++] = tokens[i];
        }
    }
    cmd->args[j] = NULL;
    cmd->argc    = j;
}


// Part 2: Built-in commands

// Executes the command if it is a built-in, then returns 1.
// Returns 0 if the command is not a built-in and should be run externally.
int handle_builtin(char **args)
{
    if (args[0] == NULL) return 1;

    // cd — change working directory
    if (strcmp(args[0], "cd") == 0) {
        const char *path = args[1] ? args[1] : getenv("HOME");
        if (path == NULL) path = "/";
        if (chdir(path) != 0)
            perror("cd");
        return 1;
    }

    // pwd — print current working directory
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_INPUT];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            printf("%s\n", cwd);
        else
            perror("pwd");
        return 1;
    }

    // exit — quit the shell
    if (strcmp(args[0], "exit") == 0) {
        printf("Goodbye!\n");
        exit(0);
    }

    // help — list available built-in commands
    if (strcmp(args[0], "help") == 0) {
        printf("myshell — available built-in commands:\n");
        printf("  cd [dir]   change working directory\n");
        printf("  pwd        print working directory\n");
        printf("  exit       quit the shell\n");
        printf("  help       show this help message\n");
        printf("\nOther Unix commands are run via fork/exec.\n");
        printf("Supports:  cmd | cmd   (pipe)\n");
        printf("           cmd > file  (redirect stdout, truncate)\n");
        printf("           cmd >> file (redirect stdout, append)\n");
        printf("           cmd < file  (redirect stdin)\n");
        return 1;
    }

    return 0; // not a built-in
}


// Part 3: Fork / Exec (single command, with optional redirect)

// Called inside the child process to wire up file descriptors
// before exec. Handles < (stdin) and > / >> (stdout).
static void apply_redirects(Command *cmd)
{
    if (cmd->in_file) {
        int fd = open(cmd->in_file, O_RDONLY);
        if (fd < 0) { perror(cmd->in_file); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->out_file) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->out_file, flags, 0644);
        if (fd < 0) { perror(cmd->out_file); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

// Fork and exec one Command. Returns the child's exit status.
static int execute_single(Command *cmd)
{
    // Built-ins must run in the parent process, not a child
    if (handle_builtin(cmd->args)) return 0;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // Child process: apply redirects then exec the command
        apply_redirects(cmd);
        execvp(cmd->args[0], cmd->args);
        // execvp only returns if it failed
        fprintf(stderr, "myshell: %s: command not found\n", cmd->args[0]);
        exit(127);
    }

    // Parent process: wait for the child to finish
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0)
            fprintf(stderr, "[exit code: %d]\n", code);
        return code;
    }
    return -1;
}


// Part 4: Pipeline (one or more commands joined by |)

// Given an array of Commands of length n, wire them together with pipes.
// The first command may have a < redirect; the last may have > or >>.
static void execute_pipeline(Command *cmds, int n)
{
    // Single command — no pipe needed
    if (n == 1) {
        execute_single(&cmds[0]);
        return;
    }

    // Strategy:
    // - Track the read-end of the previous pipe (prev_read).
    // - For each stage, create a new pipe (except after the last command).
    // - Each child inherits the correct fds and closes everything else.

    int   prev_read = -1;   // read-end of pipe from the previous stage
    pid_t pids[MAX_PIPES];

    for (int i = 0; i < n; i++) {
        int pipefd[2] = {-1, -1};

        // Create a pipe between stage i and stage i+1 (skip after last stage)
        if (i < n - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                return;
            }
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }

        if (pid == 0) {
            // Child process for stage i

            // Connect the previous pipe's read-end to stdin (skip for first command)
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }

            // Connect our new pipe's write-end to stdout (skip for last command)
            if (i < n - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            // Apply file redirects (< on first stage, > / >> on last stage)
            apply_redirects(&cmds[i]);

            execvp(cmds[i].args[0], cmds[i].args);
            fprintf(stderr, "myshell: %s: command not found\n", cmds[i].args[0]);
            exit(127);
        }

        // Parent process bookkeeping
        pids[i] = pid;

        // Close the previous read-end now that the child has inherited it
        if (prev_read != -1) close(prev_read);

        // Pass the new read-end forward to the next iteration
        if (i < n - 1) {
            close(pipefd[1]);       // parent never writes into this pipe
            prev_read = pipefd[0];
        }
    }

    // Wait for all child processes to finish
    for (int i = 0; i < n; i++)
        waitpid(pids[i], NULL, 0);
}


// Part 5: execute_command — public entry point for main.c and runner.c

// Parse the flat args[] array (which may contain '|' tokens as pipeline
// separators), build Command structs, and execute the resulting pipeline.
// Returns a CmdResult with exit_code and signal_num populated.
CmdResult execute_command(char **args, int line_num)
{
    CmdResult res;
    memset(&res, 0, sizeof(res));
    res.line_num = line_num;
    if (args[0])
        strncpy(res.cmd_name, args[0], sizeof(res.cmd_name) - 1);

    // Split args on '|' tokens to find pipeline stages
    Command cmds[MAX_PIPES];
    int n = 0, start = 0;

    for (int i = 0; ; i++) {
        if (args[i] == NULL || strcmp(args[i], "|") == 0) {
            if (i > start) {
                parse_command(args + start, i - start, &cmds[n]);
                if (cmds[n].argc > 0) n++;
            } else if (i > 0) {
                fprintf(stderr, "myshell: empty command in pipeline\n");
                return res;
            }
            start = i + 1;
        }
        if (args[i] == NULL) break;
        if (n >= MAX_PIPES)  break;
    }

    if (n == 0) return res;

    // Built-in: run in the parent process
    if (n == 1 && handle_builtin(cmds[0].args)) return res;

    if (n == 1) {
        // Single external command: fork and capture full waitpid status
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); res.exit_code = 1; return res; }

        if (pid == 0) {
            apply_redirects(&cmds[0]);
            execvp(cmds[0].args[0], cmds[0].args);
            fprintf(stderr, "myshell: %s: command not found\n", cmds[0].args[0]);
            exit(127);
        }

        // Publish the child PID so signal handlers can kill it
        g_current_child = pid;
        g_timeout_child = pid;

        int status;
        waitpid(pid, &status, 0);

        g_current_child = -1;
        g_timeout_child = -1;

        check_and_report(cmds[0].args[0], status, line_num);

        if (WIFEXITED(status))   res.exit_code  = WEXITSTATUS(status);
        if (WIFSIGNALED(status)) { res.signal_num = WTERMSIG(status); res.exit_code = 1; }
    } else {
        // Pipeline: execute and use the last stage's exit status
        execute_pipeline(cmds, n);
    }

    return res;
}
