/*
 * runner.c — Read script file and run each command
 *
 * Includes 6 parts:
 *   Part 1: RunStats          — struct to track script execution results
 *   Part 2: sigint_handler()  — handle Ctrl+C, only kill the running command
 *   Part 3: sigalrm_handler() — handle timeout, kill commands that run too long
 *   Part 4: setup_signals()   — register Parts 2 and 3 with the OS
 *   Part 5: run_with_timeout()— wrap execute_command() with a countdown timer
 *   Part 6: run_script()      — read each line of a .sh file and execute it
 */
 
#include "shared.h"
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
 
 
// Part 1: RunStats — Execution result summary table
// Purpose: Track how many commands succeeded or failed, and store the list of
//          failed commands to print a final report
 
// Store at most 20 failed commands — avoids excess memory use when script has many errors
#define MAX_FAILED_CMDS 20
 
typedef struct {
    int total;   // Total commands executed (excludes blank lines and comments)
    int passed;  // Commands that succeeded (exit_code == 0)
    int failed;  // Commands that failed (exit_code != 0 or crashed)
 
    // 2D array storing the names of failed commands
    // failed_cmds[0] = "Line 3: ./buggy"
    // failed_cmds[1] = "Line 7: gcc main.c"
    char failed_cmds[MAX_FAILED_CMDS][MAX_INPUT];
} RunStats;
 
 
// Global variables — shared between signal handlers
 
// PID of the currently running command (-1 = no command running)
static volatile pid_t g_current_child = -1;
 
// PID of the command being monitored for timeout (-1 = none)
static volatile pid_t g_timeout_child = -1;
 
// Default timeout in seconds — main.c can override this via --timeout
int g_timeout_secs = 10;
 
 
// Part 2: sigint_handler() — Handle Ctrl+C
// Purpose: When the user presses Ctrl+C, kill only the running command instead
//          of exiting the shell — the shell stays alive
// How it works: The OS calls this function automatically on Ctrl+C
//               (registered in Part 4 - setup_signals)
static void sigint_handler(int sig) {
    (void)sig;
 
    if (g_current_child > 0) {
        // A command is running → forward SIGINT to that command
        kill(g_current_child, SIGINT);
 
        // Use write() instead of printf() because signal handlers must not call printf()
        // printf is not safe when interrupted mid-execution — it can deadlock
        const char msg[] = "\n[SIGINT sent to running command]\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1); // sizeof - 1 to exclude the '\0' terminator
    } else {
        // No command is running → remind the user how to exit
        const char msg[] = "\n(No command running. Press Ctrl+D or type exit to quit)\nmyshell> ";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
}
 
 
// Part 3: sigalrm_handler() — Handle Timeout
// Purpose: When a command runs longer than g_timeout_secs seconds,
//          the OS sends SIGALRM → this function kills that command
// Difference from Part 2: uses SIGKILL instead of SIGINT because:
//   SIGINT  = "please stop" → process can ignore it
//   SIGKILL = "stop now"    → process CANNOT ignore it, OS kills immediately
// How it works: alarm(N) in Part 5 sets a countdown of N seconds
//               After N seconds, OS sends SIGALRM → this function is called
static void sigalrm_handler(int sig) {
    (void)sig;
 
    if (g_timeout_child > 0) {
        // Command is still running after the deadline → kill it immediately
        kill(g_timeout_child, SIGKILL);
 
        // Build the message with the timeout duration filled in
        // Must use snprintf because we need to embed the number of seconds
        char msg[128];
        int len = snprintf(msg, sizeof(msg),
            "\n\033[31m[TIMEOUT] Command killed after %d seconds\033[0m\n",
            // \033[31m = red color, \033[0m = reset color
            g_timeout_secs);
 
        // len = actual length of msg after the number is filled in
        write(STDOUT_FILENO, msg, len);
    }
    // If g_timeout_child <= 0: command already finished before deadline → do nothing
}
 
 
// Part 4: setup_signals() — Register Signal Handlers
// Purpose: Tell the OS "which function to call on Ctrl+C,
//          and which function to call when the timer expires"
// Called once in main() before anything else runs
// After this call: Parts 2 and 3 activate automatically when signals arrive
void setup_signals(void) {
    // struct sigaction: detailed configuration for handling one signal
    struct sigaction sa;
 
    // --- Register Ctrl+C handler (SIGINT) ---
    memset(&sa, 0, sizeof(sa));       // zero out the struct before use
    sa.sa_handler = sigint_handler;   // function to call on Ctrl+C (Part 2)
    sigemptyset(&sa.sa_mask);         // don't block any other signals during handling
    sa.sa_flags = SA_RESTART;         // auto-restart system calls interrupted by this signal
    sigaction(SIGINT, &sa, NULL);     // register with OS: SIGINT → sigint_handler
 
    // --- Register Timeout handler (SIGALRM) ---
    memset(&sa, 0, sizeof(sa));       // reset struct to reuse for another signal
    sa.sa_handler = sigalrm_handler;  // function to call when timer expires (Part 3)
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);    // register with OS: SIGALRM → sigalrm_handler
 
    // --- Ignore SIGPIPE ---
    // SIGPIPE occurs when writing to a pipe with no reader
    // (e.g., the right side of a pipe already exited)
    // SIG_IGN = ignore completely → prevents the shell from crashing for this reason
    signal(SIGPIPE, SIG_IGN);
}
 
 
// Part 5: run_with_timeout() — Run a command with a timeout timer
// Purpose: Wrap execute_command() with a timeout safety layer
//          Start the timer before → run the command → cancel the timer after
// If the command finishes before the deadline: alarm(0) cancels the timer → normal exit
// If the command exceeds the deadline: SIGALRM triggers Part 3 → command is killed
static CmdResult run_with_timeout(char **args, int line_num) {
    // Set to 0 to signal "a command is about to run but PID is not yet known"
    // (because execute_command forks internally, we can't get the PID directly)
    g_timeout_child = 0;
 
    // Start countdown — after g_timeout_secs seconds, OS will send SIGALRM
    alarm(g_timeout_secs);
 
    // Run the actual command (function from shell.c) — may take several seconds
    CmdResult res = execute_command(args, line_num);
 
    // Command finished → cancel the timer immediately
    // alarm(0) = cancel any pending alarm, SIGALRM will no longer be sent
    alarm(0);
 
    // Reset to -1 to signal "no command needs monitoring anymore"
    g_timeout_child = -1;
 
    return res;
}
 
 
// Part 6: run_script() — Read and Execute a Script File
// Purpose: Open a .sh file, read line by line, skip comments and blank lines,
//          run each command, track statistics, print a final summary report
// Called by main() when the user runs: ./shell script.sh
void run_script(const char *filename, int stop_on_error) {
 
    // Open the script file for reading
    FILE *fp = fopen(filename, "r");
 
    if (fp != NULL) {
        // FD_CLOEXEC: automatically close this file descriptor when a child process forks
        // Prevents child processes from accidentally reading the script file via stdin
        int fd = fileno(fp);  // get the file descriptor number of fp
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
        //         ↑ set flag    ↑ get old flag   ↑ OR in FD_CLOEXEC to old flags
    }
 
    // Check if the file opened successfully
    if (fp == NULL) {
        fprintf(stderr, "\033[31mError: Cannot open file '%s'\033[0m\n", filename);
        exit(1);  // exit the program, cannot proceed
    }
 
    // Initialize the stats table — zero everything out
    RunStats stats;
    memset(&stats, 0, sizeof(stats));
 
    char raw_line[MAX_INPUT];  // buffer holding each line read from the file
    int  line_num = 0;         // line counter (includes blank lines and comments)
 
    printf("\033[1m===== Running script: %s =====\033[0m\n\n", filename);
 
    // Read one line at a time until end of file
    // fgets reads one line into raw_line, returns NULL at EOF
    while (fgets(raw_line, MAX_INPUT, fp) != NULL) {
        line_num++;  // increment even if this line won't be executed
 
        // Remove the trailing '\n' that fgets keeps
        // strcspn returns the position of the first '\n' in the string
        raw_line[strcspn(raw_line, "\n")] = '\0';
 
        // Skip leading spaces and tabs
        // Advance the line pointer forward to the first real character
        char *line = raw_line;
        while (*line == ' ' || *line == '\t') line++;
 
        // Skip blank lines (*line == '\0') and comment lines (*line == '#')
        if (*line == '\0' || *line == '#') continue;
 
        // Print the line number and command content (in gray)
        printf("\033[90m[Line %d]\033[0m %s\n", line_num, line);
 
        // Copy the command line to a new buffer BEFORE tokenizing
        // Because tokenize() uses strtok() which modifies the string in place by inserting '\0'
        // We need to keep the original string (line) to store in failed_cmds if the command fails
        char line_copy[MAX_INPUT];
        strncpy(line_copy, line, MAX_INPUT - 1);
        line_copy[MAX_INPUT - 1] = '\0';  // always ensure null terminator at end
 
        // Split the command string into an args[] array
        // "gcc -o main main.c" → ["gcc", "-o", "main", "main.c", NULL]
        char *args[MAX_ARGS];
        tokenize(line_copy, args);  // function from shell.c
 
        // Skip if no tokens were parsed (line was only whitespace)
        if (args[0] == NULL) continue;
 
        // Count this as one executed command
        stats.total++;
 
        // Run the command through Part 5 (with timeout timer)
        // Returns a CmdResult containing exit_code and signal_num
        CmdResult res = run_with_timeout(args, line_num);
 
        if (res.exit_code == 0 && res.signal_num == 0) {
            // Success: exited normally with exit code 0
            stats.passed++;
        } else {
            // Failure: non-zero exit code or killed by a signal
            stats.failed++;
 
            // Save the failed command info to the list (up to MAX_FAILED_CMDS)
            if (stats.failed <= MAX_FAILED_CMDS) {
                snprintf(stats.failed_cmds[stats.failed - 1],
                         MAX_INPUT - 1,
                         "Line %d: %.900s",  // %.900s limits command name to 900 chars
                         line_num, line);    // line = original string before tokenize modified it
            }
 
            // If the user enabled --stop: halt the script here
            if (stop_on_error) {
                printf("\033[31m[ABORT] Stopping script due to error at line %d\033[0m\n", line_num);
                break;  // exit the while loop, skip remaining lines
            }
        }
    }
 
    fclose(fp);  // close the file after reading is done or after break
 

    // Print final summary report
    printf("\n\033[1m---- SCRIPT EXECUTION RESULT -----\033[0m\n");
    printf("Total commands run  : %d\n",  stats.total);
    printf("\033[32mSucceeded (OK)      : %d\033[0m\n", stats.passed);  // green
    printf("\033[31mFailed    (FAILED)  : %d\033[0m\n", stats.failed);  // red
 
    // If there are failed commands → print the detailed list
    if (stats.failed > 0) {
        printf("\nFailed commands:\n");
 
        // Only print up to MAX_FAILED_CMDS lines to avoid an overly long report
        int show = stats.failed < MAX_FAILED_CMDS ? stats.failed : MAX_FAILED_CMDS;
        for (int i = 0; i < show; i++) {
            printf("  \033[31m✗\033[0m %s\n", stats.failed_cmds[i]);
            //       ↑ red X mark  ↑ name of the failed command
        }
    }
    printf("-----------\n");
}
