// Shared interface between all 3 members
#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

// Buffer and array size limits
#define MAX_INPUT    1024
#define MAX_ARGS     64
#define LOG_FILE     "shell_errors.log"


// Command execution result — returned by execute_command()
typedef struct {
    int  exit_code;       // Process exit code (0 = success)
    int  signal_num;      // Signal number if the process crashed (0 = no crash)
    char cmd_name[256];   // Name of the command that was run
    int  line_num;        // Line number in the script (0 if interactive mode)
} CmdResult;


// Functions provided by Member A (defined in shell.c)
int       tokenize(char *input, char **args);
CmdResult execute_command(char **args, int line_num);
int       handle_builtin(char **args);

// Functions provided by Member B (defined in runner.c)
void run_script(const char *filename, int stop_on_error);
void setup_signals(void);

// PIDs of the currently running child — set by execute_command so that
// sigint_handler (Ctrl+C) and sigalrm_handler (timeout) can kill it.
extern volatile pid_t g_current_child;
extern volatile pid_t g_timeout_child;

// Functions provided by Member C (defined in error_handler.c)
int  check_and_report(const char *cmd_name, int status, int line_num);
void log_error(const char *cmd, const char *sig_name,
               const char *sig_desc, int line_num);

#endif // SHARED_H
