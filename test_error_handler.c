/*
 * test_error_handler.c — Chương trình test độc lập cho Member C
 *
 * Tự fork() và chạy crash_div / crash_ptr, rồi gọi check_and_report()
 * để kiểm tra toàn bộ luồng xử lý lỗi.
 *
 * Compile:
 *   gcc -o test_error_handler test_error_handler.c error_handler.c
 *
 * Chạy:
 *   ./test_error_handler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "shared.h"

/* ----------------------------------------------------------------
 * Hàm trợ giúp: chạy một lệnh bằng execvp và chờ kết quả
 * ---------------------------------------------------------------- */
static void run_and_check(const char *label, char *const argv[], int line_num)
{
    printf("\n--- %s ---\n", label);
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork that bai");
        return;
    }

    if (pid == 0) {
        /* Tiến trình con: thực thi lệnh */
        execvp(argv[0], argv);
        /* Nếu execvp thất bại */
        perror("execvp that bai");
        exit(127);
    }

    /* Tiến trình cha: chờ con xong */
    int status = 0;
    waitpid(pid, &status, 0);

    /* Gọi hàm của Member C để xử lý kết quả */
    int ret = check_and_report(argv[0], status, line_num);
    if (ret == 0) {
        printf("  => Lenh thanh cong.\n");
    } else {
        printf("  => Lenh that bai (xem log: %s)\n", "shell_errors.log");
    }
}

/* ----------------------------------------------------------------
 * main — chạy 5 test case
 * ---------------------------------------------------------------- */
int main(void)
{
    printf("======================================================\n");
    printf("  TEST SUITE — Member C: Exception & Error Handler    \n");
    printf("======================================================\n");

    /* Test 1: Lệnh bình thường (phải thành công, exit code 0) */
    char *args_ls[] = { "ls", "-la", NULL };
    run_and_check("Test 1: Lenh binh thuong (ls -la)", args_ls, 1);

    /* Test 2: Divide by zero — gây SIGFPE */
    char *args_div[] = { "./crash_div", NULL };
    run_and_check("Test 2: Divide by zero (SIGFPE)", args_div, 2);

    /* Test 3: Null pointer — gây SIGSEGV */
    char *args_ptr[] = { "./crash_ptr", NULL };
    run_and_check("Test 3: Null pointer (SIGSEGV)", args_ptr, 3);

    /* Test 4: Lệnh thoát với non-zero exit code */
    char *args_false[] = { "false", NULL };   /* 'false' luon tra ve exit code 1 */
    run_and_check("Test 4: Non-zero exit code (false)", args_false, 4);

    /* Test 5: Lệnh không tồn tại (execvp sẽ fail, exit 127) */
    char *args_unknown[] = { "./lenh_khong_ton_tai", NULL };
    run_and_check("Test 5: Lenh khong ton tai", args_unknown, 5);

    /* In tóm tắt log */
    printf("\n======================================================\n");
    printf("  NOI DUNG CUOI shell_errors.log:\n");
    printf("======================================================\n");
    system("tail -n 10 shell_errors.log");

    printf("\n======================================================\n");
    printf("  HOAN THANH — Shell van tiep tuc chay (khong thoat)\n");
    printf("======================================================\n");

    return 0;
}
