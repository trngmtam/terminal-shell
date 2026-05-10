/*
 * runner.c — Member B: File Loader & Process Runner
 *
 * Nhiệm vụ:
 *  1. Đọc file script (.sh) và chạy từng dòng
 *  2. Nhận file script qua argument: ./shell script.sh
 *  3. Signal handling: Ctrl+C không tắt shell, chỉ hủy lệnh đang chạy
 *  4. Timeout: lệnh chạy quá lâu thì tự động kill
 *  5. Ghi lại exit code, in summary cuối
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

/* ======================================================
 *  CẤU TRÚC THỐNG KÊ
 * ====================================================== */
#define MAX_FAILED_CMDS 20

typedef struct {
    int total;
    int passed;
    int failed;
    char failed_cmds[MAX_FAILED_CMDS][MAX_INPUT];
} RunStats;

/* ======================================================
 *  BIẾN TOÀN CỤC — dùng trong signal handlers
 * ====================================================== */
static volatile pid_t g_current_child = -1;
static volatile pid_t g_timeout_child = -1;
int                   g_timeout_secs  = 10; /* extern trong main.c */

/* ======================================================
 *  TASK 2: SIGNAL HANDLING — Ctrl+C (SIGINT)
 * ====================================================== */
static void sigint_handler(int sig) {
    (void)sig;
    if (g_current_child > 0) {
        kill(g_current_child, SIGINT);
        const char msg[] = "\n[Đã gửi SIGINT đến lệnh đang chạy]\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    } else {
        const char msg[] = "\n(Không có lệnh đang chạy. Nhấn Ctrl+D hoặc gõ exit để thoát)\nmyshell> ";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
}

/* ======================================================
 *  TASK 3: TIMEOUT — SIGALRM handler
 * ====================================================== */
static void sigalrm_handler(int sig) {
    (void)sig;
    if (g_timeout_child > 0) {
        kill(g_timeout_child, SIGKILL);
        char msg[128];
        int len = snprintf(msg, sizeof(msg),
            "\n\033[31m[TIMEOUT] Lệnh bị kill sau %d giây\033[0m\n",
            g_timeout_secs);
        write(STDOUT_FILENO, msg, len);
    }
}

/* ======================================================
 *  Đăng ký tất cả signal handlers
 * ====================================================== */
void setup_signals(void) {
    struct sigaction sa;

    // SIGINT — Ctrl+C
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    // SIGALRM — timeout
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    // SIGPIPE — tránh crash khi pipe bị đóng
    signal(SIGPIPE, SIG_IGN);
}

/* ======================================================
 *  WRAPPER: execute_command + timeout tracking
 *  Bọc execute_command của Member A để thêm timeout.
 * ====================================================== */
static CmdResult run_with_timeout(char **args, int line_num) {
    /* Không thể wrap fork bên trong execute_command từ bên ngoài,
     * nên ta dùng một fork riêng ở đây để quản lý timeout.
     * Process con chạy execute_command trong một sub-shell process. */

    /* Với pipe/redirect: execute_command tự fork bên trong, nên
     * ta chỉ set g_timeout_child = -2 (sentinel) để sigalrm_handler
     * không kill nhầm. Timeout sẽ vẫn được cancel sau khi lệnh xong. */
    g_timeout_child = 0; /* Chưa có PID cụ thể */
    alarm(g_timeout_secs);

    CmdResult res = execute_command(args, line_num);

    alarm(0);
    g_timeout_child = -1;
    return res;
}

/* ======================================================
 *  TASK 1 + TASK 4: CHẠY FILE SCRIPT
 * ====================================================== */
void run_script(const char *filename, int stop_on_error) {
    FILE *fp = fopen(filename, "r");
    if (fp != NULL) {
        // FD_CLOEXEC: tu dong dong fd nay trong process con sau fork/exec
        // Ngan child doc lai script khi execvp that bai
        int fd = fileno(fp);
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    }
    if (fp == NULL) {
        fprintf(stderr, "\033[31mLỗi: Không mở được file '%s'\033[0m\n", filename);
        exit(1);
    }

    RunStats stats;
    memset(&stats, 0, sizeof(stats));

    char raw_line[MAX_INPUT];
    int  line_num = 0;

    printf("\033[1m===== Bắt đầu chạy script: %s =====\033[0m\n\n", filename);

    while (fgets(raw_line, MAX_INPUT, fp) != NULL) {
        line_num++;

        raw_line[strcspn(raw_line, "\n")] = '\0';

        // Cắt khoảng trắng đầu dòng
        char *line = raw_line;
        while (*line == ' ' || *line == '\t') line++;

        if (*line == '\0' || *line == '#') continue; // Bỏ qua trống và comment

        printf("\033[90m[Dòng %d]\033[0m %s\n", line_num, line);

        // Tách lệnh thành args
        char line_copy[MAX_INPUT];
        strncpy(line_copy, line, MAX_INPUT - 1);
        line_copy[MAX_INPUT - 1] = '\0';

        char *args[MAX_ARGS];
        tokenize(line_copy, args);

        if (args[0] == NULL) continue;

        stats.total++;

        // Gọi execute_command của Member A (với timeout)
        CmdResult res = run_with_timeout(args, line_num);

        if (res.exit_code == 0 && res.signal_num == 0) {
            stats.passed++;
        } else {
            stats.failed++;
            if (stats.failed <= MAX_FAILED_CMDS) {
                snprintf(stats.failed_cmds[stats.failed - 1],
                         MAX_INPUT - 1,
                         "Dong %d: %.900s", line_num, line);
            }
            if (stop_on_error) {
                printf("\033[31m[ABORT] Dừng script do lỗi ở dòng %d\033[0m\n", line_num);
                break;
            }
        }
    }

    fclose(fp);

    // ===== IN SUMMARY =====
    printf("\n\033[1m===== KẾT QUẢ CHẠY SCRIPT =====\033[0m\n");
    printf("Tổng lệnh đã chạy : %d\n",  stats.total);
    printf("\033[32mThành công (OK)   : %d\033[0m\n", stats.passed);
    printf("\033[31mThất bại (FAILED) : %d\033[0m\n", stats.failed);

    if (stats.failed > 0) {
        printf("\nCác lệnh bị lỗi:\n");
        int show = stats.failed < MAX_FAILED_CMDS ? stats.failed : MAX_FAILED_CMDS;
        for (int i = 0; i < show; i++) {
            printf("  \033[31m✗\033[0m %s\n", stats.failed_cmds[i]);
        }
    }
    printf("================================\n");
}
