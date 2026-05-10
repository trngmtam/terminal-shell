/*
 * main.c — Entry point của myshell
 *
 * Xử lý 2 mode:
 *   Interactive : ./shell               (REPL loop)
 *   Script      : ./shell script.sh     (chạy file)
 *   Script+stop : ./shell script.sh --stop
 *   Timeout     : ./shell script.sh --timeout 5
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* g_timeout_secs khai báo ở runner.c — dùng extern để set từ main */
extern int g_timeout_secs;

int main(int argc, char *argv[]) {
    // Đăng ký signal handlers (Member B)
    setup_signals();

    // ===== SCRIPT MODE =====
    if (argc >= 2 && argv[1][0] != '-') {
        const char *filename  = argv[1];
        int stop_on_error = 0;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--stop") == 0) {
                stop_on_error = 1;
            } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
                g_timeout_secs = atoi(argv[++i]);
                if (g_timeout_secs <= 0) g_timeout_secs = 10;
                printf("[Timeout: %d giây]\n", g_timeout_secs);
            }
        }

        run_script(filename, stop_on_error);
        return 0;
    }

    // ===== INTERACTIVE MODE (REPL) =====
    char input[MAX_INPUT];

    printf("\033[1mmyshell\033[0m — Gõ 'help' để xem lệnh hỗ trợ, 'exit' để thoát\n\n");

    while (1) {
        // In prompt: hiện thư mục hiện tại
        char cwd[MAX_INPUT];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("\033[32m%s\033[0m \033[1mmyshell>\033[0m ", cwd);
        } else {
            printf("myshell> ");
        }
        fflush(stdout);

        // Đọc input
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            printf("\n");
            break; // Ctrl+D → thoát
        }

        // Xóa newline cuối
        input[strcspn(input, "\n")] = '\0';

        // Bỏ qua dòng trống
        if (strlen(input) == 0) continue;

        // Tách lệnh thành args
        char *args[MAX_ARGS];
        char  input_copy[MAX_INPUT];
        strncpy(input_copy, input, MAX_INPUT - 1);
        input_copy[MAX_INPUT - 1] = '\0';

        int argc_cmd = tokenize(input_copy, args);
        if (argc_cmd == 0) continue;

        // Thực thi lệnh (Member A lo pipe, redirect, built-in, fork/exec)
        execute_command(args, 0);
    }

    printf("Goodbye!\n");
    return 0;
}
