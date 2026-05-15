/*
 * error_handler.c — Member C: Exception & Error Handler
 *
 * Responsibilities:
 *  1. Decode signals into named error descriptions
 *  2. Print colored error messages to the terminal
 *  3. Write log entries to shell_errors.log with timestamps
 *  4. check_and_report() is the shared function used by Member A and B
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RESET   "\033[0m"


typedef struct {
    int         signal_num;
    const char *name;
    const char *description;
} SignalInfo;

static SignalInfo signal_table[] = {
    { SIGSEGV,  "SIGSEGV",  "Segmentation fault (invalid memory access)"           },
    { SIGFPE,   "SIGFPE",   "Floating point exception (divide by zero, overflow)"  },
    { SIGILL,   "SIGILL",   "Illegal instruction (invalid CPU instruction)"        },
    { SIGBUS,   "SIGBUS",   "Bus error (memory alignment error)"                   },
    { SIGABRT,  "SIGABRT",  "Abort (program called abort())"                       },
    { SIGKILL,  "SIGKILL",  "Killed (terminated by system or timeout)"             },
    { SIGTERM,  "SIGTERM",  "Terminated (graceful termination requested)"          },
    { SIGPIPE,  "SIGPIPE",  "Broken pipe (write to pipe with no reader)"           },
    { SIGINT,   "SIGINT",   "Interrupted (user pressed Ctrl+C)"                    },
    { SIGALRM,  "SIGALRM",  "Alarm (timeout set by shell)"                        },
    { SIGHUP,   "SIGHUP",   "Hangup (terminal closed)"                            },
    { -1,       NULL,       NULL                                                    }
};

const char *get_signal_name(int sig) {
    for (int i = 0; signal_table[i].name != NULL; i++) {
        if (signal_table[i].signal_num == sig)
            return signal_table[i].name;
    }
    return "UNKNOWN";
}

const char *get_signal_description(int sig) {
    for (int i = 0; signal_table[i].description != NULL; i++) {
        if (signal_table[i].signal_num == sig)
            return signal_table[i].description;
    }
    return "Unknown signal";
}

void log_error(const char *cmd,
               const char *sig_name,
               const char *sig_desc,
               int         line_num) {
    FILE *fp = fopen(LOG_FILE, "a"); // "a" = append, do not overwrite
    if (fp == NULL) {
        // Don't crash the shell just because logging failed
        perror("log_error: fopen");
        return;
    }

    // Get current timestamp
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "[%s] CRASH | cmd='%s' | line=%d | signal=%s | %s\n",
            time_str, cmd, line_num, sig_name, sig_desc);

    fclose(fp);
}

static void report_crash(const char *cmd_name, int sig, int line_num) {
    const char *sig_name = get_signal_name(sig);
    const char *sig_desc = get_signal_description(sig);

    fprintf(stderr,
        COLOR_RED COLOR_BOLD
        "  Command: %s\n"
        "  Line   : %d\n"
        "  Signal : %s (no. %d)\n"
        "  Reason : %s\n"
        COLOR_RESET "\n",
        cmd_name, line_num, sig_name, sig, sig_desc);

    // Write to log file
    log_error(cmd_name, sig_name, sig_desc, line_num);
}

static void report_exit_error(const char *cmd_name, int code, int line_num) {
    // exit code 127 = command not found, already reported by execvp
    if (code == 127) return;

    fprintf(stderr,
        COLOR_YELLOW
        "[WARN] '%s' exited with exit code %d (line %d)"
        COLOR_RESET "\n",
        cmd_name, code, line_num);

    log_error(cmd_name, "EXIT_ERROR", "Non-zero exit code", line_num);
}

int check_and_report(const char *cmd_name, int status, int line_num) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            return 0; // Success — print nothing
        }
        report_exit_error(cmd_name, code, line_num);
        return -1;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        report_crash(cmd_name, sig, line_num);
        return -1;
    }

    if (WIFSTOPPED(status)) {
        int sig = WSTOPSIG(status);
        fprintf(stderr,
            COLOR_CYAN "[INFO] '%s' was stopped (signal %d)\n" COLOR_RESET,
            cmd_name, sig);
        return -1;
    }

    return 0;
}
