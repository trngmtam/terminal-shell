/*
 * runner.c — Đọc file script và chạy từng lệnh
 *
 * Gồm 6 phần:
 *   Phần 1: RunStats          — struct đếm kết quả chạy script
 *   Phần 2: sigint_handler()  — xử lý Ctrl+C, chỉ kill lệnh đang chạy
 *   Phần 3: sigalrm_handler() — xử lý timeout, kill lệnh chạy quá giờ
 *   Phần 4: setup_signals()   — đăng ký Phần 2 và 3 với hệ điều hành
 *   Phần 5: run_with_timeout()— bọc execute_command() thêm đồng hồ đếm ngược
 *   Phần 6: run_script()      — đọc từng dòng file .sh và chạy
 */

#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>


/* -------------------------------------------------------
 * Phần 1: RunStats — Bảng thống kê kết quả chạy script
 *
 * Ý nghĩa: Theo dõi bao nhiêu lệnh thành công, thất bại
 *          và lưu danh sách các lệnh bị lỗi để in báo cáo cuối
 * ------------------------------------------------------- */

// Tối đa lưu 20 lệnh lỗi — tránh tốn bộ nhớ khi script có nhiều lỗi
#define MAX_FAILED_CMDS 20

typedef struct {
    int total;   // Tổng số lệnh đã chạy (không tính dòng trống và comment)
    int passed;  // Số lệnh thành công (exit_code == 0)
    int failed;  // Số lệnh thất bại (exit_code != 0 hoặc bị crash)

    // Mảng 2 chiều lưu tên các lệnh bị lỗi
    // failed_cmds[0] = "Dong 3: ./buggy"
    // failed_cmds[1] = "Dong 7: gcc main.c"
    char failed_cmds[MAX_FAILED_CMDS][MAX_INPUT];
} RunStats;


/* -------------------------------------------------------
 * Biến toàn cục — dùng chung giữa các signal handler
 *
 * volatile: báo compiler KHÔNG cache biến này vào register
 *           vì signal handler có thể thay đổi bất cứ lúc nào
 * static:   chỉ dùng trong file runner.c, file khác không thấy
 * ------------------------------------------------------- */

// PID của lệnh đang chạy (-1 = không có lệnh nào)
static volatile pid_t g_current_child = -1;

// PID của lệnh đang bị theo dõi timeout (-1 = không có)
static volatile pid_t g_timeout_child = -1;

// Số giây timeout mặc định — main.c có thể thay đổi qua --timeout
int g_timeout_secs = 10;


/* -------------------------------------------------------
 * Phần 2: sigint_handler() — Xử lý Ctrl+C
 *
 * Ý nghĩa: Khi người dùng nhấn Ctrl+C, thay vì tắt cả shell
 *          chỉ kill lệnh đang chạy, shell vẫn sống
 *
 * Cách hoạt động: Hệ điều hành tự gọi hàm này khi nhận Ctrl+C
 *                 (được đăng ký ở Phần 4 - setup_signals)
 * ------------------------------------------------------- */
static void sigint_handler(int sig) {
    // Không dùng tham số sig — báo compiler đừng cảnh báo
    (void)sig;

    if (g_current_child > 0) {
        // Có lệnh đang chạy → gửi SIGINT đến lệnh đó
        // SIGINT = tín hiệu "dừng lịch sự", giống Ctrl+C trực tiếp vào lệnh
        kill(g_current_child, SIGINT);

        // Dùng write() thay printf() vì signal handler không được dùng printf()
        // printf không an toàn khi bị ngắt giữa chừng → có thể deadlock
        const char msg[] = "\n[Đã gửi SIGINT đến lệnh đang chạy]\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1); // sizeof - 1 để bỏ ký tự '\0' cuối
    } else {
        // Không có lệnh nào đang chạy → nhắc người dùng cách thoát
        const char msg[] = "\n(Không có lệnh đang chạy. Nhấn Ctrl+D hoặc gõ exit để thoát)\nmyshell> ";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
}


/* -------------------------------------------------------
 * Phần 3: sigalrm_handler() — Xử lý Timeout
 *
 * Ý nghĩa: Khi lệnh chạy quá g_timeout_secs giây,
 *          hệ điều hành gửi SIGALRM → hàm này kill lệnh đó
 *
 * Khác Phần 2: dùng SIGKILL thay SIGINT vì:
 *   SIGINT  = "dừng lại đi" → process có thể bỏ qua
 *   SIGKILL = "tắt ngay"    → process KHÔNG THỂ bỏ qua, OS kill luôn
 *
 * Cách hoạt động: alarm(N) ở Phần 5 đặt đồng hồ N giây
 *                 Sau N giây OS gửi SIGALRM → hàm này được gọi
 * ------------------------------------------------------- */
static void sigalrm_handler(int sig) {
    (void)sig;

    if (g_timeout_child > 0) {
        // Lệnh vẫn đang chạy sau khi hết giờ → kill ngay lập tức
        kill(g_timeout_child, SIGKILL);

        // Tạo thông báo kèm số giây timeout
        // Phải dùng snprintf vì cần điền số giây vào chuỗi
        char msg[128];
        int len = snprintf(msg, sizeof(msg),
            "\n\033[31m[TIMEOUT] Lệnh bị kill sau %d giây\033[0m\n",
            // \033[31m = màu đỏ, \033[0m = reset màu
            g_timeout_secs);

        // len = độ dài thật của msg sau khi điền số giây vào
        write(STDOUT_FILENO, msg, len);
    }
    // Nếu g_timeout_child <= 0: lệnh đã xong trước khi hết giờ → bỏ qua
}


/* -------------------------------------------------------
 * Phần 4: setup_signals() — Đăng ký Signal Handlers
 *
 * Ý nghĩa: Nói với hệ điều hành "khi có Ctrl+C thì gọi hàm nào,
 *          khi hết giờ thì gọi hàm nào"
 *
 * Gọi 1 lần duy nhất trong main() trước khi làm bất cứ điều gì
 * Sau khi gọi xong: Phần 2 và Phần 3 tự động hoạt động khi có signal
 * ------------------------------------------------------- */
void setup_signals(void) {
    // struct sigaction: cấu hình chi tiết cách xử lý 1 signal
    struct sigaction sa;

    // --- Đăng ký xử lý Ctrl+C (SIGINT) ---
    memset(&sa, 0, sizeof(sa));       // xóa sạch struct về 0 trước khi dùng
    sa.sa_handler = sigint_handler;   // hàm sẽ được gọi khi có Ctrl+C (Phần 2)
    sigemptyset(&sa.sa_mask);         // không chặn signal nào khác trong lúc xử lý
    sa.sa_flags = SA_RESTART;         // tự động restart các system call bị ngắt giữa chừng
    sigaction(SIGINT, &sa, NULL);     // đăng ký với OS: SIGINT → sigint_handler

    // --- Đăng ký xử lý Timeout (SIGALRM) ---
    memset(&sa, 0, sizeof(sa));       // reset struct để dùng lại cho signal khác
    sa.sa_handler = sigalrm_handler;  // hàm sẽ được gọi khi hết giờ (Phần 3)
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);    // đăng ký với OS: SIGALRM → sigalrm_handler

    // --- Bỏ qua SIGPIPE ---
    // SIGPIPE xảy ra khi ghi vào pipe không có ai đọc
    // (ví dụ: lệnh bên phải của pipe đã chết trước)
    // SIG_IGN = ignore, bỏ qua hoàn toàn → tránh shell bị crash vì lý do này
    signal(SIGPIPE, SIG_IGN);
}


/* -------------------------------------------------------
 * Phần 5: run_with_timeout() — Chạy lệnh kèm đồng hồ timeout
 *
 * Ý nghĩa: Bọc execute_command() thêm lớp bảo vệ timeout
 *          Bật đồng hồ trước → chạy lệnh → tắt đồng hồ sau
 *
 * Nếu lệnh xong trước khi hết giờ: alarm(0) hủy đồng hồ → bình thường
 * Nếu lệnh chạy quá giờ: SIGALRM kích hoạt Phần 3 → kill lệnh
 * ------------------------------------------------------- */
static CmdResult run_with_timeout(char **args, int line_num) {
    // Đặt = 0 báo hiệu "có lệnh đang chạy nhưng chưa biết PID"
    // (vì execute_command tự fork bên trong, ta không lấy được PID trực tiếp)
    g_timeout_child = 0;

    // Bắt đầu đếm ngược — sau g_timeout_secs giây OS sẽ gửi SIGALRM
    alarm(g_timeout_secs);

    // Chạy lệnh thật (hàm từ shell.c) — có thể mất vài giây
    CmdResult res = execute_command(args, line_num);

    // Lệnh xong → hủy đồng hồ ngay
    // alarm(0) = cancel alarm đang chạy, SIGALRM sẽ không được gửi nữa
    alarm(0);

    // Reset về -1 báo hiệu "không còn lệnh nào cần theo dõi"
    g_timeout_child = -1;

    return res;
}


/* -------------------------------------------------------
 * Phần 6: run_script() — Đọc và Chạy File Script
 *
 * Ý nghĩa: Mở file .sh, đọc từng dòng, bỏ qua comment và
 *          dòng trống, chạy từng lệnh, thống kê kết quả,
 *          in báo cáo tổng kết cuối cùng
 *
 * Được gọi bởi main() khi người dùng chạy: ./shell script.sh
 * ------------------------------------------------------- */
void run_script(const char *filename, int stop_on_error) {

    // Mở file script để đọc
    FILE *fp = fopen(filename, "r");

    if (fp != NULL) {
        // FD_CLOEXEC: tự động đóng file descriptor này khi process con fork ra
        // Ngăn process con vô tình đọc lại nội dung file script qua stdin
        int fd = fileno(fp);  // lấy số file descriptor của fp
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
        //         ↑ set flag    ↑ lấy flag cũ   ↑ thêm FD_CLOEXEC vào flag cũ
    }

    // Kiểm tra mở file có thành công không
    if (fp == NULL) {
        fprintf(stderr, "\033[31mLỗi: Không mở được file '%s'\033[0m\n", filename);
        exit(1);  // thoát chương trình, không chạy tiếp được
    }

    // Khởi tạo bảng thống kê — xóa toàn bộ về 0
    RunStats stats;
    memset(&stats, 0, sizeof(stats));

    char raw_line[MAX_INPUT];  // buffer chứa từng dòng đọc từ file
    int  line_num = 0;         // đếm số dòng (kể cả dòng trống và comment)

    printf("\033[1m===== Bắt đầu chạy script: %s =====\033[0m\n\n", filename);

    // Đọc từng dòng cho đến hết file
    // fgets đọc 1 dòng vào raw_line, trả về NULL khi hết file
    while (fgets(raw_line, MAX_INPUT, fp) != NULL) {
        line_num++;  // tăng số dòng dù dòng này có chạy hay không

        // Xóa ký tự '\n' ở cuối dòng mà fgets giữ lại
        // strcspn trả về vị trí đầu tiên của '\n' trong chuỗi
        raw_line[strcspn(raw_line, "\n")] = '\0';

        // Bỏ qua khoảng trắng và tab ở đầu dòng
        // Dịch con trỏ line về phía trước đến ký tự thật đầu tiên
        char *line = raw_line;
        while (*line == ' ' || *line == '\t') line++;

        // Bỏ qua dòng trống (*line == '\0') và dòng comment (*line == '#')
        if (*line == '\0' || *line == '#') continue;

        // In số dòng và nội dung lệnh ra màn hình (màu xám)
        printf("\033[90m[Dòng %d]\033[0m %s\n", line_num, line);

        // Copy dòng lệnh sang buffer mới TRƯỚC KHI tách
        // Vì tokenize() dùng strtok() sẽ sửa trực tiếp chuỗi bằng cách chèn '\0'
        // Cần giữ nguyên chuỗi gốc (line) để lưu vào failed_cmds nếu lệnh lỗi
        char line_copy[MAX_INPUT];
        strncpy(line_copy, line, MAX_INPUT - 1);
        line_copy[MAX_INPUT - 1] = '\0';  // đảm bảo luôn có '\0' ở cuối

        // Tách chuỗi lệnh thành mảng args[]
        // "gcc -o main main.c" → ["gcc", "-o", "main", "main.c", NULL]
        char *args[MAX_ARGS];
        tokenize(line_copy, args);  // hàm từ shell.c

        // Bỏ qua nếu không tách được từ nào (dòng chỉ có khoảng trắng)
        if (args[0] == NULL) continue;

        // Đếm vào tổng số lệnh đã chạy
        stats.total++;

        // Chạy lệnh qua Phần 5 (kèm đồng hồ timeout)
        // Trả về CmdResult chứa exit_code và signal_num
        CmdResult res = run_with_timeout(args, line_num);

        if (res.exit_code == 0 && res.signal_num == 0) {
            // Thành công: thoát bình thường với exit code = 0
            stats.passed++;
        } else {
            // Thất bại: exit code khác 0 hoặc bị kill bởi signal
            stats.failed++;

            // Lưu thông tin lệnh lỗi vào danh sách (tối đa MAX_FAILED_CMDS)
            if (stats.failed <= MAX_FAILED_CMDS) {
                snprintf(stats.failed_cmds[stats.failed - 1],
                         MAX_INPUT - 1,
                         "Dong %d: %.900s",  // %.900s giới hạn 900 ký tự tên lệnh
                         line_num, line);    // line = chuỗi gốc trước khi tokenize sửa
            }

            // Nếu người dùng bật --stop: dừng script ngay tại đây
            if (stop_on_error) {
                printf("\033[31m[ABORT] Dừng script do lỗi ở dòng %d\033[0m\n", line_num);
                break;  // thoát vòng while, không chạy các dòng tiếp theo
            }
        }
    }

    fclose(fp);  // đóng file sau khi đọc xong hoặc bị break

    // -------------------------------------------------------
    // In báo cáo tổng kết
    // -------------------------------------------------------
    printf("\n\033[1m===== KẾT QUẢ CHẠY SCRIPT =====\033[0m\n");
    printf("Tổng lệnh đã chạy : %d\n",  stats.total);
    printf("\033[32mThành công (OK)   : %d\033[0m\n", stats.passed);  // màu xanh lá
    printf("\033[31mThất bại (FAILED) : %d\033[0m\n", stats.failed);  // màu đỏ

    // Nếu có lệnh lỗi → in danh sách chi tiết
    if (stats.failed > 0) {
        printf("\nCác lệnh bị lỗi:\n");

        // Chỉ in tối đa MAX_FAILED_CMDS dòng để tránh in quá dài
        int show = stats.failed < MAX_FAILED_CMDS ? stats.failed : MAX_FAILED_CMDS;
        for (int i = 0; i < show; i++) {
            printf("  \033[31m✗\033[0m %s\n", stats.failed_cmds[i]);
            //       ↑ dấu X màu đỏ  ↑ tên lệnh bị lỗi
        }
    }
    printf("================================\n");
}
