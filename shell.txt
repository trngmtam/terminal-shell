/*
 * shell.c — Member A: Shell & Command Parser
 *
 * Nhiệm vụ:
 *  1. Tokenizer       — tách chuỗi lệnh thành args[]
 *  2. Fork/Exec       — tạo process con để chạy lệnh
 *  3. Pipe ( | )      — kết nối stdout lệnh trái với stdin lệnh phải
 *  4. Redirect (>,<,>>) — chuyển hướng stdin/stdout vào file
 *  5. Built-ins       — cd, pwd, exit, help
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

/* ======================================================
 *  TASK 1: TOKENIZER
 *  Tách chuỗi input thành mảng args[], kết thúc bằng NULL.
 *  Trả về số lượng token.
 * ====================================================== */
int tokenize(char *input, char **args) {
    int count = 0;
    char *token = strtok(input, " \t\r\n");
    while (token != NULL && count < MAX_ARGS - 1) {
        args[count++] = token;
        token = strtok(NULL, " \t\r\n");
    }
    args[count] = NULL; // Bắt buộc cho execvp()
    return count;
}

/* ======================================================
 *  TASK 5: BUILT-IN COMMANDS
 *  Xử lý trực tiếp trong process cha (không fork).
 *  Trả về 1 nếu đã xử lý, 0 nếu không phải built-in.
 * ====================================================== */
int handle_builtin(char **args) {
    if (args[0] == NULL) return 1;

    // cd [path]
    if (strcmp(args[0], "cd") == 0) {
        char *path = args[1] ? args[1] : getenv("HOME");
        if (path == NULL) path = "/";
        if (chdir(path) != 0) {
            perror("cd");
        }
        return 1;
    }

    // pwd — in thư mục hiện tại
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_INPUT];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd");
        }
        return 1;
    }

    // exit [code]
    if (strcmp(args[0], "exit") == 0) {
        int code = args[1] ? atoi(args[1]) : 0;
        printf("Goodbye!\n");
        exit(code);
    }

    // help
    if (strcmp(args[0], "help") == 0) {
        printf("\033[1mmyshell\033[0m — Built-in commands:\n");
        printf("  cd [dir]   — đổi thư mục làm việc\n");
        printf("  pwd        — in thư mục hiện tại\n");
        printf("  exit [n]   — thoát shell (exit code n, mặc định 0)\n");
        printf("  help       — hiển thị trợ giúp này\n");
        printf("\nLệnh bên ngoài: dùng fork/exec, hỗ trợ pipe (|) và redirect (>, <, >>)\n");
        return 1;
    }

    return 0; // Không phải built-in
}

/* ======================================================
 *  PHÁT HIỆN PIPE VÀ REDIRECT TRONG ARGS
 *
 *  Hàm find_pipe() tìm vị trí ký tự "|" trong args[].
 *  Trả về index của "|", hoặc -1 nếu không có.
 * ====================================================== */
static int find_pipe(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) return i;
    }
    return -1;
}

/*
 * Hàm parse_redirects() quét args[] để tìm >, <, >>.
 * Điền vào out_file / in_file / append_file nếu tìm thấy.
 * Xóa các token redirect khỏi args[] để execvp không bị lỗi.
 */
static void parse_redirects(char **args,
                             char **out_file,
                             char **in_file,
                             int  *append) {
    *out_file = NULL;
    *in_file  = NULL;
    *append   = 0;

    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], ">") == 0 && args[i+1] != NULL) {
            *out_file = args[i+1];
            *append   = 0;
            // Xóa 2 token ">", "filename" khỏi mảng
            int j = i;
            while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
            args[j] = NULL; args[j+1] = NULL;
            continue; // Không tăng i, kiểm tra lại vị trí i
        }
        if (strcmp(args[i], ">>") == 0 && args[i+1] != NULL) {
            *out_file = args[i+1];
            *append   = 1;
            int j = i;
            while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
            args[j] = NULL; args[j+1] = NULL;
            continue;
        }
        if (strcmp(args[i], "<") == 0 && args[i+1] != NULL) {
            *in_file = args[i+1];
            int j = i;
            while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
            args[j] = NULL; args[j+1] = NULL;
            continue;
        }
        i++;
    }
}

/* ======================================================
 *  TASK 4: REDIRECT — chạy lệnh với chuyển hướng file
 * ====================================================== */
static CmdResult execute_with_redirect(char **args,
                                       char *out_file,
                                       char *in_file,
                                       int   append,
                                       int   line_num) {
    CmdResult result;
    memset(&result, 0, sizeof(result));
    strncpy(result.cmd_name, args[0] ? args[0] : "(empty)", 255);
    result.line_num = line_num;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        result.exit_code = -1;
        return result;
    }

    if (pid == 0) {
        // === PROCESS CON ===
        if (out_file != NULL) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(out_file, flags, 0644);
            if (fd < 0) { perror("open (output redirect)"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (in_file != NULL) {
            int fd = open(in_file, O_RDONLY);
            if (fd < 0) { perror("open (input redirect)"); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        execvp(args[0], args);
        fprintf(stderr, "myshell: %s: command not found\n", args[0]);
        _exit(127);
    }

    // === PROCESS CHA ===
    int status;
    waitpid(pid, &status, 0);
    check_and_report(args[0], status, line_num);

    if (WIFEXITED(status)) {
        result.exit_code  = WEXITSTATUS(status);
        result.signal_num = 0;
    } else if (WIFSIGNALED(status)) {
        result.exit_code  = -1;
        result.signal_num = WTERMSIG(status);
    }
    return result;
}

/* ======================================================
 *  TASK 3: PIPE — chạy 2 lệnh nối bằng pipe
 * ====================================================== */
static CmdResult execute_pipe(char **args1, char **args2, int line_num) {
    CmdResult result;
    memset(&result, 0, sizeof(result));
    strncpy(result.cmd_name, args1[0] ? args1[0] : "(pipe)", 255);
    result.line_num = line_num;

    int pipefd[2]; // [0]=đọc, [1]=ghi
    if (pipe(pipefd) == -1) {
        perror("pipe");
        result.exit_code = -1;
        return result;
    }

    // Process con 1: lệnh bên TRÁI pipe, ghi vào pipe
    pid_t pid1 = fork();
    if (pid1 < 0) { perror("fork"); result.exit_code = -1; return result; }
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO); // stdout → pipe write
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(args1[0], args1);
        fprintf(stderr, "myshell: %s: command not found\n", args1[0]);
        _exit(127);
    }

    // Process con 2: lệnh bên PHẢI pipe, đọc từ pipe
    pid_t pid2 = fork();
    if (pid2 < 0) { perror("fork"); result.exit_code = -1; return result; }
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO); // stdin ← pipe read
        close(pipefd[1]);
        close(pipefd[0]);
        execvp(args2[0], args2);
        fprintf(stderr, "myshell: %s: command not found\n", args2[0]);
        _exit(127);
    }

    // Cha đóng cả 2 đầu và chờ 2 con
    close(pipefd[0]);
    close(pipefd[1]);

    int status1, status2;
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);

    // Báo lỗi nếu có
    check_and_report(args1[0], status1, line_num);
    check_and_report(args2[0], status2, line_num);

    if (WIFEXITED(status2)) {
        result.exit_code  = WEXITSTATUS(status2); // exit code của lệnh phải
        result.signal_num = 0;
    } else if (WIFSIGNALED(status2)) {
        result.exit_code  = -1;
        result.signal_num = WTERMSIG(status2);
    }
    return result;
}

/* ======================================================
 *  TASK 2 + TỔNG HỢP: EXECUTE_COMMAND
 *
 *  Đây là hàm chính mà Member B gọi.
 *  Tự động phát hiện và xử lý:
 *    - Built-in commands
 *    - Pipe (|)
 *    - Redirect (>, <, >>)
 *    - Lệnh thông thường (fork/exec)
 * ====================================================== */
CmdResult execute_command(char **args, int line_num) {
    CmdResult result;
    memset(&result, 0, sizeof(result));
    result.line_num = line_num;

    if (args[0] == NULL) return result; // Lệnh rỗng

    strncpy(result.cmd_name, args[0], 255);

    // Kiểm tra built-in trước
    if (handle_builtin(args)) {
        result.exit_code = 0;
        return result;
    }

    // Kiểm tra có pipe không
    int pipe_pos = find_pipe(args);
    if (pipe_pos >= 0) {
        // Tách args thành 2 mảng: trái và phải pipe
        args[pipe_pos] = NULL;         // Cắt tại "|"
        char **args_left  = args;
        char **args_right = args + pipe_pos + 1;

        if (args_right[0] == NULL) {
            fprintf(stderr, "myshell: lỗi cú pháp: thiếu lệnh sau '|'\n");
            result.exit_code = 1;
            return result;
        }
        return execute_pipe(args_left, args_right, line_num);
    }

    // Kiểm tra redirect
    char *out_file = NULL;
    char *in_file  = NULL;
    int   append   = 0;
    parse_redirects(args, &out_file, &in_file, &append);

    if (out_file != NULL || in_file != NULL) {
        return execute_with_redirect(args, out_file, in_file, append, line_num);
    }

    // Lệnh thông thường — fork/exec
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        result.exit_code = -1;
        return result;
    }

    if (pid == 0) {
        // === PROCESS CON ===
        // Reset stdin ve /dev/null de tranh con doc lai file script
        int dn = open("/dev/null", 0);
        if (dn >= 0) { dup2(dn, STDIN_FILENO); close(dn); }
        execvp(args[0], args);
        fprintf(stderr, "myshell: %s: command not found\n", args[0]);
        _exit(127);
    }

    // === PROCESS CHA ===
    int status;
    waitpid(pid, &status, 0);
    check_and_report(args[0], status, line_num);

    if (WIFEXITED(status)) {
        result.exit_code  = WEXITSTATUS(status);
        result.signal_num = 0;
    } else if (WIFSIGNALED(status)) {
        result.exit_code  = -1;
        result.signal_num = WTERMSIG(status);
    }
    return result;
}
