#include "vga.h"
#include "io.h"
#include "serial.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "timer.h"
#include "ata.h"
#include "fs.h"
#include "memory.h"
#include "paging.h"
#include "util.h"
#include "task.h"
#include "elf.h"
#include "syscall.h"

extern task_t tasks[];
extern int task_count;
extern int current_task;

/* ---------- SHELL HELPERS ---------- */

/* Half a page per PgUp/PgDn, one line per arrow, which is the usual pairing. */
#define SHELL_SCROLL_PAGE 12

void shell_readline(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = keyboard_getchar();

        /* PgUp/PgDn and the arrows drive the scrollback instead of the line
         * buffer. Arrows are not used for editing, so nothing is given up.
         * Home/End jump to the oldest retained line and back to the prompt. */
        if (c == KEY_PGUP || c == KEY_UP) {
            vga_scroll_up(c == KEY_UP ? 1 : SHELL_SCROLL_PAGE);
            continue;
        }
        if (c == KEY_PGDN || c == KEY_DOWN) {
            vga_scroll_down(c == KEY_DOWN ? 1 : SHELL_SCROLL_PAGE);
            continue;
        }
        if (c == KEY_HOME) { vga_scroll_top(); continue; }
        if (c == KEY_END)  { vga_scroll_bottom(); continue; }

        /* Any other key means the user is done reading and wants to type, so
         * bring the live prompt back before echoing into the line. */
        if (vga_scrollback_view() > 0) vga_scroll_bottom();

        if (c == '\n') { vga_putchar('\n'); break; }
        if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } continue; }
        buf[i++] = c;
        vga_putchar(c);
    }
    buf[i] = '\0';
}

static int shell_parse(char *line, char **argv, int max_args) {
    int argc = 0;
    while (*line && argc < max_args - 1) {
        while (*line == ' ') line++;
        if (!*line) break;
        argv[argc++] = line;
        while (*line && *line != ' ') line++;
        if (*line) *line++ = '\0';
    }
    argv[argc] = 0;
    return argc;
}

int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ---------- CPU DETECTION ---------- */
void cpuid(unsigned int code, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code));
}

void get_cpu_name(char *out) {
    unsigned int *p = (unsigned int*)out;
    cpuid(0x80000002, p, p+1, p+2, p+3);
    cpuid(0x80000003, p+4, p+5, p+6, p+7);
    cpuid(0x80000004, p+8, p+9, p+10, p+11);
    out[48] = '\0';
}

/* ---------- COMMANDS ---------- */
typedef struct {
    const char *name;
    const char *help;
    void (*func)(int argc, char **argv);
} command_t;

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_cpuinfo(int argc, char **argv);
static void cmd_color(int argc, char **argv);
static void cmd_date(int argc, char **argv);
static void cmd_ver(int argc, char **argv);
static void cmd_calc(int argc, char **argv);
static void cmd_crash(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_sleep(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_meminfo(int argc, char **argv);
static void cmd_memtest(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_kill(int argc, char **argv);
static void cmd_sem_test(int argc, char **argv);
static void cmd_mutex_test(int argc, char **argv);
static void cmd_task_test(int argc, char **argv);
static void cmd_exec(int argc, char **argv);
static void cmd_elfinfo(int argc, char **argv);

static void shell_putchar(char c);
static void shell_print(const char *s);
static void shell_print_color(const char *s, int color);

static command_t commands[] = {
    {"help",    "Show available commands",              cmd_help},
    {"clear",   "Clear the screen",                    cmd_clear},
    {"echo",    "Print arguments to screen",           cmd_echo},
    {"cpuinfo", "Show CPU information",                cmd_cpuinfo},
    {"color",   "Set text color (0-F)",                 cmd_color},
    {"date",    "Show build date",                     cmd_date},
    {"ver",     "Show OS version",                     cmd_ver},
    {"calc",    "Simple calculator: calc X + Y",        cmd_calc},
    {"crash",   "Trigger a divide-by-zero exception",   cmd_crash},
    {"uptime",  "Show system uptime",                   cmd_uptime},
    {"sleep",   "Sleep for N milliseconds",             cmd_sleep},
    {"ls",      "List files on FAT12 disk",             cmd_ls},
    {"cat",     "Show file contents: cat <file>",       cmd_cat},
    {"touch",   "Create empty file: touch <file>",      cmd_touch},
    {"rm",      "Delete file: rm <file>",               cmd_rm},
    {"write",   "Write to file: write <file> <text>",   cmd_write},
    {"meminfo", "Show memory and paging info",          cmd_meminfo},
    {"memtest", "Test virtual memory read/write",       cmd_memtest},
    {"ps",      "List running tasks",                   cmd_ps},
    {"kill",    "Kill a task: kill <tid>",              cmd_kill},
    {"semtest", "Test semaphores",                      cmd_sem_test},
    {"mutest",  "Test mutexes",                         cmd_mutex_test},
    {"tasktest","Test task creation",                   cmd_task_test},
    {"exec",    "Run ELF program: exec <file> [args...]", cmd_exec},
    {"run",     "Alias for exec",                        cmd_exec},
    {"elfinfo", "Show ELF header: elfinfo <file>",        cmd_elfinfo},
    {"reboot",  "Reboot the system",                   cmd_reboot},
    {0, 0, 0}
};

static void cmd_help(int argc, char **argv) {
    shell_print_color("MyOS Command Reference\n", 0x0F);
    shell_print("--------------------\n");
    for (int i = 0; commands[i].name; i++) {
        shell_print_color(commands[i].name, 0x0A);
        shell_print(" - ");
        shell_print(commands[i].help);
        shell_putchar('\n');
    }
    shell_print("\nKeys: PgUp/PgDn or Up/Down scroll back through earlier output,\n");
    shell_print("Home/End jump to the oldest line and back. Any other key returns.\n");
}

static void cmd_clear(int argc, char **argv) { vga_clear(); }

static void cmd_reboot(int argc, char **argv) {
    shell_print("Rebooting...\n");
    for (volatile int i = 0; i < 1000000; i++);
    outb(0x64, 0xFE);
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) shell_putchar(' ');
        shell_print(argv[i]);
    }
    shell_putchar('\n');
}

static int redir_fd = -1;
static int redir_append = 0;

/* Redirected output is collected here and handed to the filesystem in
 * whole chunks: one fs_write per character would rewrite the same sector
 * (and the directory entry) over and over. */
#define REDIR_BUF_SIZE 256
static char redir_buf[REDIR_BUF_SIZE];
static uint32_t redir_len = 0;

static void redir_flush(void) {
    if (redir_fd < 0 || redir_len == 0) return;
    int n = redir_append ? fs_append(redir_fd, redir_buf, redir_len)
                         : fs_write(redir_fd, redir_buf, redir_len);
    if (n < 0)
        shell_print("[error: cannot write redirected output to disk]\n");
    redir_len = 0;
}

static void redir_put(const char *s, uint32_t n) {
    while (n > 0 && redir_fd >= 0) {
        uint32_t room = REDIR_BUF_SIZE - redir_len;
        uint32_t take = n < room ? n : room;
        for (uint32_t i = 0; i < take; i++) redir_buf[redir_len++] = s[i];
        n -= take;
        if (redir_len == REDIR_BUF_SIZE) redir_flush();
    }
}

static void shell_putchar(char c) {
    if (redir_fd >= 0) {
        redir_put(&c, 1);
    } else {
        vga_putchar(c);
    }
}

static void shell_print(const char *s) {
    if (redir_fd >= 0) {
        redir_put(s, strlen(s));
    } else {
        vga_print(s);
    }
}

static void shell_print_color(const char *s, int color) {
    if (redir_fd >= 0) {
        redir_put(s, strlen(s));
    } else {
        vga_print_color(s, color);
    }
}

static void cmd_cpuinfo(int argc, char **argv) {
    shell_print_color("CPU Information\n", 0x0F);
    shell_print("---------------\n");
    char name[49];
    get_cpu_name(name);
    shell_print("CPU: ");
    shell_print_color(name, 0x0A);
    shell_putchar('\n');
    unsigned int a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    shell_print("Family: "); print_dec((a >> 8) & 0xF); shell_putchar('\n');
    shell_print("Model:  "); print_dec((a >> 4) & 0xF); shell_putchar('\n');
    shell_print("Stepping: "); print_dec(a & 0xF); shell_putchar('\n');
}

static void cmd_color(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: color <hex value 0-F>\n");
        shell_print("0=Black 1=Blue 2=Green 3=Cyan 4=Red 5=Magenta 6=Brown 7=White\n");
        shell_print("8=DarkGray 9=LightBlue A=LightGreen B=LightCyan C=LightRed D=Pink E=Yellow F=BrightWhite\n");
        return;
    }
    char c = argv[1][0];
    int color;
    if (c >= '0' && c <= '9') color = c - '0';
    else if (c >= 'A' && c <= 'F') color = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') color = c - 'a' + 10;
    else { shell_print("Invalid color.\n"); return; }
    vga_set_color(color);
    vga_clear();
}

static void cmd_date(int argc, char **argv) {
    shell_print("Build date: " __DATE__ " " __TIME__ "\n");
}

static void cmd_ver(int argc, char **argv) {
    shell_print_color("MyOS v1.3\n", 0x0F);
    shell_print("A custom operating system built from scratch.\n");
    shell_print("Now with paging and a kernel heap.\n");
}

static void cmd_calc(int argc, char **argv) {
    if (argc < 4) {
        shell_print("Usage: calc <num> <+|-|*|/> <num>\n");
        return;
    }
    int a = 0, b = 0;
    for (int i = 0; argv[1][i]; i++) a = a * 10 + (argv[1][i] - '0');
    for (int i = 0; argv[3][i]; i++) b = b * 10 + (argv[3][i] - '0');
    int result = 0;
    char op = argv[2][0];
    if (op == '+') result = a + b;
    else if (op == '-') result = a - b;
    else if (op == '*') result = a * b;
    else if (op == '/') {
        if (b == 0) { shell_print("Division by zero!\n"); return; }
        result = a / b;
    }
    print_dec(a); shell_putchar(' '); shell_putchar(op); shell_putchar(' ');
    print_dec(b); shell_print(" = "); print_dec(result); shell_putchar('\n');
}

static void cmd_crash(int argc, char **argv) {
    shell_print("Triggering divide by zero...\n");
    asm volatile("mov $0, %%eax; div %%eax" : : : "eax");
}

static void cmd_uptime(int argc, char **argv) {
    unsigned int t = timer_get_ticks();
    unsigned int seconds = t / 100;
    unsigned int minutes = seconds / 60;
    unsigned int hours = minutes / 60;
    shell_print("Uptime: ");
    print_dec(hours); shell_print("h ");
    print_dec(minutes % 60); shell_print("m ");
    print_dec(seconds % 60); shell_print("s (");
    print_dec(t); shell_print(" ticks)\n");
}

static void cmd_sleep(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: sleep <milliseconds>\n");
        return;
    }
    int ms = 0;
    for (int i = 0; argv[1][i]; i++) ms = ms * 10 + (argv[1][i] - '0');
    shell_print("Sleeping for ");
    print_dec(ms);
    shell_print(" ms...\n");
    timer_sleep(ms);
    shell_print("Done!\n");
}

static void cmd_ls(int argc, char **argv) {
    shell_print_color("FAT12 Root Directory:\n", 0x0F);
    fs_list();
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: cat <file>\n");
        return;
    }
    fs_cat(argv[1]);
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: touch <file>\n");
        return;
    }
    int fd = fs_create(argv[1]);
    if (fd < 0) {
        shell_print("Failed to create file: ");
        shell_print(argv[1]);
        shell_putchar('\n');
    } else {
        shell_print("Created: ");
        shell_print(argv[1]);
        shell_putchar('\n');
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: rm <file>\n");
        return;
    }
    int result = fs_delete(argv[1]);
    if (result < 0) {
        shell_print("File not found: ");
        shell_print(argv[1]);
        shell_putchar('\n');
    } else {
        shell_print("Deleted: ");
        shell_print(argv[1]);
        shell_putchar('\n');
    }
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        shell_print("Usage: write <file> <text>\n");
        return;
    }
    int fd = fs_open(argv[1], 0);
    if (fd < 0) {
        fd = fs_create(argv[1]);
        if (fd < 0) {
            shell_print("Failed to create file: ");
            shell_print(argv[1]);
            shell_putchar('\n');
            return;
        }
    }

    uint32_t total_len = 0;
    for (int i = 2; i < argc; i++) {
        total_len += strlen(argv[i]);
        if (i < argc - 1) total_len++;
    }

    char *buf = kmalloc(total_len + 1);
    if (!buf) {
        shell_print("Out of memory\n");
        return;
    }

    char *p = buf;
    for (int i = 2; i < argc; i++) {
        for (int j = 0; argv[i][j]; j++) *p++ = argv[i][j];
        if (i < argc - 1) *p++ = ' ';
    }
    *p = '\0';

    int written = fs_write(fd, buf, total_len);
    kfree(buf);

    if (written < 0) {
        shell_print("Write failed\n");
    } else {
        shell_print("Wrote ");
        print_dec(written);
        shell_print(" bytes to ");
        shell_print(argv[1]);
        shell_putchar('\n');
    }
}

#define MEMTEST_VADDR 0x40000000

static void cmd_meminfo(int argc, char **argv) {
    shell_print_color("Memory Information\n", 0x0F);
    shell_print("-------------------\n");
    shell_print("Physical memory: ");
    print_dec(PHYS_MAX_MB);
    shell_print(" MB total, ");
    print_dec(frame_count_free());
    shell_print(" frames free (");
    print_dec(frame_count_free() * FRAME_SIZE);
    shell_print(" bytes)\n");
    shell_print("Kernel heap: ");
    print_dec(kmalloc_used());
    shell_print(" / ");
    print_dec(HEAP_SIZE);
    shell_print(" bytes used\n");
    shell_print("Paging: ");
    if (paging_state()) shell_print_color("ENABLED\n", 0x0A);
    else shell_print("disabled\n");
}

static void cmd_memtest(int argc, char **argv) {
    shell_print("Allocating a physical frame...\n");
    uint32_t phys = frame_alloc();
    if (!phys) { shell_print("No free frames!\n"); return; }
    shell_print("Mapping frame at virtual 0x40000000...\n");
    map_page(MEMTEST_VADDR, phys, PAGE_PRESENT | PAGE_WRITABLE);
    volatile uint8_t *p = (volatile uint8_t*)MEMTEST_VADDR;
    int ok = 1;
    for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) if (p[i] != (uint8_t)i) ok = 0;
    shell_print("virt 0x40000000 -> phys ");
    print_hex(virt_to_phys(MEMTEST_VADDR));
    shell_putchar('\n');
    if (ok) shell_print_color("PASS: 256 bytes verified via virtual page\n", 0x0A);
    else shell_print_color("FAIL: data mismatch\n", 0x0C);
    unmap_page(MEMTEST_VADDR);
    frame_free(phys);
}

static void cmd_ps(int argc, char **argv) {
    shell_print_color("Task List\n", 0x0F);
    shell_print("TID  State        Priority  Name\n");
    shell_print("---  -----        --------  ----\n");
    for (int i = 0; i < task_count; i++) {
        const char *state_str = "UNKNOWN";
        switch (tasks[i].state) {
            case TASK_RUNNING: state_str = "RUNNING"; break;
            case TASK_READY:   state_str = "READY  "; break;
            case TASK_BLOCKED: state_str = "BLOCKED"; break;
            case TASK_SLEEPING:state_str = "SLEEP  "; break;
            case TASK_FINISHED:state_str = "FINISH "; break;
        }
        shell_print(" ");
        print_dec(tasks[i].tid);
        shell_print("  ");
        shell_print(state_str);
        shell_print("  ");
        print_dec(tasks[i].priority);
        shell_print("        ");
        shell_print(tasks[i].name);
        shell_putchar('\n');
    }
}

static void cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: kill <tid>\n");
        return;
    }
    int tid = 0;
    for (int i = 0; argv[1][i]; i++) tid = tid * 10 + (argv[1][i] - '0');
    if (tid >= 0 && tid < task_count && tid != current_task) {
        tasks[tid].state = TASK_FINISHED;
        shell_print("Killed task ");
        print_dec(tid);
        shell_putchar('\n');
    } else {
        shell_print("Invalid task ID\n");
    }
}

static void task_a() {
    int sem = sem_create(0);
    for (int i = 0; i < 5; i++) {
        sem_wait(sem);
        shell_print("Task A got semaphore\n");
    }
    sem_destroy(sem);
    task_exit();
}

static void task_b() {
    int sem = sem_create(0);
    for (int i = 0; i < 5; i++) {
        sem_wait(sem);
        shell_print("Task B got semaphore\n");
    }
    sem_destroy(sem);
    task_exit();
}

static void task_c() {
    int sem = sem_create(0);
    for (int i = 0; i < 5; i++) {
        sem_signal(sem);
        shell_print("Task C signaled semaphore\n");
        task_sleep(50);
    }
    sem_destroy(sem);
    task_exit();
}

static void cmd_sem_test(int argc, char **argv) {
    shell_print("Starting semaphore test...\n");
    int sem = sem_create(0);
    task_create(task_a, "TaskA", 10);
    task_create(task_b, "TaskB", 10);
    task_create(task_c, "TaskC", 5);
    for (int i = 0; i < 5; i++) {
        sem_signal(sem);
        task_sleep(50);
    }
    sem_destroy(sem);
    shell_print("Semaphore test done\n");
}

static void mutex_task(int mutex_id, const char *name) {
    for (int i = 0; i < 3; i++) {
        mutex_lock(mutex_id);
        shell_print("["); shell_print(name); shell_print("] locked\n");
        task_sleep(20);
        mutex_unlock(mutex_id);
        shell_print("["); shell_print(name); shell_print("] unlocked\n");
        task_sleep(10);
    }
    task_exit();
}

static void cmd_mutex_test(int argc, char **argv) {
    shell_print("Starting mutex test...\n");
    int m = mutex_create();
    task_create((void(*)())mutex_task, "MutexT1", 10);
    task_create((void(*)())mutex_task, "MutexT2", 10);
    task_sleep(200);
    mutex_destroy(m);
    shell_print("Mutex test done\n");
}

static void child_task() {
    shell_print("Child task running\n");
    task_sleep(30);
    shell_print("Child task done\n");
    task_exit();
}

static void parent_task() {
    shell_print("Parent creating child\n");
    task_create(child_task, "Child", 5);
    shell_print("Parent waiting...\n");
    task_sleep(50);
    shell_print("Parent done\n");
    task_exit();
}

static void cmd_task_test(int argc, char **argv) {
    shell_print("Starting task creation test...\n");
    task_create(parent_task, "Parent", 10);
    task_sleep(100);
    shell_print("Task test done\n");
}

/* ---------- ELF EXECUTION ---------- */
static void cmd_exec(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: exec <file> [args...]\n");
        shell_print("Example: exec HELLO.ELF foo bar\n");
        return;
    }
    const char *path = argv[1];
    int user_argc = argc - 1;
    char **user_argv = &argv[1];

    shell_print("Loading ");
    shell_print(path);
    shell_print(" ...\n");

    int rc = elf_exec(path, user_argc, user_argv);
    if (rc < 0) {
        shell_print_color("exec failed: ", 0x0C);
        if (rc == -30) shell_print("file not found\n");
        else {
            shell_print("ELF load error ");
            /* print signed code */
            if (rc < 0) { shell_putchar('-'); print_dec((unsigned int)(-rc)); }
            else print_dec((unsigned int)rc);
            shell_putchar('\n');
            shell_print("(not a valid 32-bit i386 EXEC in user space? try elfinfo)\n");
        }
        return;
    }
    shell_print("Program exited with code ");
    if (rc < 0) { shell_putchar('-'); print_dec((unsigned int)(-rc)); }
    else print_dec((unsigned int)rc);
    shell_putchar('\n');
}

static void cmd_elfinfo(int argc, char **argv) {
    if (argc < 2) {
        shell_print("Usage: elfinfo <file>\n");
        return;
    }
    int fd = fs_open(argv[1], 0);
    if (fd < 0) {
        shell_print("File not found: ");
        shell_print(argv[1]);
        shell_putchar('\n');
        return;
    }
    uint32_t sz = fs_size(fd);
    shell_print("File: ");
    shell_print(argv[1]);
    shell_print("  size=");
    print_dec(sz);
    shell_putchar('\n');
    if (sz == 0 || sz > 256 * 1024) {
        shell_print("File too large for elfinfo preview.\n");
        return;
    }
    char *buf = kmalloc(sz);
    if (!buf) { shell_print("Out of memory\n"); return; }
    if (fs_read(fd, buf, sz) < 0) {
        shell_print("Read failed\n");
        kfree(buf);
        return;
    }
    int v = elf_check((uint8_t *)buf, sz);
    if (v == 0) shell_print_color("Valid 32-bit i386 EXEC for user space.\n", 0x0A);
    else {
        shell_print_color("Invalid ELF (check code ", 0x0C);
        if (v < 0) { shell_putchar('-'); print_dec((unsigned int)(-v)); }
        else print_dec((unsigned int)v);
        shell_print(").\n");
    }
    elf_info((uint8_t *)buf, sz);
    kfree(buf);
}

/* Try to run INIT.ELF once at boot as the system init task. */
static void run_init_task(void) {
    int fd = fs_open("INIT.ELF", 0);
    if (fd < 0)
        fd = fs_open("INIT", 0);
    if (fd < 0)
        return;
    shell_print("[INIT] Found INIT.ELF, running init task...\n");
    char *args[] = { "INIT.ELF", 0 };
    int rc = elf_exec("INIT.ELF", 1, args);
    if (rc < 0) {
        /* Fall back to INIT without extension */
        rc = elf_exec("INIT", 1, args);
    }
    if (rc < 0) {
        shell_print("[INIT] init task failed to load.\n");
    } else {
        shell_print("[INIT] init task exited with code ");
        print_dec((unsigned int)rc);
        shell_putchar('\n');
    }
}

/* ---------- MAIN ---------- */
__attribute__((section(".text.start")))
void kernel_main() {
    extern char _bss_start[];
    extern char _bss_end[];

    memset(_bss_start, 0, (uint32_t)(_bss_end - _bss_start));

    /* Before the first vga_print: serial_putc() is a no-op until serial_init()
     * has found a UART, so without this the COM1 mirror never runs and
     * "-serial stdio" stays silent. Initialising here also means the boot
     * banner reaches the terminal, which gives the session a scrollback far
     * larger than the 25-line screen. */
    serial_init();

    vga_disable_cursor();
    vga_set_color(0x0F);
    vga_clear();
    vga_print_color("Welcome to MyOS v1.3\n", 0x0F);
    vga_set_color(0x07);
    vga_print("Booting subsystems...\n\n");

    memory_init();
    paging_init();
    tasking_init();
    idt_init();
    syscall_init();
    keyboard_init();
    timer_init(100);
    ata_init();
    fs_init();

    run_init_task();

    vga_print("\nType 'help' for a list of commands.\n\n");

    char buf[128];
    char *argv[16];

    while (1) {
        vga_print_color("myos> ", 0x0A);
        shell_readline(buf, 128);
        if (!buf[0]) continue;
        int argc = shell_parse(buf, argv, 16);
        if (!argc) continue;

        /* Handle redirection: scan argv for > or >> */
        int redir_idx = -1;
        for (int i = 1; i < argc; i++) {
            if (str_eq(argv[i], ">") || str_eq(argv[i], ">>")) {
                redir_idx = i;
                break;
            }
        }

        if (redir_idx >= 0 && redir_idx + 1 < argc) {
            int append = str_eq(argv[redir_idx], ">>");
            const char *fname = argv[redir_idx + 1];

            int fd = fs_open(fname, 0);
            if (fd < 0) {
                fd = fs_create(fname);
            }
            if (fd >= 0) {
                redir_append = append;
                redir_fd = fd;
            } else {
                vga_print("[error: cannot open ");
                vga_print(fname);
                vga_print(" for writing]\n");
            }

            argc = redir_idx;
            argv[argc] = 0;
        } else {
            redir_fd = -1;
        }

        int found = 0;
        for (int i = 0; commands[i].name; i++) {
            if (str_eq(argv[0], commands[i].name)) {
                commands[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            /* Implicit exec: if the word matches a file on disk, run it
             * as an ELF program with CLI args (argv[0]=file). */
            if (fs_open(argv[0], 0) >= 0) {
                int rc = elf_exec(argv[0], argc, argv);
                if (rc < 0) {
                    shell_print_color("exec failed for ", 0x0C);
                    shell_print(argv[0]);
                    shell_putchar('\n');
                } else {
                    shell_print("Program exited with code ");
                    print_dec((unsigned int)rc);
                    shell_putchar('\n');
                }
            } else {
                shell_print_color("Unknown command: ", 0x0C);
                shell_print(argv[0]);
                shell_putchar('\n');
            }
        }

        if (redir_fd >= 0) {
            redir_flush();
            redir_fd = -1;
            redir_append = 0;
        }
    }
}