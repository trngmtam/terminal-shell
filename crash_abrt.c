/*
 * crash_abrt.c — Demo program that triggers SIGABRT (abort)
 * Used in presentations to demonstrate error detection.
 *
 * Compile: gcc -o crash_abrt crash_abrt.c
 * Run    : ./crash_abrt
 */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("crash_abrt starting...\n");
    printf("Calling abort() now...\n");
    fflush(stdout);  /* Ensure the line above is printed before the crash */

    abort();  /* <-- This triggers SIGABRT */

    printf("This line is NEVER printed\n");
    return 0;
}
