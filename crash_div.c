/*
 * crash_div.c — Demo program that triggers SIGFPE (divide by zero)
 * Used in presentations to demonstrate error detection.
 *
 * Compile: gcc -o crash_div crash_div.c
 * Run    : ./crash_div
 */

#include <stdio.h>
#include <signal.h>

int main(void)
{
    printf("crash_div starting...\n");
    printf("Preparing division...\n");

    int a = 10;
    int b = 0;   /* Intentionally set b = 0 to trigger the error */

    printf("Computing %d / %d = ", a, b);
    fflush(stdout);  /* Ensure the line above is printed before the crash */

    (void)b;         /* suppress unused-variable warning */
    raise(SIGFPE);   /* ARM64 does not raise SIGFPE on integer divide-by-zero;
                        raise() guarantees the signal is delivered */
    printf("???\n"); /* never reached */
    printf("This line is NEVER printed\n");
    return 0;
}
