/*
 * myshell.c — A simple Unix shell implementation
 * Member A: REPL + Tokenizer + Fork/Exec + Pipe + Redirect + Builtins
 *
 * Compile: gcc -o shell shell.c -Wall
 * Run:     ./shell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

/* ─── Constants ─────────────────────────────────────────────────── */
#define MAX_INPUT    1024
#define MAX_ARGS     64
#define MAX_PIPES    16
#define DELIMITERS   " \t\r\n"

/* ─── Struct: parsed command ─────────────────────────────────────── */
typedef struct {
    char *args[MAX_ARGS];   /* argument list, NULL-terminated        */
    int   argc;             /* number of arguments                   */
    char *in_file;          /* redirect stdin  (<)                   */
    char *out_file;         /* redirect stdout (>)                   */
    int   append;           /* 1 = append (>>), 0 = truncate (>)     */
} Command;

/* ═══════════════════════════════════════════════════════════════════
 *  TASK 2 — Tokenizer
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * tokenize_raw:
 *   Split `input` on whitespace and fill `args`.
 *   Returns number of tokens found.
 */
static int tokenize_raw(char *input, char **args)
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

/*
 * parse_command:
 *   Build a Command from a raw token list, stripping redirect
 *   operators (>, >>, <) and recording their file names.
 */
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

/* ═══════════════════════════════════════════════════════════════════
 *  TASK 6 — Built-in commands
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * handle_builtin:
 *   Returns 1 and executes the command if it is a built-in.
 *   Returns 0 if the command should be dispatched externally.
 */
static int handle_builtin(char **args)
{
    if (args[0] == NULL) return 1;

    /* ── cd ── */
    if (strcmp(args[0], "cd") == 0) {
        const char *path = args[1] ? args[1] : getenv("HOME");
        if (path == NULL) path = "/";
        if (chdir(path) != 0)
            perror("cd");
        return 1;
    }

    /* ── pwd ── */
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_INPUT];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            printf("%s\n", cwd);
        else
            perror("pwd");
        return 1;
    }

    /* ── exit ── */
    if (strcmp(args[0], "exit") == 0) {
        printf("Goodbye!\n");
        exit(0);
    }

    /* ── help ── */
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

    return 0; /* not a built-in */
}

/* ═══════════════════════════════════════════════════════════════════
 *  TASK 3 — Fork / Exec  (single command, with optional redirect)
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * apply_redirects:
 *   Called inside the child process to wire up file descriptors.
 */
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

/*
 * execute_single:
 *   Fork and exec one Command.  Returns child's exit status.
 */
static int execute_single(Command *cmd)
{
    /* Built-ins must run in the parent process */
    if (handle_builtin(cmd->args)) return 0;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ── child ── */
        apply_redirects(cmd);
        execvp(cmd->args[0], cmd->args);
        /* execvp only returns on error */
        fprintf(stderr, "myshell: %s: command not found\n", cmd->args[0]);
        exit(127);
    }

    /* ── parent: wait for child ── */
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

/* ═══════════════════════════════════════════════════════════════════
 *  TASK 4 — Pipeline  (one or more commands joined by |)
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * execute_pipeline:
 *   Given an array of Commands of length n, wire them up with pipes.
 *   The first command may still have < redirect; the last may have > / >>.
 */
static void execute_pipeline(Command *cmds, int n)
{
    if (n == 1) {
        execute_single(&cmds[0]);
        return;
    }

    /*
     * Strategy:
     *  - We keep track of the "read end" of the previous pipe.
     *  - For each command we create a new pipe (except after the last).
     *  - Each child inherits the right fds and closes everything else.
     */

    int   prev_read = -1;   /* read-end of pipe from previous stage   */
    pid_t pids[MAX_PIPES];

    for (int i = 0; i < n; i++) {
        int pipefd[2] = {-1, -1};

        /* create pipe between stage i and i+1 (not needed after last) */
        if (i < n - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                return;
            }
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }

        if (pid == 0) {
            /* ── child i ── */

            /* Connect previous pipe to stdin (unless first command) */
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }

            /* Connect write-end of our new pipe to stdout
               (unless last command)                                  */
            if (i < n - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            /* File redirects (only meaningful on first / last stage) */
            apply_redirects(&cmds[i]);

            execvp(cmds[i].args[0], cmds[i].args);
            fprintf(stderr, "myshell: %s: command not found\n",
                    cmds[i].args[0]);
            exit(127);
        }

        /* ── parent ── */
        pids[i] = pid;

        /* Close the previous read-end now that it's been inherited */
        if (prev_read != -1) close(prev_read);

        /* The new read-end becomes prev_read for next iteration     */
        if (i < n - 1) {
            close(pipefd[1]);          /* parent doesn't write here  */
            prev_read = pipefd[0];
        }
    }

    /* Wait for all children */
    for (int i = 0; i < n; i++)
        waitpid(pids[i], NULL, 0);
}

/* ═══════════════════════════════════════════════════════════════════
 *  High-level line dispatcher
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * run_line:
 *   Parse and execute one line of user input.
 *   Splits on '|' to find pipeline stages, then parses each stage's
 *   tokens into a Command (handling <, >, >>).
 */
static void run_line(char *line)
{
    /* --- Split line on '|' to get segments --- */
    char *segments[MAX_PIPES];
    int   nseg = 0;

    char *seg = strtok(line, "|");
    while (seg != NULL && nseg < MAX_PIPES) {
        segments[nseg++] = seg;
        seg = strtok(NULL, "|");
    }

    Command cmds[MAX_PIPES];

    for (int i = 0; i < nseg; i++) {
        char *tokens[MAX_ARGS];
        int ntok = tokenize_raw(segments[i], tokens);
        if (ntok == 0) {
            fprintf(stderr, "myshell: empty command in pipeline\n");
            return;
        }
        parse_command(tokens, ntok, &cmds[i]);
        if (cmds[i].argc == 0) {
            fprintf(stderr, "myshell: empty command in pipeline\n");
            return;
        }
    }

    execute_pipeline(cmds, nseg);
}

/* ═══════════════════════════════════════════════════════════════════
 *  TASK 1 — REPL main loop
 * ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
    char input[MAX_INPUT];

    printf("myshell — type 'help' for built-in commands, Ctrl+D to quit\n");

    while (1) {
        /* Print prompt */
        printf("myshell> ");
        fflush(stdout);

        /* Read a line */
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            /* Ctrl+D (EOF) */
            printf("\n");
            break;
        }

        /* Strip trailing newline */
        input[strcspn(input, "\n")] = '\0';

        /* Skip blank lines */
        if (strlen(input) == 0) continue;

        /* Make a working copy because strtok mutates the string */
        char copy[MAX_INPUT];
        strncpy(copy, input, MAX_INPUT - 1);
        copy[MAX_INPUT - 1] = '\0';

        run_line(copy);
    }

    return 0;
}