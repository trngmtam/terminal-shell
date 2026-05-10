/*
 * Public API of error handler
 * Include this file anywhere error handling functions are needed.
 */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

/* Main function: call after each waitpid()
 * Returns 0 if successful, -1 if error/crash */
int  check_and_report(const char *cmd_name, int status, int line_num);

/* Manually write a log line to shell_errors.log */
void log_error(const char *cmd, const char *sig_name,
               const char *sig_desc, int line_num);

/* Utility: print the last n lines of the log to the terminal */
void print_log_tail(int n);

/* Signal lookup (can be used externally if needed) */
const char *get_signal_name(int sig);
const char *get_signal_description(int sig);

#endif