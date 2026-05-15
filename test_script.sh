# test_script.sh — full error-handling test suite
#
# How to run:
#   ./shell test_script.sh                  # run all cases, keep going after errors
#   ./shell test_script.sh --stop           # halt at first error
#   ./shell test_script.sh --timeout 3      # use a 3-second timeout
#
# After running, inspect the log:
#   cat shell_errors.log

# --- 1. PASS: normal commands (exit code 0) ---
echo "1. PASS: normal commands"
echo "hello from script"
pwd
ls /tmp > /dev/null

# --- 2. PASS: pipes and redirects ---
echo "2. PASS: pipes and redirects"
echo "abc def ghi" | wc -w
echo "redirected" > /tmp/myshell_test.txt
cat /tmp/myshell_test.txt

# --- 3. FAIL: non-zero exit code (WIFEXITED, code != 0) ---
echo "3. FAIL: non-zero exit code"
ls /this/path/does/not/exist
false

# --- 5. CRASH: SIGFPE (divide by zero) ---
echo "5. CRASH: SIGFPE"
./crash_div

# --- 6. CRASH: SIGSEGV (null pointer) ---
echo "6. CRASH: SIGSEGV"
./crash_ptr

# --- 7. CRASH: SIGABRT (program calls abort) ---
echo "7. CRASH: SIGABRT"
./crash_abrt

# --- 8. TIMEOUT: SIGKILL after deadline (run with --timeout N) ---
echo "8. TIMEOUT: long-running command"
sleep 10

# --- 9. PASS: shell survived everything above ---
echo "9. PASS: shell still alive"
echo "all tests completed"
