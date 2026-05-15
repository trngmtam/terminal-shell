/*
 * crash_ptr.c — Demo program that triggers SIGSEGV (null pointer dereference)
 * Used in presentations to demonstrate error detection.
 *
 * Compile: gcc -o crash_ptr crash_ptr.c
 * Run    : ./crash_ptr
 */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("crash_ptr starting...\n");
    printf("Preparing to read memory at NULL...\n");

    int *ptr = NULL;   /* Null pointer -- does not point to valid memory */

    printf("Reading value at NULL: ");
    fflush(stdout);    /* Ensure the line above is printed before the crash */

    printf("%d\n", *ptr);  /* <-- This line triggers SIGSEGV */

    printf("This line is NEVER printed\n");
    return 0;
}
