/*
 * test_error_handler.c — Standalone test program for Member C
 *
 * Forks and runs crash_div / crash_ptr, then calls check_and_report()
 * to verify the full error handling flow.
 *
 * Compile:
 *   gcc -o test_error_handler test_error_handler.c error_handler.c
 *
 * Run:
 *   ./test_error_handler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "shared.h"


static void run_and_check(const char *label, char *const argv[], int line_num)
{
    printf("\n--- %s ---\n", label);
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        /* Child process: execute command */
        execvp(argv[0], argv);
        /* If execvp fails */
        perror("execvp failed");
        exit(127);
    }

    /* Parent process: wait for child */
    int status = 0;
    waitpid(pid, &status, 0);

    /* Call Member C's function to handle the result */
    int ret = check_and_report(argv[0], status, line_num);
    if (ret == 0) {
        printf("  => Command succeeded.\n");
    } else {
        printf("  => Command failed (see log: %s)\n", "shell_errors.log");
    }
}

int main(void)
{
    printf("  TEST SUITE — Member C: Exception & Error Handler    \n");

    /* Test 1: Normal command (should succeed, exit code 0) */
    char *args_ls[] = { "ls", "-la", NULL };
    run_and_check("Test 1: Normal command (ls -la)", args_ls, 1);

    /* Test 2: Divide by zero — triggers SIGFPE */
    char *args_div[] = { "./crash_div", NULL };
    run_and_check("Test 2: Divide by zero", args_div, 2);

    /* Test 3: Null pointer — triggers SIGSEGV */
    char *args_ptr[] = { "./crash_ptr", NULL };
    run_and_check("Test 3: Null pointer", args_ptr, 3);

    /* Test 4: Command exits with non-zero exit code */
    char *args_false[] = { "false", NULL };   /* 'false' always returns exit code 1 */
    run_and_check("Test 4: Non-zero exit code (false)", args_false, 4);

    /* Test 5: Non-existent command (execvp will fail, exit 127) */
    char *args_unknown[] = { "./command_does_not_exist", NULL };
    run_and_check("Test 5: Non-existent command", args_unknown, 5);

    /* Print log summary */
    printf("  TAIL OF shell_errors.log:\n");
    system("tail -n 10 shell_errors.log");

    printf("  DONE. Shell is still running (did not exit)\n");

    return 0;
}
