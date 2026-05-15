# terminal-shell
# myshell 🐚

A lightweight Unix shell built from scratch in C — supporting interactive mode, script execution, pipes, redirects, signal handling, and timeout management.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Project Structure](#project-structure)
- [Architecture](#architecture)
- [File Breakdown](#file-breakdown)
  - [shared.h](#sharedhh)
  - [main.c](#mainc)
  - [shell.c](#shellc)
  - [runner.c](#runnerc)
  - [error\_handler.c](#error_handlerc)
- [How to Build](#how-to-build)
- [How to Run](#how-to-run)
- [Test Cases](#test-cases)
- [Error Logging](#error-logging)

---

## Overview

`myshell` is a minimal Unix shell written in C. It reads commands either from the keyboard (interactive mode) or from a script file, then executes them by forking child processes. It handles pipes, I/O redirection, built-in commands, signal interrupts, and per-command timeouts.

The project is split across **5 files**, each with a distinct responsibility:

```
shared.h          → shared types and function declarations
main.c            → entry point, argument parsing, mode selection
shell.c           → command parsing and execution engine
runner.c          → script file loader, signal handling, timeout
error_handler.c   → error classification, colored output, log file
```

---

## Features

| Feature | Description |
|---|---|
| Interactive REPL | Prompt loop — type commands, see results immediately |
| Script execution | `./shell script.sh` runs every line in the file |
| Built-in commands | `cd`, `pwd`, `exit`, `help` run inside the shell process |
| Pipe `\|` | Connects stdout of left command to stdin of right command |
| Redirect `>` | Writes command output to a file (overwrites) |
| Redirect `>>` | Appends command output to a file |
| Redirect `<` | Reads command input from a file |
| Signal handling | `Ctrl+C` kills the current command only — shell stays alive |
| Timeout | `--timeout N` auto-kills any command running longer than N seconds |
| Stop on error | `--stop` halts the script immediately when any command fails |
| Error logging | All failures are written to `shell_errors.log` with timestamps |
| Colored output | Crash = red, warning = yellow, info = cyan |

---

## Project Structure

```
myshell/
├── shared.h          # Shared header: CmdResult struct + all function prototypes
├── main.c            # Entry point: interactive loop or script mode
├── shell.c           # Tokenizer, built-ins, pipe, redirect, fork/exec
├── runner.c          # Script reader, Ctrl+C handler, timeout, stats summary
├── error_handler.c   # Signal decoder, colored error printer, log writer
├── test_myshell.sh   # Test script covering all features
└── shell_errors.log  # Auto-generated: error log (created at runtime)
```

---

## Architecture

### High-level flow

```
User input (keyboard or file)
            │
            ▼
        main.c
       /       \
      /         \
Interactive    Script
  mode          mode
      \         /
       \       /
        ▼     ▼
      runner.c
    (script mode only:
     reads lines, timeout,
     Ctrl+C, stats)
            │
            ▼
        shell.c  ◄──────── called by both main.c and runner.c
   (execution engine)
     /    |    |    \
    /     |    |     \
builtin  pipe redir  fork
cd/pwd   |    > < >>  exec
exit     |              \
help     ▼               ▼
      2 children     1 child
      connected      runs command
      by pipe
            │
            ▼
    error_handler.c
   (called after every waitpid)
     /              \
    /                \
print colored      write to
error to stderr    shell_errors.log
```

### Data flow through `CmdResult`

Every command execution returns a `CmdResult` struct — the single shared currency between all modules:

```c
typedef struct {
    int  exit_code;      // 0 = success, non-zero = error
    int  signal_num;     // 0 = normal exit, non-zero = crashed by signal
    char cmd_name[256];  // name of the command that ran
    int  line_num;       // line number in script (0 if interactive)
} CmdResult;
```

```
shell.c: execute_command()  ──returns CmdResult──►  runner.c: run_with_timeout()
                                                              │
                                                              ▼
                                                     stats.passed++ or stats.failed++
```

---

## File Breakdown

---

### `shared.h`

**Role:** The contract. Defines shared types and declares all public functions so each `.c` file can see the others without including their source.

#### Contents

| Section | What it does |
|---|---|
| Include guard `#ifndef SHARED_H` | Prevents double-inclusion when multiple files `#include` this header |
| `#define MAX_INPUT 1024` | Max length of a single command line |
| `#define MAX_ARGS 64` | Max number of tokens in one command |
| `#define LOG_FILE "shell_errors.log"` | Log file name — change here to change everywhere |
| `typedef struct { ... } CmdResult` | The "result ticket" returned by every command execution |
| Function prototypes (3 groups) | Declares all public functions from shell.c, runner.c, error_handler.c |

#### Why `#define` instead of `const int`

In C, `const int` cannot be used to declare array sizes. `#define` is a preprocessor text replacement — it works everywhere, including `char buf[MAX_INPUT]`.

#### Why function prototypes matter

Without prototypes, calling `execute_command()` from `runner.c` would produce a compile error: `implicit declaration of function`. The prototype tells the compiler the function exists and what its signature is — the linker resolves the actual address later.

---

### `main.c`

**Role:** Entry point. Reads command-line arguments and routes to either interactive REPL or script mode.

#### Two modes

```
./shell                           → Interactive mode
./shell script.sh                 → Script mode
./shell script.sh --stop          → Script mode, halt on first error
./shell script.sh --timeout 5     → Script mode, kill commands after 5s
./shell script.sh --stop --timeout 3  → Both options combined
```

#### Section 1 — Startup

```c
setup_signals();
```

Called first, before anything else. Registers signal handlers with the OS so `Ctrl+C` and `alarm()` behave correctly from the very start.

#### Section 2 — Script mode (`argc >= 2`)

Parses `--stop` and `--timeout N` from `argv`, then calls `run_script()`.

Key detail: `--timeout` is followed by a number in the next argument slot:

```
argv[2] = "--timeout"
argv[3] = "5"         ← i++ to step past "--timeout", then atoi(argv[i])
```

`g_timeout_secs` is declared in `runner.c` and accessed via `extern` — `main.c` writes to it, `runner.c` reads it.

#### Section 3 — Interactive mode (REPL loop)

```
print prompt → fgets() reads one line → tokenize → execute_command → repeat
```

- `fgets()` returns `NULL` on `Ctrl+D` → clean exit
- `strcspn(input, "\n")` removes the trailing newline `fgets` keeps
- Input is copied before `tokenize()` because `strtok()` modifies the string in-place

---

### `shell.c`

**Role:** The execution engine. Receives `args[]`, classifies the command type, and runs it correctly.

#### Part 1 — `tokenize()`

Splits a raw string into an array of tokens using `strtok()`.

```
Input:  "ls -la /home"
Output: args[0]="ls"  args[1]="-la"  args[2]="/home"  args[3]=NULL
```

- Delimiters: space, tab, carriage return, newline
- `args[count] = NULL` at the end is **mandatory** — `execvp()` reads until it sees `NULL`
- `strtok()` inserts `\0` characters directly into the input string — always pass a **copy**

#### Part 2 — `handle_builtin()`

Handles four commands that must run inside the shell process itself:

| Command | Why it must be built-in |
|---|---|
| `cd [path]` | `chdir()` only affects the current process. If forked, the child changes directory and dies — the shell's directory never changes |
| `pwd` | Reads and prints the shell's own current directory |
| `exit [code]` | Terminates the shell process directly |
| `help` | Prints usage information |

Returns `1` if handled (caller stops), `0` if not a built-in (caller continues to pipe/redirect/fork).

#### Part 3 — `execute_pipe()`

Runs two commands connected by `|`. The left command's stdout feeds directly into the right command's stdin through a kernel pipe buffer.

```
ls -la | grep .c

fork child 1 (ls):
    dup2(pipefd[1], STDOUT_FILENO)  → stdout now writes to pipe
    execvp("ls", ...)

fork child 2 (grep):
    dup2(pipefd[0], STDIN_FILENO)   → stdin now reads from pipe
    execvp("grep", ...)

parent:
    close(pipefd[0]) and close(pipefd[1])  ← MUST close both ends
    waitpid(pid1) + waitpid(pid2)
```

**Why the parent must close the pipe:** If the parent keeps `pipefd[1]` open, `grep` waits forever for more data — it only gets `EOF` when every write end of the pipe is closed.

Result: the `CmdResult` of the **right** command (the last in the pipeline) is returned.

#### Part 4 — `execute_with_redirect()`

Runs a command with stdin or stdout wired to a file.

```
ls > out.txt    → open(out.txt, O_WRONLY|O_CREAT|O_TRUNC)
ls >> out.txt   → open(out.txt, O_WRONLY|O_CREAT|O_APPEND)
sort < in.txt   → open(in.txt,  O_RDONLY)
```

`dup2(fd, STDOUT_FILENO)` replaces file descriptor 1 (stdout) with the file's fd. After this, anything the command writes to stdout goes to the file — the command has no idea.

`parse_redirects()` is called beforehand to strip `>`, `>>`, `<`, and their filenames from `args[]` — otherwise `execvp()` would pass them as literal arguments to the command.

#### Part 5 — `execute_command()` (the dispatcher)

The only function `main.c` and `runner.c` call directly. Looks at `args[]` and routes to the right handler:

```
Step 1: handle_builtin(args)     → if returns 1, done
Step 2: find_pipe(args)          → if found, execute_pipe()
Step 3: parse_redirects(args)    → if found, execute_with_redirect()
Step 4: fork() + execvp()        → plain command
```

For plain commands, stdin is redirected to `/dev/null` in the child process. This prevents the child from accidentally reading the script file when running in script mode.

---

### `runner.c`

**Role:** Reads a script file line by line, manages per-command timeouts, handles `Ctrl+C` gracefully, and prints a summary report at the end.

#### Global variables

```c
static volatile pid_t g_current_child = -1;  // PID of running command
static volatile pid_t g_timeout_child = -1;  // PID being watched for timeout
int                   g_timeout_secs  = 10;  // timeout in seconds (extern in main.c)
```

`volatile` tells the compiler: **do not cache this variable in a register**. Signal handlers run asynchronously and can modify these at any time — the compiler must always read the real value from RAM.

#### Part 1 — `RunStats` struct

Tracks the running tally of all commands executed in the script:

```c
typedef struct {
    int total;
    int passed;
    int failed;
    char failed_cmds[20][MAX_INPUT];  // up to 20 failed command names
} RunStats;
```

Populated during `run_script()`, printed as a summary at the end.

#### Part 2 — `sigint_handler()` — Ctrl+C

```
Ctrl+C pressed
    │
    ├── g_current_child > 0  →  kill(child, SIGINT)  →  command dies, shell lives
    └── g_current_child <= 0 →  print "no command running" + re-show prompt
```

Uses `write()` instead of `printf()` — `printf()` is **not async-signal-safe**. If a signal interrupts `printf()` mid-execution and the handler calls `printf()` again, the result is undefined behavior (deadlock or corruption). `write()` is a direct syscall and is safe.

#### Part 3 — `sigalrm_handler()` — Timeout

```
alarm(N) set  →  N seconds pass  →  OS sends SIGALRM  →  sigalrm_handler() runs
                                                                   │
                                                         kill(child, SIGKILL)
```

Uses `SIGKILL` instead of `SIGINT`:
- `SIGINT` can be caught or ignored by the target process
- `SIGKILL` **cannot** be caught, blocked, or ignored — the OS terminates the process unconditionally

This ensures even a deadlocked or infinite-looping command is killed.

#### Part 4 — `setup_signals()`

Registers handlers with the OS using `sigaction()` (more portable and controllable than `signal()`).

`SA_RESTART` flag: automatically restarts interrupted system calls (like `fgets()`). Without it, `Ctrl+C` during a `fgets()` call returns `EINTR` and breaks the REPL loop.

`SIGPIPE` is set to `SIG_IGN` (ignore). Without this, writing to a closed pipe (e.g., when the right side of a pipe exits early) would crash the shell with an unhandled `SIGPIPE`.

#### Part 5 — `run_with_timeout()`

Wraps `execute_command()` with a countdown timer:

```
g_timeout_child = 0       ← signal: a command is starting
alarm(g_timeout_secs)     ← start countdown
execute_command(args)     ← run the command
alarm(0)                  ← cancel countdown (command finished in time)
g_timeout_child = -1      ← signal: no command running
```

#### Part 6 — `run_script()`

Main loop:

```
fopen(filename)
while fgets(line):
    skip blank lines and # comments
    tokenize the line
    run_with_timeout(args, line_num)
    if success: stats.passed++
    else:       stats.failed++, save to failed_cmds[]
                if --stop: break
fclose()
print summary report
```

`FD_CLOEXEC` is set on the script file descriptor — the OS automatically closes it in any child process after `fork()`/`exec()`. This prevents child processes from accidentally inheriting and reading from the script file.

---

### `error_handler.c`

**Role:** Called after every `waitpid()`. Decodes the exit status, prints a colored message to the terminal, and appends a timestamped entry to `shell_errors.log`.

#### Part 1 — Signal table

A static lookup table mapping signal numbers to human-readable names and descriptions:

```c
static SignalInfo signal_table[] = {
    { SIGSEGV, "SIGSEGV", "Segmentation fault — invalid memory access" },
    { SIGFPE,  "SIGFPE",  "Floating point exception — divide by zero"  },
    { SIGKILL, "SIGKILL", "Killed by system or timeout"                },
    ...
    { -1, NULL, NULL }   // sentinel: marks end of table
};
```

Linux provides `strsignal()` which returns English descriptions. This project uses a custom table to provide Vietnamese descriptions with additional context — more useful for Vietnamese-speaking developers.

#### Part 2 — Colored terminal output

Two internal functions, not visible outside this file (`static`):

```
report_crash()       → RED + BOLD  : signal name, number, description
report_exit_error()  → YELLOW      : exit code warning
                        (skips code 127 — execvp already printed "command not found")
```

ANSI color codes:
```
\033[31m  = red
\033[33m  = yellow
\033[1m   = bold
\033[0m   = reset to default
```

Output goes to `stderr` (`fprintf(stderr, ...)`), not `stdout`. This keeps error messages separate from normal command output — important when redirecting stdout to a file.

#### Part 3 — `log_error()`

Appends one line to `shell_errors.log`:

```
[2026-05-15 10:30:45] CRASH | cmd='./buggy' | line=5 | SIGSEGV | Segmentation fault
```

- `fopen(LOG_FILE, "a")` — `"a"` mode appends to the end, never overwrites
- If the file cannot be opened, the function prints a warning and returns — it **does not crash the shell**
- `time()` + `strftime()` format the current timestamp

#### Part 4 — `check_and_report()` (public API)

The only function from this file that other modules call. Immediately after every `waitpid()`:

```c
int check_and_report(const char *cmd_name, int status, int line_num)
```

`status` is a packed integer from `waitpid()` — you cannot read it directly. Use macros:

| Macro | Meaning |
|---|---|
| `WIFEXITED(status)` | Did the process exit normally? |
| `WEXITSTATUS(status)` | If yes, what was the exit code? |
| `WIFSIGNALED(status)` | Was the process killed by a signal? |
| `WTERMSIG(status)` | If yes, which signal number? |
| `WIFSTOPPED(status)` | Was the process stopped (paused)? |

Returns `0` on success, `-1` on any error.

---

## How to Build

```bash
gcc main.c shell.c runner.c error_handler.c -o shell
```

No external libraries required — only standard C and POSIX headers.

---

## How to Run

### Interactive mode

```bash
./shell
```

```
/home/user/myshell myshell> ls
/home/user/myshell myshell> cd /tmp
/tmp myshell> pwd
/tmp
/tmp myshell> exit
Goodbye!
```

### Script mode

```bash
./shell script.sh                    # run all lines, continue on error
./shell script.sh --stop             # stop immediately on first failure
./shell script.sh --timeout 5        # kill any command running over 5 seconds
./shell script.sh --stop --timeout 3 # combine both options
```

---

## Test Cases

Run the included test script to verify all features:

```bash
./shell test_myshell.sh
```

| Group | Tests |
|---|---|
| GROUP 1 | Normal commands: `ls`, `echo`, `date`, `uname` |
| GROUP 2 | Built-ins: `cd`, `pwd` |
| GROUP 3 | Output redirect `>` — create and overwrite file |
| GROUP 4 | Append redirect `>>` — add to file without erasing |
| GROUP 5 | Input redirect `<` — read from file instead of keyboard |
| GROUP 6 | Pipe `\|` — chain commands together |
| GROUP 7 | Non-existent command → exit code 127 |
| GROUP 8 | Failing command → exit code ≠ 0, logged to file |

Expected result: **41 passed, 5 failed** (the 5 failures are intentional test cases for error handling).

---

## Error Logging

Every failed command is appended to `shell_errors.log` automatically:

```
[2026-05-15 08:23:30] CRASH | cmd='./buggy'   | line=12 | SIGSEGV    | Segmentation fault
[2026-05-15 08:23:31] CRASH | cmd='ls'         | line=18 | EXIT_ERROR | Non-zero exit code
[2026-05-15 08:23:31] CRASH | cmd='grep'       | line=22 | EXIT_ERROR | Non-zero exit code
```

View the log:
```bash
cat shell_errors.log
```

The log accumulates across multiple runs — each new session appends to the existing file. Delete it to start fresh:
```bash
rm shell_errors.log
```

---

## Signal Reference

| Signal | Number | Trigger | Handler |
|---|---|---|---|
| `SIGINT` | 2 | `Ctrl+C` | Kill current command only, shell stays alive |
| `SIGALRM` | 14 | `alarm()` timeout | Kill current command with `SIGKILL` |
| `SIGPIPE` | 13 | Write to closed pipe | Ignored — prevents shell crash |
| `SIGSEGV` | 11 | Invalid memory access | Reported by error_handler, logged |
| `SIGFPE` | 8 | Divide by zero | Reported by error_handler, logged |
| `SIGKILL` | 9 | Timeout kill | Cannot be caught or ignored |
