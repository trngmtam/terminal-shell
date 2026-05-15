#!/bin/bash
# =============================================================
# test_myshell.sh — File test toàn bộ tính năng của myshell
#
# Cách chạy:
#   ./shell test_myshell.sh          → chạy hết, báo cáo cuối
#   ./shell test_myshell.sh --stop   → dừng ngay khi có lỗi
#
# Các nhóm test:
#   GROUP 1: Lệnh thông thường (fork/exec)
#   GROUP 2: Built-in commands (cd, pwd, exit, help)
#   GROUP 3: Redirect output (>)
#   GROUP 4: Redirect append (>>)
#   GROUP 5: Redirect input (<)
#   GROUP 6: Pipe (|)
#   GROUP 7: Lệnh không tồn tại → exit code 127
#   GROUP 8: Lệnh thất bại → exit code khác 0
# =============================================================


# =============================================================
# GROUP 1: Lệnh thông thường — fork/exec
# Mỗi lệnh này sẽ được fork ra process con rồi exec
# Kỳ vọng: exit_code = 0, không có signal
# =============================================================

# Test 1.1: liệt kê file trong thư mục hiện tại
ls

# Test 1.2: liệt kê chi tiết có ẩn
ls -la

# Test 1.3: in chuỗi ra màn hình
echo "Hello from myshell!"

# Test 1.4: xem thư mục hiện tại (lệnh ngoài, khác built-in pwd)
echo "Thu muc hien tai:"
pwd

# Test 1.5: in ngày giờ hệ thống
date

# Test 1.6: xem thông tin hệ thống
uname -a

# Test 1.7: đếm số file trong thư mục hiện tại
ls | wc -l


# =============================================================
# GROUP 2: Built-in commands
# Những lệnh này chạy TRONG shell, không fork
# =============================================================

# Test 2.1: pwd — in thư mục hiện tại
pwd

# Test 2.2: cd — chuyển về thư mục /tmp rồi kiểm tra
cd /tmp
pwd

# Test 2.3: cd về HOME (không có argument)
cd
pwd

# Test 2.4: cd về thư mục cụ thể
cd /var
pwd

# Test 2.5: cd về lại HOME
cd


# =============================================================
# GROUP 3: Redirect output (>)
# Output lệnh ghi vào file thay vì màn hình
# Kỳ vọng: file được tạo ra với nội dung đúng
# =============================================================

# Test 3.1: ghi ls vào file
ls -la > /tmp/test_output.txt
echo "Da ghi ls vao /tmp/test_output.txt"

# Test 3.2: kiểm tra file vừa tạo có tồn tại không
ls -la /tmp/test_output.txt

# Test 3.3: xem nội dung file vừa tạo
cat /tmp/test_output.txt

# Test 3.4: ghi đè file cũ bằng nội dung mới
echo "Dong moi ghi de" > /tmp/test_output.txt
cat /tmp/test_output.txt

# Test 3.5: ghi date vào file
date > /tmp/test_date.txt
cat /tmp/test_date.txt


# =============================================================
# GROUP 4: Redirect append (>>)
# Ghi thêm vào cuối file, không xóa nội dung cũ
# =============================================================

# Test 4.1: tạo file mới
echo "Dong 1" > /tmp/test_append.txt

# Test 4.2: ghi thêm dòng 2 vào cuối
echo "Dong 2" >> /tmp/test_append.txt

# Test 4.3: ghi thêm dòng 3
echo "Dong 3" >> /tmp/test_append.txt

# Test 4.4: kiểm tra file có đủ 3 dòng không
cat /tmp/test_append.txt

# Test 4.5: đếm số dòng (phải ra 3)
wc -l /tmp/test_append.txt


# =============================================================
# GROUP 5: Redirect input (<)
# Đọc input từ file thay vì bàn phím
# =============================================================

# Test 5.1: chuẩn bị file dữ liệu để sort
echo "chuoi" > /tmp/test_input.txt
echo "apple" >> /tmp/test_input.txt
echo "banana" >> /tmp/test_input.txt
echo "mango" >> /tmp/test_input.txt

# Test 5.2: sort đọc từ file (< redirect input)
sort < /tmp/test_input.txt

# Test 5.3: đếm số dòng trong file
wc -l < /tmp/test_input.txt


# =============================================================
# GROUP 6: Pipe (|)
# Output lệnh trái = Input lệnh phải
# =============================================================

# Test 6.1: pipe cơ bản — ls rồi đếm số dòng
ls | wc -l

# Test 6.2: ls rồi grep lọc file .c
ls | grep ".c"

# Test 6.3: echo rồi chuyển thành chữ hoa
echo "hello world" | tr 'a-z' 'A-Z'

# Test 6.4: cat file rồi sort
cat /tmp/test_input.txt | sort

# Test 6.5: cat file rồi đếm dòng
cat /tmp/test_input.txt | wc -l

# Test 6.6: pipe kết hợp grep
echo "tim kiem trong file:"
cat /tmp/test_input.txt | grep "a"


# =============================================================
# GROUP 7: Lệnh không tồn tại
# Kỳ vọng: exit code 127 = command not found
# error_handler.c sẽ KHÔNG in cảnh báo (bỏ qua 127)
# vì execvp đã in "command not found" rồi
# =============================================================

# Test 7.1: lệnh không tồn tại
lenh_khong_ton_tai

# Test 7.2: lệnh viết sai
Ls


# =============================================================
# GROUP 8: Lệnh thất bại (exit code khác 0, không phải 127)
# Kỳ vọng: error_handler.c in cảnh báo màu VÀNG
# và ghi vào shell_errors.log
# =============================================================

# Test 8.1: ls thư mục không tồn tại → exit code 1 hoặc 2
ls /thu_muc_khong_ton_tai_xyz

# Test 8.2: cat file không tồn tại → exit code 1
cat /file_khong_ton_tai_xyz.txt

# Test 8.3: grep tìm chuỗi không có trong file → exit code 1
echo "test grep that bai:"
echo "hello" | grep "xyz_khong_co"


# =============================================================
# DỌN DẸP — xóa các file test đã tạo
# =============================================================
rm -f /tmp/test_output.txt
rm -f /tmp/test_date.txt
rm -f /tmp/test_append.txt
rm -f /tmp/test_input.txt

echo "=== XONG! Kiem tra shell_errors.log de xem chi tiet loi ==="
