/*
 * error_handler.c — Member C: Exception & Error Handler
 *
 * Nhiệm vụ:
 *  1. Giải mã signal thành tên lỗi có mô tả
 *  2. In thông báo lỗi có màu ra terminal
 *  3. Ghi log vào file shell_errors.log với timestamp
 *  4. Hàm check_and_report() dùng chung cho Member A và B
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ======================================================
 *  MÃ MÀU ANSI
 * ====================================================== */
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RESET   "\033[0m"

/* ======================================================
 *  TASK 1: BẢNG GIẢI MÃ SIGNAL
 * ====================================================== */
typedef struct {
    int         signal_num;
    const char *name;
    const char *description;
} SignalInfo;

static SignalInfo signal_table[] = {
    { SIGSEGV,  "SIGSEGV",  "Segmentation fault (truy cập vùng nhớ không hợp lệ)" },
    { SIGFPE,   "SIGFPE",   "Floating point exception (chia cho 0, tràn số)"       },
    { SIGILL,   "SIGILL",   "Illegal instruction (lệnh CPU không hợp lệ)"          },
    { SIGBUS,   "SIGBUS",   "Bus error (lỗi căn chỉnh bộ nhớ)"                    },
    { SIGABRT,  "SIGABRT",  "Abort (chương trình tự gọi abort())"                  },
    { SIGKILL,  "SIGKILL",  "Killed (bị kill bởi hệ thống hoặc timeout)"           },
    { SIGTERM,  "SIGTERM",  "Terminated (yêu cầu kết thúc bình thường)"            },
    { SIGPIPE,  "SIGPIPE",  "Broken pipe (ghi vào pipe không có reader)"           },
    { SIGINT,   "SIGINT",   "Interrupted (người dùng nhấn Ctrl+C)"                 },
    { SIGALRM,  "SIGALRM",  "Alarm (timeout do shell đặt)"                        },
    { SIGHUP,   "SIGHUP",   "Hangup (terminal bị đóng)"                           },
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

/* ======================================================
 *  TASK 3: GHI LOG FILE VỚI TIMESTAMP
 * ====================================================== */
void log_error(const char *cmd,
               const char *sig_name,
               const char *sig_desc,
               int         line_num) {
    FILE *fp = fopen(LOG_FILE, "a"); // "a" = append, không ghi đè
    if (fp == NULL) {
        // Không crash shell chỉ vì không ghi được log
        perror("log_error: fopen");
        return;
    }

    // Lấy thời gian hiện tại
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "[%s] CRASH | cmd='%s' | line=%d | signal=%s | %s\n",
            time_str, cmd, line_num, sig_name, sig_desc);

    fclose(fp);
}

/* ======================================================
 *  TASK 2: IN LỖI CÓ MÀU RA TERMINAL
 * ====================================================== */
static void report_crash(const char *cmd_name, int sig, int line_num) {
    const char *sig_name = get_signal_name(sig);
    const char *sig_desc = get_signal_description(sig);

    fprintf(stderr,
        COLOR_RED COLOR_BOLD
        "╔══ [CRASH] ══════════════════════════════════╗\n"
        "  Lệnh  : %s\n"
        "  Dòng  : %d\n"
        "  Signal: %s (số %d)\n"
        "  Lý do : %s\n"
        "╚═════════════════════════════════════════════╝"
        COLOR_RESET "\n",
        cmd_name, line_num, sig_name, sig, sig_desc);

    // Ghi vào log file
    log_error(cmd_name, sig_name, sig_desc, line_num);
}

static void report_exit_error(const char *cmd_name, int code, int line_num) {
    // exit code 127 = command not found, đã có thông báo từ execvp
    if (code == 127) return;

    fprintf(stderr,
        COLOR_YELLOW
        "[WARN] '%s' thoát với exit code %d (dòng %d)"
        COLOR_RESET "\n",
        cmd_name, code, line_num);

    log_error(cmd_name, "EXIT_ERROR", "Non-zero exit code", line_num);
}

/* ======================================================
 *  TASK 4: HÀM TỔNG HỢP — check_and_report()
 *
 *  Gọi ngay sau mỗi waitpid().
 *  Trả về: 0 nếu thành công, -1 nếu có lỗi.
 * ====================================================== */
int check_and_report(const char *cmd_name, int status, int line_num) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            return 0; // Thành công — không in gì
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
            COLOR_CYAN "[INFO] '%s' bị tạm dừng (signal %d)\n" COLOR_RESET,
            cmd_name, sig);
        return -1;
    }

    return 0;
}
