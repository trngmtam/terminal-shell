// shared.h — Giao diện dùng chung giữa 3 members
#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

#define MAX_INPUT    1024
#define MAX_ARGS     64
#define LOG_FILE     "shell_errors.log"

// === CẤU TRÚC KẾT QUẢ CHẠY LỆNH ===
typedef struct {
    int  exit_code;       // Exit code của process (0 = thành công)
    int  signal_num;      // Signal nếu bị crash (0 = không crash)
    char cmd_name[256];   // Tên lệnh đã chạy
    int  line_num;        // Dòng trong script (0 nếu interactive)
} CmdResult;

// === HÀM CỦA MEMBER A (định nghĩa trong shell.c) ===
int       tokenize(char *input, char **args);
CmdResult execute_command(char **args, int line_num);
int       handle_builtin(char **args);

// === HÀM CỦA MEMBER B (định nghĩa trong runner.c) ===
void run_script(const char *filename, int stop_on_error);
void setup_signals(void);

// === HÀM CỦA MEMBER C (định nghĩa trong error_handler.c) ===
int  check_and_report(const char *cmd_name, int status, int line_num);
void log_error(const char *cmd, const char *sig_name,
               const char *sig_desc, int line_num);

#endif // SHARED_H
