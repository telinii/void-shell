#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <glob.h>
#include <time.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <termios.h>
#include <strings.h>

#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_TOKEN 4096

static void anim_clear_screen(void);
static void anim_reapply_region(void);
static void kalc_run(const char *start);

static int last_status = 0;

#define MAX_JOBS 64
typedef struct {
    int id;
    pid_t pid;
    char *cmd;
    int running;
} Job;
static Job jobs[MAX_JOBS];
static int njobs = 0;
static int next_jobid = 1;
static int bg_current = 0;
static char *bg_cmd = NULL;
static pid_t shell_pgid = 0;
static int shell_terminal = -1;

static void reap_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < njobs; i++) {
            if (jobs[i].pid == pid) {
                if (WIFSTOPPED(status)) {
                    jobs[i].running = 2;
                } else {
                    jobs[i].running = 0;
                    if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }
}

static void sigchld_handler(int sig) {
    (void)sig;
    reap_children();
}

typedef struct {
    char *name;
    char *value;
    int exported;
} ShellVar;

static ShellVar shell_vars[256];
static int nshell_vars = 0;

static ShellVar *find_var(const char *name) {
    for (int i = 0; i < nshell_vars; i++) {
        if (strcmp(shell_vars[i].name, name) == 0) return &shell_vars[i];
    }
    return NULL;
}

static void set_var(const char *name, const char *value, int exported) {
    ShellVar *v = find_var(name);
    if (!v) {
        if (nshell_vars >= 256) return;
        v = &shell_vars[nshell_vars++];
        v->name = strdup(name);
        v->value = NULL;
        v->exported = 0;
    }
    free(v->value);
    v->value = strdup(value ? value : "");
    if (exported) {
        v->exported = 1;
        setenv(name, v->value, 1);
    }
}

static const char *get_var(const char *name) {
    ShellVar *v = find_var(name);
    if (v) return v->value;
    return getenv(name);
}

static void unset_var(const char *name) {
    for (int i = 0; i < nshell_vars; i++) {
        if (strcmp(shell_vars[i].name, name) == 0) {
            free(shell_vars[i].name);
            free(shell_vars[i].value);
            for (int j = i; j < nshell_vars - 1; j++) shell_vars[j] = shell_vars[j+1];
            nshell_vars--;
            unsetenv(name);
            return;
        }
    }
}

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
    int append;
} Command;

static int expand_token(const char *token, char **argv, int argc) {
    char tbuf[MAX_TOKEN];
    const char *t = token;
    if (token[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(tbuf, sizeof(tbuf), "%s%s", home, token + 1);
            t = tbuf;
        }
    }
    if (strpbrk(t, "*?[")) {
        glob_t g;
        memset(&g, 0, sizeof(g));
        if (glob(t, GLOB_NOCHECK, NULL, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc && argc < MAX_ARGS - 1; i++) {
                argv[argc++] = strdup(g.gl_pathv[i]);
            }
            globfree(&g);
            return argc;
        }
    }
    argv[argc++] = strdup(t);
    return argc;
}

/* Tokenizer com aspas e expansao de $VAR / $? */
static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        char buf[MAX_TOKEN];
        int bi = 0;
        char quote = 0;
        while (*p && (quote || (*p != ' ' && *p != '\t'))) {
            if (!quote && (*p == '"' || *p == '\'')) {
                quote = *p++;
                continue;
            }
            if (quote && *p == quote) {
                quote = 0;
                p++;
                continue;
            }
            if (*p == '$' && quote != '\'') {
                p++;
                if (*p == '?') {
                    p++;
                    char num[16];
                    snprintf(num, sizeof(num), "%d", last_status);
                    for (char *q = num; *q; q++) {
                        if (bi < MAX_TOKEN - 1) buf[bi++] = *q;
                    }
                } else {
                    char name[64];
                    int ni = 0;
                    while ((isalnum((unsigned char)*p) || *p == '_') && ni < 63) {
                        name[ni++] = *p++;
                    }
                    name[ni] = '\0';
                    const char *val = get_var(name);
                    if (val) {
                        for (const char *q = val; *q; q++) {
                            if (bi < MAX_TOKEN - 1) buf[bi++] = *q;
                        }
                    }
                }
                continue;
            }
            if (bi < MAX_TOKEN - 1) buf[bi++] = *p;
            p++;
        }
        buf[bi] = '\0';
        argc = expand_token(buf, argv, argc);
        if (argc >= MAX_ARGS - 1) break;
    }
    argv[argc] = NULL;
    return argc;
}

static int do_builtin(char **argv) {
    if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    }
    if (strcmp(argv[0], "clear") == 0) {
        anim_clear_screen();
        last_status = 0;
        return 1;
    }
    if (strcmp(argv[0], "cd") == 0) {
        const char *dir = argv[1] ? argv[1] : getenv("HOME");
        if (chdir(dir) != 0) {
            perror("cd");
            last_status = 1;
        } else {
            last_status = 0;
        }
        return 1;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) {
            printf("%s\n", buf);
            last_status = 0;
        } else {
            perror("pwd");
            last_status = 1;
        }
        return 1;
    }
    if (strcmp(argv[0], "jobs") == 0) {
        for (int i = 0; i < njobs; i++) {
            if (jobs[i].running == 2)
                printf("[%d]  Stopped   %s\n", jobs[i].id, jobs[i].cmd);
            else if (jobs[i].running == 1)
                printf("[%d]  Running   %s &\n", jobs[i].id, jobs[i].cmd);
            else
                printf("[%d]  Done      %s\n", jobs[i].id, jobs[i].cmd);
        }
        /* remove done jobs */
        int w = 0;
        for (int i = 0; i < njobs; i++) {
            if (jobs[i].running) jobs[w++] = jobs[i];
            else free(jobs[i].cmd);
        }
        njobs = w;
        last_status = 0;
        return 1;
    }
    if (strcmp(argv[0], "fg") == 0) {
        int id = 0;
        if (argv[1]) {
            if (argv[1][0] == '%') id = atoi(argv[1] + 1);
            else id = atoi(argv[1]);
        } else if (njobs > 0) {
            id = jobs[njobs-1].id;
        }
        for (int i = 0; i < njobs; i++) {
            if (jobs[i].id == id && jobs[i].running) {
                if (jobs[i].running == 2) kill(jobs[i].pid, SIGCONT);
                printf("%s\n", jobs[i].cmd);
                if (shell_terminal >= 0) tcsetpgrp(shell_terminal, jobs[i].pid);
                int status;
                waitpid(jobs[i].pid, &status, WUNTRACED);
                if (shell_terminal >= 0) tcsetpgrp(shell_terminal, shell_pgid);
                if (WIFSTOPPED(status)) {
                    jobs[i].running = 2;
                    printf("\n[%d]+  Stopped                 %s\n", jobs[i].id, jobs[i].cmd);
                    last_status = 128 + WSTOPSIG(status);
                } else {
                    jobs[i].running = 0;
                    if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
                    free(jobs[i].cmd);
                    for (int j = i; j < njobs - 1; j++) jobs[j] = jobs[j+1];
                    njobs--;
                }
                return 1;
            }
        }
        fprintf(stderr, "fg: no such job\n");
        last_status = 1;
        return 1;
    }
    if (strcmp(argv[0], "bg") == 0) {
        int id = 0;
        if (argv[1]) {
            if (argv[1][0] == '%') id = atoi(argv[1] + 1);
            else id = atoi(argv[1]);
        } else if (njobs > 0) {
            id = jobs[njobs-1].id;
        }
        for (int i = 0; i < njobs; i++) {
            if (jobs[i].id == id && jobs[i].running == 2) {
                kill(jobs[i].pid, SIGCONT);
                jobs[i].running = 1;
                printf("[%d] %s &\n", jobs[i].id, jobs[i].cmd);
                last_status = 0;
                return 1;
            }
        }
        fprintf(stderr, "bg: no such job\n");
        last_status = 1;
        return 1;
    }
    if (strcmp(argv[0], "export") == 0) {
        if (argv[1] == NULL) {
            for (int i = 0; i < nshell_vars; i++) {
                if (shell_vars[i].exported)
                    printf("export %s=%s\n", shell_vars[i].name, shell_vars[i].value);
            }
            last_status = 0;
            return 1;
        }
        for (int i = 1; argv[i]; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                set_var(argv[i], eq + 1, 1);
                *eq = '=';
            } else {
                /* mark existing as exported */
                ShellVar *v = find_var(argv[i]);
                if (v) {
                    v->exported = 1;
                    setenv(v->name, v->value, 1);
                } else {
                    set_var(argv[i], "", 1);
                }
            }
        }
        last_status = 0;
        return 1;
    }
    if (strcmp(argv[0], "unset") == 0) {
        for (int i = 1; argv[i]; i++) unset_var(argv[i]);
        last_status = 0;
        return 1;
    }
    if (strcmp(argv[0], "set") == 0) {
        for (int i = 0; i < nshell_vars; i++) {
            printf("%s=%s\n", shell_vars[i].name, shell_vars[i].value);
        }
        last_status = 0;
        return 1;
    }
    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; argv[i]; i++) {
            if (i > 1) printf(" ");
            printf("%s", argv[i]);
        }
        printf("\n");
        last_status = 0;
        return 1;
    }
    return 0;
}

static void parse_command(char *seg, Command *cmd) {
    cmd->argc = 0;
    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->append = 0;

    /* tokeniza segmento respeitando aspas */
    char *argv[MAX_ARGS];
    int n = tokenize(seg, argv);
    /* move redirects para fora */
    int newargc = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(argv[i], "<") == 0 && i + 1 < n) {
            cmd->infile = argv[++i];
        } else if (strcmp(argv[i], ">") == 0 && i + 1 < n) {
            cmd->outfile = argv[++i];
            cmd->append = 0;
        } else if (strcmp(argv[i], ">>") == 0 && i + 1 < n) {
            cmd->outfile = argv[++i];
            cmd->append = 1;
        } else {
            cmd->argv[newargc++] = argv[i];
        }
    }
    cmd->argv[newargc] = NULL;
    cmd->argc = newargc;
}

static void run_external(char **argv, char *infile, char *outfile, int append) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        last_status = 1;
        return;
    }
    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        if (infile) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) { perror("open"); _exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (outfile) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(outfile, flags, 0644);
            if (fd < 0) { perror("open"); _exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "void-shell: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);

    if (bg_current) {
        if (njobs < MAX_JOBS) {
            jobs[njobs].id = next_jobid++;
            jobs[njobs].pid = pid;
            jobs[njobs].cmd = bg_cmd ? strdup(bg_cmd) : strdup(argv[0]);
            jobs[njobs].running = 1;
            njobs++;
            printf("[%d] %d\n", jobs[njobs-1].id, pid);
        }
        last_status = 0;
        return;
    }
    if (shell_terminal >= 0) tcsetpgrp(shell_terminal, pid);
    int status;
    waitpid(pid, &status, WUNTRACED);
    if (shell_terminal >= 0) tcsetpgrp(shell_terminal, shell_pgid);
    if (WIFSTOPPED(status)) {
        if (njobs < MAX_JOBS) {
            jobs[njobs].id = next_jobid++;
            jobs[njobs].pid = pid;
            jobs[njobs].cmd = bg_cmd ? strdup(bg_cmd) : strdup(argv[0]);
            jobs[njobs].running = 2;
            njobs++;
            printf("\n[%d]+  Stopped                 %s\n", jobs[njobs-1].id, jobs[njobs-1].cmd);
        }
        last_status = 128 + WSTOPSIG(status);
    } else {
        if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
    }
}

static void run_pipeline(char *line) {
    char *segs[MAX_CMDS];
    int ncmd = 0;
    char *seg = strtok(line, "|");
    while (seg && ncmd < MAX_CMDS) {
        segs[ncmd++] = seg;
        seg = strtok(NULL, "|");
    }
    if (ncmd == 0) return;

    Command cmds[MAX_CMDS];
    for (int i = 0; i < ncmd; i++) {
        parse_command(segs[i], &cmds[i]);
    }

    int pipes[MAX_CMDS - 1][2];
    for (int i = 0; i < ncmd - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return;
        }
    }

    pid_t pids[MAX_CMDS];
    for (int i = 0; i < ncmd; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return;
        }
        if (pids[i] == 0) {
            if (i == 0) setpgid(0, 0);
            else setpgid(0, pids[0]);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < ncmd - 1) dup2(pipes[i][1], STDOUT_FILENO);
            for (int j = 0; j < ncmd - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd < 0) { perror("open"); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (cmds[i].outfile) {
                int flags = O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC);
                int fd = open(cmds[i].outfile, flags, 0644);
                if (fd < 0) { perror("open"); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            if (cmds[i].argc == 0) _exit(0);
            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "void-shell: %s: %s\n", cmds[i].argv[0], strerror(errno));
            _exit(127);
        }
        setpgid(pids[i], (i == 0 ? pids[i] : pids[0]));
    }

    for (int i = 0; i < ncmd - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    if (bg_current) {
        if (njobs < MAX_JOBS) {
            jobs[njobs].id = next_jobid++;
            jobs[njobs].pid = pids[0];
            jobs[njobs].cmd = bg_cmd ? strdup(bg_cmd) : strdup("pipeline");
            jobs[njobs].running = 1;
            njobs++;
            printf("[%d] %d\n", jobs[njobs-1].id, pids[0]);
        }
        last_status = 0;
        return;
    }
    if (shell_terminal >= 0) tcsetpgrp(shell_terminal, pids[0]);
    int status = 0;
    for (int i = 0; i < ncmd; i++) {
        waitpid(pids[i], &status, WUNTRACED);
    }
    if (shell_terminal >= 0) tcsetpgrp(shell_terminal, shell_pgid);
    if (WIFSTOPPED(status)) {
        if (njobs < MAX_JOBS) {
            jobs[njobs].id = next_jobid++;
            jobs[njobs].pid = pids[0];
            jobs[njobs].cmd = bg_cmd ? strdup(bg_cmd) : strdup("pipeline");
            jobs[njobs].running = 2;
            njobs++;
            printf("\n[%d]+  Stopped                 %s\n", jobs[njobs-1].id, jobs[njobs-1].cmd);
        }
        last_status = 128 + WSTOPSIG(status);
    } else {
        if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
    }
}

static void run_command_str(char *cmd);

static void run_line(char *line);

static int find_matching(const char *s, char open, char close) {
    int depth = 0;
    int in_s = 0, in_d = 0;
    for (int i = 0; s[i]; i++) {
        if (in_s) { if (s[i] == '\'') in_s = 0; continue; }
        if (in_d) { if (s[i] == '"') in_d = 0; continue; }
        if (s[i] == '\'') { in_s = 1; continue; }
        if (s[i] == '"') { in_d = 1; continue; }
        if (s[i] == open) depth++;
        else if (s[i] == close) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static void run_command_str(char *cmd) {
    /* detect trailing & (background) */
    char *c = cmd;
    /* trim trailing spaces */
    int len = strlen(c);
    while (len > 0 && (c[len-1] == ' ' || c[len-1] == '\t')) c[--len] = '\0';
    bg_current = 0;
    bg_cmd = NULL;
    if (len > 0 && c[len-1] == '&') {
        /* ensure not && */
        if (len == 1 || c[len-2] != '&') {
            c[len-1] = '\0';
            len = strlen(c);
            while (len > 0 && (c[len-1] == ' ' || c[len-1] == '\t')) c[--len] = '\0';
            bg_current = 1;
            bg_cmd = c;
        }
    }

    /* trim leading spaces */
    while (*cmd == ' ' || *cmd == '\t') cmd++;

    /* subshell ( ... ) */
    if (cmd[0] == '(') {
        int idx = find_matching(cmd, '(', ')');
        if (idx > 0) {
            char *inner = strndup(cmd + 1, idx - 1);
            if (bg_current) {
                pid_t pid = fork();
                if (pid == 0) {
                    run_line(inner);
                    exit(last_status);
                }
                if (njobs < MAX_JOBS) {
                    jobs[njobs].id = next_jobid++;
                    jobs[njobs].pid = pid;
                    jobs[njobs].cmd = strdup(inner);
                    jobs[njobs].running = 1;
                    njobs++;
                    printf("[%d] %d\n", jobs[njobs-1].id, pid);
                }
                free(inner);
                return;
            }
            pid_t pid = fork();
            if (pid == 0) {
                setpgid(0, 0);
                signal(SIGTTOU, SIG_DFL);
                signal(SIGTTIN, SIG_DFL);
                run_line(inner);
                exit(last_status);
            }
            setpgid(pid, pid);
            if (shell_terminal >= 0) tcsetpgrp(shell_terminal, pid);
            int status;
            waitpid(pid, &status, WUNTRACED);
            if (shell_terminal >= 0) tcsetpgrp(shell_terminal, shell_pgid);
            if (WIFSTOPPED(status)) {
                if (njobs < MAX_JOBS) {
                    jobs[njobs].id = next_jobid++;
                    jobs[njobs].pid = pid;
                    jobs[njobs].cmd = strdup(inner);
                    jobs[njobs].running = 2;
                    njobs++;
                    printf("\n[%d]+  Stopped                 %s\n", jobs[njobs-1].id, jobs[njobs-1].cmd);
                }
                last_status = 128 + WSTOPSIG(status);
            } else {
                if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
            }
            free(inner);
            return;
        }
    }

    /* group { ... } */
    if (cmd[0] == '{') {
        int idx = find_matching(cmd, '{', '}');
        if (idx > 0) {
            char *inner = strndup(cmd + 1, idx - 1);
            run_line(inner);
            free(inner);
            return;
        }
    }

    char *argv[MAX_ARGS];
    int argc = tokenize(cmd, argv);
    if (argc == 0) return;

    /* leading assignments: VAR=value */
    int nassign = 0;
    while (nassign < argc && strchr(argv[nassign], '=') && argv[nassign][0] != '=') {
        char *eq = strchr(argv[nassign], '=');
        *eq = '\0';
        set_var(argv[nassign], eq + 1, 0);
        nassign++;
    }
    if (nassign == argc) {
        return; /* only assignments */
    }
    if (nassign > 0) {
        for (int k = 0; k < nassign; k++) {
            setenv(argv[k], argv[k] + strlen(argv[k]) + 1, 1);
        }
        memmove(argv, argv + nassign, (argc - nassign + 1) * sizeof(char *));
        argc -= nassign;
    }

    /* pipeline (sem assignments) */
    if (nassign == 0 && strchr(cmd, '|')) {
        char *copy = strdup(cmd);
        run_pipeline(copy);
        free(copy);
        return;
    }

    char *infile = NULL, *outfile = NULL;
    int append = 0;
    char *nargv[MAX_ARGS];
    int newargc = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "<") == 0 && i + 1 < argc) {
            infile = argv[++i];
        } else if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            outfile = argv[++i];
            append = 0;
        } else if (strcmp(argv[i], ">>") == 0 && i + 1 < argc) {
            outfile = argv[++i];
            append = 1;
        } else {
            nargv[newargc++] = argv[i];
        }
    }
    nargv[newargc] = NULL;
    if (newargc == 0) return;

    if (do_builtin(nargv)) return;

    run_external(nargv, infile, outfile, append);
}

static void trim_str(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';
}

static void run_line(char *line) {
    /* /kalc: mini manual interativo de matematica (menu + mouse) */
    {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "/kalc", 5) == 0 &&
            (s[5] == '\0' || s[5] == ' ' || s[5] == '\t')) {
            char *arg = s + 5;
            while (*arg == ' ' || *arg == '\t') arg++;
            int alen = (int)strlen(arg);
            while (alen > 0 && (arg[alen - 1] == ' ' || arg[alen - 1] == '\t'))
                arg[--alen] = '\0';
            kalc_run(*arg ? arg : NULL);
            anim_reapply_region();
            last_status = 0;
            return;
        }
    }
    char *cmds[MAX_CMDS];
    int ops[MAX_CMDS];
    int ncmd = 0;
    char buf[MAX_TOKEN * 4];
    int bi = 0;
    int in_s = 0, in_d = 0;
    int depth = 0;
    int next_op = 0; /* 0=SEMI,1=AND,2=OR */
    char *p = line;

    while (*p) {
        if (!in_s && !in_d && depth == 0) {
            if (*p == ';') {
                buf[bi] = '\0';
                trim_str(buf);
                if (buf[0] && ncmd < MAX_CMDS) {
                    cmds[ncmd] = strdup(buf);
                    ops[ncmd] = next_op;
                    ncmd++;
                }
                bi = 0;
                next_op = 0;
                p++;
                while (*p == ' ' || *p == '\t') p++;
                continue;
            }
            if (*p == '&' && p[1] == '&') {
                buf[bi] = '\0';
                trim_str(buf);
                if (buf[0] && ncmd < MAX_CMDS) {
                    cmds[ncmd] = strdup(buf);
                    ops[ncmd] = next_op;
                    ncmd++;
                }
                bi = 0;
                next_op = 1;
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
                continue;
            }
            if (*p == '|' && p[1] == '|') {
                buf[bi] = '\0';
                trim_str(buf);
                if (buf[0] && ncmd < MAX_CMDS) {
                    cmds[ncmd] = strdup(buf);
                    ops[ncmd] = next_op;
                    ncmd++;
                }
                bi = 0;
                next_op = 2;
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
                continue;
            }
        }
        if (!in_s && !in_d) {
            if (*p == '(' || *p == '{') depth++;
            else if (*p == ')' || *p == '}') depth--;
        }
        if (*p == '\'') { in_s = !in_s; buf[bi++] = *p++; continue; }
        if (*p == '"') { in_d = !in_d; buf[bi++] = *p++; continue; }
        buf[bi++] = *p++;
    }
    buf[bi] = '\0';
    trim_str(buf);
    if (buf[0] && ncmd < MAX_CMDS) {
        cmds[ncmd] = strdup(buf);
        ops[ncmd] = next_op;
        ncmd++;
    }

    for (int i = 0; i < ncmd; i++) {
        if (i > 0) {
            if (ops[i] == 1 && last_status != 0) continue;
            if (ops[i] == 2 && last_status == 0) continue;
        }
        run_command_str(cmds[i]);
    }
    for (int i = 0; i < ncmd; i++) free(cmds[i]);
}

static char histfile[4096];

static const char *const galaxy_lines[13] = {
    "                .  *  .  *  .  *  .  *",
    "          .  *  .  *  .  *  .  *  .  *  .",
    "      .  *  .  *  .  *  .  *  .  *  .  *  .",
    "    .  *  .  *  .  *  .  *  .  *  .  *  .  *",
    "  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .",
    " .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .",
    ".  *  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .",
    " .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .",
    "  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .",
    "    .  *  .  *  .  *  .  *  .  *  .  *  .  *",
    "      .  *  .  *  .  *  .  *  .  *  .  *  .",
    "          .  *  .  *  .  *  .  *  .  *  .",
    "                .  *  .  *  .  *  .  *",
};

static const char *const logo_lines[7] = {
    "`7MMF'   `7MF'            db       `7MM       .M\"\"\"bgd `7MM                 `7MM  `7MM  ",
    "  `MA     ,V                         MM      ,MI    \"Y   MM                   MM    MM  ",
    "   VM:   ,V    ,pW\"Wq.  `7MM    ,M\"\"bMM      `MMb.       MMpMMMb.   .gP\"Ya    MM    MM  ",
    "    MM.  M'   6W'   `Wb   MM  ,AP    MM        `YMMNq.   MM    MM  ,M'   Yb   MM    MM  ",
    "    `MM A'    8M     M8   MM  8MI    MM      .     `MM   MM    MM  8M\"\"\"\"\"\"   MM    MM  ",
    "     :MM;     YA.   ,A9   MM  `Mb    MM      Mb     dM   MM    MM  YM.    ,   MM    MM  ",
    "      VF       `Ybmd9'  .JMML. `Wbmd\"MML.    P\"Ybmmd\"  .JMML  JMML. `Mbmmd' .JMML..JMML.",
};

static void print_gradient_raw(const char *s, int offset) {
    int len = (int)strlen(s);
    for (int i = 0; i < len; i++) {
        int pos = (i + offset) % (len > 0 ? len : 1);
        int seg = (len > 1) ? (pos * 5 / len) : 0;
        int r, g, b;
        switch (seg) {
            case 0: r = 255; g = 255; b = 255; break; /* branco */
            case 1: r = 180; g = 130; b = 255; break; /* roxo claro */
            case 2: r = 90;  g = 40;  b = 150; break; /* roxo escuro */
            case 3: r = 80;  g = 80;  b = 90;  break; /* cinza */
            default:r = 20;  g = 20;  b = 25;  break; /* quase preto */
        }
        printf("\033[38;2;%d;%d;%dm%c", r, g, b, s[i]);
    }
    printf("\033[0m");
}

static void print_gradient(const char *s, int offset) {
    print_gradient_raw(s, offset);
    printf("\n");
}

/* Quantas linhas de terminal um bloco ocupa, contando quebra de linha. */
static int block_height(const char *const *lines, int n, int cols) {
    if (cols < 1) cols = 1;
    int h = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(lines[i]);
        int r = (len + cols - 1) / cols;
        if (r < 1) r = 1;
        h += r;
    }
    return h;
}

/* Desenha um bloco em linha absoluta, limpando antes as linhas que ele usa. */
static int draw_block(const char *const *lines, int n, int start_row, int cols, int offset) {
    if (cols < 1) cols = 1;
    int row = start_row;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(lines[i]);
        int r = (len + cols - 1) / cols;
        if (r < 1) r = 1;
        for (int k = 0; k < r; k++) {
            printf("\033[%d;1H\033[K", row + k);
        }
        printf("\033[%d;1H", row);
        print_gradient_raw(lines[i], offset);
        row += r;
    }
    return row;
}

/* ------------------------------------------------------------------ */
/* Animacao continua da galaxia                                        */
/* ------------------------------------------------------------------ */

#define ANIM_HEADER_ROWS 14   /* 13 linhas de estrelas + 1 linha em branco */
#define ANIM_MIN_COLS    50
#define ANIM_MIN_ROWS    16
#define ANIM_FRAME_MS    80

static int anim_enabled = 0;
static int anim_offset = 0;
static int anim_rows = 24;
static int anim_cols = 80;
static int anim_logo_live = 0;    /* banner pode ser redesenhado agora? */
static int anim_logo_pinned = 0;  /* banner esta fixo acima da regiao? */
static int anim_region_top = 2;   /* topo da regiao de rolagem atual */
static int anim_prompt_row = 2;   /* linha onde o prompt comeca */
static struct timespec anim_last;

static void anim_read_size(void) {
    struct winsize ws;
    anim_rows = 24;
    anim_cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) anim_rows = ws.ws_row;
        if (ws.ws_col > 0) anim_cols = ws.ws_col;
    }
}

static int anim_poll_resize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return 0;
    if (ws.ws_row == anim_rows && ws.ws_col == anim_cols) return 0;
    if (ws.ws_row > 0) anim_rows = ws.ws_row;
    if (ws.ws_col > 0) anim_cols = ws.ws_col;
    return 1;
}

/* Redesenha so as estrelas, sem tocar no banner nem na area do shell. */
static void anim_draw_stars(int offset) {
    for (int i = 0; i < 13; i++) {
        printf("\033[%d;1H", i + 1);
        print_gradient_raw(galaxy_lines[i], offset);
        printf("\033[K");
    }
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);
}

/* Render estatico: sem regiao de rolagem e sem animacao. */
static void anim_render_static(void) {
    int rows = anim_rows, cols = anim_cols;
    anim_logo_live = 0;
    anim_logo_pinned = 0;
    printf("\033[r\033[2J\033[H");
    draw_block(galaxy_lines, 13, 1, cols, anim_offset);
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);
    int bh = block_height(logo_lines, 7, cols);
    int prompt_row = ANIM_HEADER_ROWS + 1;
    if (ANIM_HEADER_ROWS + 1 + bh <= rows) {
        draw_block(logo_lines, 7, ANIM_HEADER_ROWS + 1, cols, anim_offset);
        prompt_row = ANIM_HEADER_ROWS + 1 + bh;
    }
    if (prompt_row > rows) prompt_row = rows;
    printf("\033[%d;1H", prompt_row);
    fflush(stdout);
}

/* Render completo: galaxia fixa no topo + banner + regiao de rolagem. */
static void anim_render_all(void) {
    int rows = anim_rows, cols = anim_cols;
    int banner_top = ANIM_HEADER_ROWS + 1;
    int bh = block_height(logo_lines, 7, cols);
    int banner_end = banner_top + bh;

    printf("\033[?2026h"); /* synchronized output (kitty): sem flicker */
    printf("\033[r");
    printf("\033[2J\033[H");
    draw_block(galaxy_lines, 13, 1, cols, anim_offset);
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);

    int region_top, prompt_row, logo_pinned = 0;
    if (banner_end <= rows) {
        draw_block(logo_lines, 7, banner_top, cols, anim_offset);
        prompt_row = banner_end;
        if (rows >= banner_end + 8) {
            region_top = banner_end;   /* terminal alto: banner fica fixo */
            logo_pinned = 1;
        } else {
            region_top = banner_top;   /* terminal curto: banner rola junto */
        }
    } else {
        prompt_row = banner_top;
        region_top = banner_top;
    }

    if (region_top > rows - 1) region_top = rows - 1;
    if (region_top < 2) region_top = 2;
    if (prompt_row < region_top) prompt_row = region_top;
    if (prompt_row > rows) prompt_row = rows;

    anim_logo_pinned = logo_pinned;
    anim_logo_live = 1;
    anim_region_top = region_top;
    anim_prompt_row = prompt_row;

    printf("\033[%d;%dr", region_top, rows);
    printf("\033[%d;1H", prompt_row);
    printf("\033[?2026l");
    fflush(stdout);
}

static void anim_setup(void) {
    if (!isatty(STDOUT_FILENO)) return;
    anim_read_size();
    if (anim_cols < ANIM_MIN_COLS || anim_rows < ANIM_MIN_ROWS) {
        anim_enabled = 0;
        anim_render_static();
        return;
    }
    anim_enabled = 1;
    anim_render_all();
}

static void anim_clear_screen(void) {
    if (!isatty(STDOUT_FILENO)) return;
    anim_read_size();
    anim_enabled = (anim_cols >= ANIM_MIN_COLS && anim_rows >= ANIM_MIN_ROWS);
    if (anim_enabled) anim_render_all();
    else anim_render_static();
}

/* Reaplica a regiao de rolagem sem limpar a tela (ex.: ao voltar do /kalc). */
static void anim_reapply_region(void) {
    if (!isatty(STDOUT_FILENO)) return;
    int old_rows = anim_rows, old_cols = anim_cols;
    anim_read_size();
    if (anim_rows != old_rows || anim_cols != old_cols) {
        anim_clear_screen();
        return;
    }
    if (!anim_enabled) return;
    printf("\033[%d;%dr", anim_region_top, anim_rows);
    fflush(stdout);
}

static void anim_cleanup(void) {
    if (!isatty(STDOUT_FILENO)) return;
    printf("\033[?2026l\033[r");
    if (anim_rows > 0) printf("\033[%d;1H", anim_rows);
    printf("\033[0m");
    fflush(stdout);
}

/* Chamado pelo readline enquanto ele espera entrada. */
static int anim_tick(void) {
    if (anim_poll_resize()) {
        int ok = (anim_cols >= ANIM_MIN_COLS && anim_rows >= ANIM_MIN_ROWS);
        if (ok) {
            anim_enabled = 1;
            anim_render_all();
        } else {
            anim_enabled = 0;
            printf("\033[r\033[%d;1H", anim_rows > 0 ? anim_rows : 24);
            fflush(stdout);
        }
        rl_on_new_line();
        rl_redisplay();
        return 0;
    }
    if (!anim_enabled) return 0;
    if (rl_pending_input) return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - anim_last.tv_sec) * 1000L +
              (now.tv_nsec - anim_last.tv_nsec) / 1000000L;
    if (ms < ANIM_FRAME_MS) return 0;
    anim_last = now;

    anim_offset = (anim_offset + 3) % 4096;
    printf("\033[?2026h\033[s");
    anim_draw_stars(anim_offset);
    if (anim_logo_live) {
        draw_block(logo_lines, 7, ANIM_HEADER_ROWS + 1, anim_cols, anim_offset);
    }
    printf("\033[u\033[?2026l");
    fflush(stdout);
    return 0;
}

/* Ctrl+L do readline: limpa e redesenha o cabecalho sem perder a regiao. */
static int anim_clear_key(int count, int key) {
    (void)count; (void)key;
    anim_clear_screen();
    rl_on_new_line();
    rl_redisplay();
    return 0;
}

static void print_banner(void) {
    if (!isatty(STDOUT_FILENO)) return;
    printf("\033[r");
    for (int frame = 0; frame < 12; frame++) {
        printf("\033[2J\033[H");
        int off = frame * 3;
        for (int i = 0; i < 13; i++) print_gradient(galaxy_lines[i], off);
        printf("\n");
        for (int i = 0; i < 7; i++) print_gradient(logo_lines[i], off);
        printf("\n");
        fflush(stdout);
        struct timespec ts = {0, 80000000L};
        nanosleep(&ts, NULL);
    }
}

/* ================================================================== */
/* /kalc - mini manual de matematica interativo (menu + mouse)        */
/* ================================================================== */

typedef struct {
    const char *title;
    const char *const *lines;
    int nlines;
} KalcTopic;

static const char *const kalc_sum[] = {
    "# O que é",
    "Somar é juntar quantidades. O resultado é a soma ou total.",
    "a + b = c, onde a e b são as parcelas e c é a soma.",
    "",
    "# Exemplos: do básico ao avançado",
    "- 2 + 3 = 5",
    "- 17 + 25 = 42",
    "- 248 + 137 = 385",
    "- 1/2 + 1/3 = 5/6  (frações: iguale os denominadores)",
    "- 0,75 + 1,50 = 2,25  (alinhar as vírgulas)",
    "- 7 + (-3) = 4  (somar negativo é subtrair)",
    "",
    "# Regras e propriedades",
    "- Comutativa: a + b = b + a",
    "- Associativa: (a + b) + c = a + (b + c)",
    "- Elemento neutro: a + 0 = a",
    "- Oposto: a + (-a) = 0",
    "- Frações: a/b + c/d = (ad + cb) / bd",
    "",
    "# Macetes",
    "- Some primeiro as dezenas e depois as unidades.",
    "- Para somar 9, 19, 29...: some 10, 20, 30 e tire 1.",
    "- Para 98, 97...: arredonde para 100 e compense.",
    "- 1 + 2 + ... + n = n(n+1)/2.",
    "- Pares que somam 10: 1+9, 2+8, 3+7, 4+6, 5+5.",
    "",
    "# Erros comuns",
    "- Não alinhar as casas decimais.",
    "- Somar numeradores e denominadores de frações direto.",
    "- Trocar o sinal ao somar negativos.",
};

static const char *const kalc_sub[] = {
    "# O que é",
    "Subtrair é retirar uma quantidade de outra: a - b = c.",
    "a é o minuendo, b o subtraendo e c a diferença.",
    "",
    "# Exemplos: do básico ao avançado",
    "- 9 - 4 = 5",
    "- 52 - 18 = 34",
    "- 300 - 125 = 175",
    "- 3/4 - 1/4 = 2/4 = 1/2",
    "- 2,5 - 0,75 = 1,75",
    "- 5 - (-3) = 8  (menos com menos vira mais)",
    "- 10 - 25 = -15",
    "",
    "# Regras e propriedades",
    "- Não é comutativa: a - b != b - a (em geral).",
    "- Subtrair é somar o oposto: a - b = a + (-b).",
    "- a - 0 = a  e  a - a = 0.",
    "- Sinais: -(-x) = x  e  -(a + b) = -a - b.",
    "- Frações: a/b - c/d = (ad - cb) / bd.",
    "",
    "# Macetes",
    "- Para tirar 9: tire 10 e devolva 1.",
    "- Para tirar 99: tire 100 e devolva 1.",
    "- Complete a dezena: 63 - 47 = 63 - 50 + 3 = 16.",
    "- Confira com a soma: se a - b = c, então c + b = a.",
    "- Em dinheiro, pense no troco.",
    "",
    "# Erros comuns",
    "- Inverter a ordem em perguntas de 'quanto falta'.",
    "- Errar o sinal com parênteses.",
    "- Não alinhar as vírgulas.",
};

static const char *const kalc_mul[] = {
    "# O que é",
    "Multiplicar é somar parcelas iguais: a x b = c.",
    "3 x 4 = 3 + 3 + 3 + 3 = 12.",
    "",
    "# Exemplos: do básico ao avançado",
    "- 6 x 7 = 42",
    "- 12 x 15 = 180",
    "- 25 x 4 = 100",
    "- 1/2 x 1/3 = 1/6",
    "- 0,5 x 0,2 = 0,10",
    "- (-3) x 4 = -12",
    "- (-3) x (-4) = 12",
    "",
    "# Regras e propriedades",
    "- Comutativa: a x b = b x a",
    "- Associativa: (a x b) x c = a x (b x c)",
    "- Distributiva: a x (b + c) = ab + ac",
    "- Neutro: a x 1 = a   |   Nulo: a x 0 = 0",
    "- Sinais: (+)(+) = +, (-)(-) = +, (+)(-) = -",
    "- Frações: (a/b) x (c/d) = ac/bd",
    "",
    "# Macetes",
    "- Tabuada do 9: 9 x n = 10n - n.",
    "- x5 = metade de x10; x25 = x100/4; x50 = x100/2.",
    "- x11 (2 dígitos): 23 x 11 = 2 (2+3) 3 = 253.",
    "- (a+b)(a-b) = a^2 - b^2.",
    "- x4 = dobrar duas vezes.",
    "",
    "# Erros comuns",
    "- Achar que (-)(-) dá negativo.",
    "- Multiplicar numerador por denominador.",
    "- Errar a vírgula: 0,2 x 0,3 = 0,06 (duas casas).",
};

static const char *const kalc_div[] = {
    "# O que é",
    "Dividir é repartir em partes iguais: a / b = c.",
    "a é o dividendo, b o divisor e c o quociente.",
    "O resto é o que sobra quando a divisão não é exata.",
    "",
    "# Exemplos: do básico ao avançado",
    "- 12 / 3 = 4",
    "- 45 / 5 = 9",
    "- 100 / 8 = 12,5",
    "- 7 / 2 = 3,5",
    "- (1/2) / (1/4) = 2",
    "- 10 / 0 = indefinido (não existe)",
    "",
    "# Regras e propriedades",
    "- Não é comutativa: a / b != b / a.",
    "- Dividir é multiplicar pelo inverso: a / b = a x (1/b).",
    "- a / 1 = a   |   a / a = 1 (a != 0)   |   0 / a = 0.",
    "- Sinais: (+)/(+) = +, (-)/(-) = +, (+)/(-) = -.",
    "- Frações: (a/b) / (c/d) = (a/b) x (d/c).",
    "- Com resto: a = bq + r, com 0 <= r < b.",
    "",
    "# Macetes",
    "- /5 = x2 /10;  /25 = x4 /100;  /50 = x2 /100.",
    "- Para dividir por 4: divida por 2 duas vezes.",
    "- Divisibilidade: 3 (soma dos dígitos), 9 (idem),",
    "  5 (termina em 0 ou 5), 2 (par).",
    "- Estime arredondando e ajuste no final.",
    "- Regra de três nada mais é que proporção.",
    "",
    "# Erros comuns",
    "- Dividir por zero.",
    "- Inverter dividendo e divisor.",
    "- Esquecer o resto em divisões inteiras.",
    "- Não inverter a fração de baixo.",
};

static const KalcTopic kalc_topics[] = {
    { "Sum (soma)", kalc_sum, (int)(sizeof(kalc_sum) / sizeof(kalc_sum[0])) },
    { "Subtract (subtração)", kalc_sub, (int)(sizeof(kalc_sub) / sizeof(kalc_sub[0])) },
    { "Multiplication (multiplicação)", kalc_mul, (int)(sizeof(kalc_mul) / sizeof(kalc_mul[0])) },
    { "Division (divisão)", kalc_div, (int)(sizeof(kalc_div) / sizeof(kalc_div[0])) },
};
#define KALC_NTOPICS ((int)(sizeof(kalc_topics) / sizeof(kalc_topics[0])))
#define KALC_MENU_TOP 5
#define KALC_MENU_QUIT (KALC_MENU_TOP + KALC_NTOPICS + 1)

#define KALC_PURPLE "\033[38;2;180;130;255m"
#define KALC_DIM    "\033[38;2;120;110;140m"
#define KALC_SEL    "\033[48;2;70;40;120m\033[38;2;255;255;255m"
#define KALC_RESET  "\033[0m"

static int kalc_rows = 24;
static int kalc_cols = 80;

static void kalc_size(void) {
    struct winsize ws;
    kalc_rows = 24;
    kalc_cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) kalc_rows = ws.ws_row;
        if (ws.ws_col > 0) kalc_cols = ws.ws_col;
    }
}

static int kalc_find_topic(const char *s) {
    if (!s) return -1;
    if (s[0] >= '1' && s[0] < '1' + KALC_NTOPICS && s[1] == '\0')
        return s[0] - '1';
    if (strcasecmp(s, "sum") == 0 || strcasecmp(s, "soma") == 0) return 0;
    if (strcasecmp(s, "sub") == 0 || strcasecmp(s, "subtract") == 0 ||
        strcasecmp(s, "subtracao") == 0 || strcasecmp(s, "subtração") == 0) return 1;
    if (strcasecmp(s, "mul") == 0 || strcasecmp(s, "mult") == 0 ||
        strcasecmp(s, "multiplication") == 0 ||
        strcasecmp(s, "multiplicacao") == 0 || strcasecmp(s, "multiplicação") == 0) return 2;
    if (strcasecmp(s, "div") == 0 || strcasecmp(s, "division") == 0 ||
        strcasecmp(s, "divisao") == 0 || strcasecmp(s, "divisão") == 0) return 3;
    return -1;
}

/* Impressao simples para quando nao ha terminal interativo. */
static void kalc_print_plain(const char *start) {
    int idx = kalc_find_topic(start);
    if (idx >= 0) {
        const KalcTopic *t = &kalc_topics[idx];
        printf("%s\n", t->title);
        for (int i = 0; i < t->nlines; i++) {
            const char *l = t->lines[i];
            if (l[0] == '#' && l[1] == ' ') printf("\n%s\n", l + 2);
            else if (l[0] == '-' && l[1] == ' ') printf("  - %s\n", l + 2);
            else printf("%s\n", l);
        }
        return;
    }
    printf("KALC :: matemática básica\n");
    for (int i = 0; i < KALC_NTOPICS; i++)
        printf("  [%d] %s\n", i + 1, kalc_topics[i].title);
    printf("\nUse /kalc <topico> para abrir direto (ex.: /kalc sum).\n");
}

static void kalc_render_menu(int sel) {
    kalc_size();
    printf("\033[?2026h\033[2J\033[H");
    printf("\033[2;3H%sKALC :: matemática básica%s", KALC_PURPLE, KALC_RESET);
    printf("\033[3;3H%s-------------------------%s", KALC_DIM, KALC_RESET);
    for (int i = 0; i < KALC_NTOPICS; i++) {
        printf("\033[%d;3H", KALC_MENU_TOP + i);
        if (i == sel)
            printf("%s > [%d]  %s %s", KALC_SEL, i + 1, kalc_topics[i].title, KALC_RESET);
        else
            printf("   [%d]  %s", i + 1, kalc_topics[i].title);
    }
    printf("\033[%d;3H", KALC_MENU_QUIT);
    if (sel == KALC_NTOPICS)
        printf("%s > [q]  Sair %s", KALC_SEL, KALC_RESET);
    else
        printf("   [q]  Sair");
    printf("\033[%d;3H%sClique em um tópico, ou use ↑/↓ + Enter.%s",
           KALC_MENU_QUIT + 2, KALC_DIM, KALC_RESET);
    printf("\033[?2026l");
    fflush(stdout);
}

/* Copia no maximo maxw caracteres (largura), sem cortar UTF-8. */
static const char *kalc_fit(const char *line, char *buf, size_t bufsz, int maxw) {
    size_t i = 0;
    int w = 0;
    while (line[i]) {
        unsigned char c = (unsigned char)line[i];
        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (w + 1 > maxw || i + (size_t)len >= bufsz) break;
        memcpy(buf + i, line + i, (size_t)len);
        i += (size_t)len;
        w++;
    }
    buf[i] = '\0';
    return buf;
}

/* Retorna o scroll ja limitado ao conteudo. */
static int kalc_render_topic(int idx, int scroll) {
    kalc_size();
    int rows = kalc_rows;
    const KalcTopic *t = &kalc_topics[idx];
    int body_top = 4;
    int body_rows = rows - body_top;
    if (body_rows < 1) body_rows = 1;
    int max_scroll = t->nlines - body_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0) scroll = 0;

    printf("\033[?2026h\033[2J\033[H");
    printf("\033[1;3H%s%s%s", KALC_PURPLE, t->title, KALC_RESET);
    printf("\033[2;3H%s-------------------------%s", KALC_DIM, KALC_RESET);
    int avail = kalc_cols - 4;
    if (avail < 1) avail = 1;
    for (int i = 0; i < body_rows; i++) {
        int li = scroll + i;
        if (li >= t->nlines) break;
        printf("\033[%d;3H", body_top + i);
        const char *raw = t->lines[li];
        int extra = (raw[0] == '-' && raw[1] == ' ') ? 2 : 0;
        char fit[512];
        const char *line = kalc_fit(raw, fit, sizeof(fit), avail - extra);
        if (line[0] == '#' && line[1] == ' ') {
            printf("%s%s%s", KALC_PURPLE, line + 2, KALC_RESET);
        } else if (line[0] == '-' && line[1] == ' ') {
            printf("%s•%s %s", KALC_PURPLE, KALC_RESET, line + 2);
        } else {
            printf("%s", line);
        }
    }
    printf("\033[%d;3H%s[<] Voltar%s   ↑/↓ rolar   q voltar   %d/%d",
           rows, KALC_DIM, KALC_RESET, scroll + 1, max_scroll + 1);
    printf("\033[?2026l");
    fflush(stdout);
    return scroll;
}

enum {
    K_NONE = 0, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_PGUP, K_PGDN, K_HOME, K_END, K_ENTER, K_ESC, K_MOUSE
};

typedef struct {
    int type;
    int ch;
    int mx, my;
    int mb;
    int press;
} KalcKey;

static int kalc_getc(int timeout_ms) {
    struct pollfd p;
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    p.revents = 0;
    int r;
    do {
        r = poll(&p, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    if (r <= 0) return -1;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

static int kalc_read_key(KalcKey *k, int timeout_ms) {
    memset(k, 0, sizeof(*k));
    int c = kalc_getc(timeout_ms);
    if (c < 0) return 0;
    if (c == '\r' || c == '\n') { k->type = K_ENTER; return 1; }
    if (c != 27) { k->type = K_CHAR; k->ch = c; return 1; }

    int c2 = kalc_getc(30);
    if (c2 < 0) { k->type = K_ESC; return 1; }
    if (c2 != '[') { k->type = K_ESC; return 1; }

    char seq[64];
    int n = 0;
    while (n < (int)sizeof(seq) - 1) {
        int d = kalc_getc(30);
        if (d < 0) break;
        seq[n++] = (char)d;
        if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || d == '~') break;
    }
    seq[n] = '\0';

    if (n == 1) {
        switch (seq[0]) {
            case 'A': k->type = K_UP; return 1;
            case 'B': k->type = K_DOWN; return 1;
            case 'C': k->type = K_RIGHT; return 1;
            case 'D': k->type = K_LEFT; return 1;
            case 'H': k->type = K_HOME; return 1;
            case 'F': k->type = K_END; return 1;
        }
    }
    if (strcmp(seq, "5~") == 0) { k->type = K_PGUP; return 1; }
    if (strcmp(seq, "6~") == 0) { k->type = K_PGDN; return 1; }
    if (seq[0] == '<') {
        int b, x, y;
        char term;
        if (sscanf(seq, "<%d;%d;%d%c", &b, &x, &y, &term) == 4) {
            k->type = K_MOUSE;
            k->mb = b;
            k->mx = x;
            k->my = y;
            k->press = (term == 'M');
            return 1;
        }
    }
    k->type = K_ESC;
    return 1;
}

static void kalc_run(const char *start) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        kalc_print_plain(start);
        return;
    }

    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) return;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[r\033[2J\033[H\033[?25l\033[?1000h\033[?1006h");
    fflush(stdout);

    int mode = 0;      /* 0 = menu, 1 = topico */
    int sel = 0;
    int topic = 0;
    int scroll = 0;
    int idx = kalc_find_topic(start);
    if (idx >= 0) { mode = 1; topic = idx; }

    kalc_size();
    int last_rows = kalc_rows, last_cols = kalc_cols;
    if (mode == 0) kalc_render_menu(sel);
    else scroll = kalc_render_topic(topic, scroll);

    int running = 1;
    while (running) {
        KalcKey k;
        int got = kalc_read_key(&k, 200);

        kalc_size();
        if (kalc_rows != last_rows || kalc_cols != last_cols) {
            last_rows = kalc_rows;
            last_cols = kalc_cols;
            if (mode == 0) kalc_render_menu(sel);
            else scroll = kalc_render_topic(topic, scroll);
        }
        if (!got) continue;

        if (mode == 0) {
            switch (k.type) {
                case K_CHAR:
                    if (k.ch == 'q' || k.ch == 'Q' || k.ch == 3) {
                        running = 0;
                    } else if (k.ch >= '1' && k.ch < '1' + KALC_NTOPICS) {
                        topic = k.ch - '1';
                        mode = 1;
                        scroll = 0;
                        scroll = kalc_render_topic(topic, scroll);
                    }
                    break;
                case K_UP:
                    sel = (sel + KALC_NTOPICS) % (KALC_NTOPICS + 1);
                    kalc_render_menu(sel);
                    break;
                case K_DOWN:
                    sel = (sel + 1) % (KALC_NTOPICS + 1);
                    kalc_render_menu(sel);
                    break;
                case K_ENTER:
                    if (sel < KALC_NTOPICS) {
                        topic = sel;
                        mode = 1;
                        scroll = 0;
                        scroll = kalc_render_topic(topic, scroll);
                    } else {
                        running = 0;
                    }
                    break;
                case K_ESC:
                    running = 0;
                    break;
                case K_MOUSE:
                    if (k.press && k.mb == 0) {
                        if (k.my >= KALC_MENU_TOP && k.my < KALC_MENU_TOP + KALC_NTOPICS) {
                            topic = k.my - KALC_MENU_TOP;
                            mode = 1;
                            scroll = 0;
                            scroll = kalc_render_topic(topic, scroll);
                        } else if (k.my == KALC_MENU_QUIT) {
                            running = 0;
                        }
                    }
                    break;
            }
        } else {
            int body_rows = kalc_rows - 4;
            if (body_rows < 1) body_rows = 1;
            switch (k.type) {
                case K_CHAR:
                    if (k.ch == 'q' || k.ch == 'Q' || k.ch == 3 ||
                        k.ch == 8 || k.ch == 127) {
                        mode = 0;
                        kalc_render_menu(sel);
                    } else if (k.ch == 'j') {
                        scroll = kalc_render_topic(topic, scroll + 1);
                    } else if (k.ch == 'k') {
                        scroll = kalc_render_topic(topic, scroll - 1);
                    }
                    break;
                case K_UP:
                    scroll = kalc_render_topic(topic, scroll - 1);
                    break;
                case K_DOWN:
                    scroll = kalc_render_topic(topic, scroll + 1);
                    break;
                case K_PGUP:
                    scroll = kalc_render_topic(topic, scroll - body_rows);
                    break;
                case K_PGDN:
                    scroll = kalc_render_topic(topic, scroll + body_rows);
                    break;
                case K_HOME:
                    scroll = kalc_render_topic(topic, 0);
                    break;
                case K_END:
                    scroll = kalc_render_topic(topic, 1 << 20);
                    break;
                case K_ESC:
                    mode = 0;
                    kalc_render_menu(sel);
                    break;
                case K_MOUSE:
                    if (k.press) {
                        if (k.mb == 0 && k.my == kalc_rows) {
                            mode = 0;
                            kalc_render_menu(sel);
                        } else if (k.mb == 64) {
                            scroll = kalc_render_topic(topic, scroll - 3);
                        } else if (k.mb == 65) {
                            scroll = kalc_render_topic(topic, scroll + 3);
                        }
                    }
                    break;
            }
        }
    }

    printf("\033[?1006l\033[?1000l\033[?25h\033[0m\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

static void save_history(void) {
    write_history(histfile);
}

int main(void) {
    const char *home = getenv("HOME");
    snprintf(histfile, sizeof(histfile), "%s/.void_shell_history", home ? home : ".");
    read_history(histfile);
    atexit(save_history);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    if (isatty(STDIN_FILENO)) {
        shell_terminal = STDIN_FILENO;
        setpgid(0, 0);
        shell_pgid = getpid();
        tcsetpgrp(shell_terminal, shell_pgid);
    }

    print_banner();
    anim_setup();
    rl_set_keyboard_input_timeout(80000);
    rl_event_hook = anim_tick;
    rl_add_defun("void-clear-screen", anim_clear_key, '\x0c');
    atexit(anim_cleanup);

    for (;;) {
        if (anim_poll_resize()) {
            int ok = (anim_cols >= ANIM_MIN_COLS && anim_rows >= ANIM_MIN_ROWS);
            if (ok) {
                anim_enabled = 1;
                anim_render_all();
            } else {
                anim_enabled = 0;
                printf("\033[r\033[%d;1H", anim_rows > 0 ? anim_rows : 24);
                fflush(stdout);
            }
        }
        char *input = readline("\001\033[38;2;140;90;220m\002void-shell> \001\033[0m\002");
        if (!anim_logo_pinned) anim_logo_live = 0;
        if (!input) { /* EOF (Ctrl+D) */
            printf("\n");
            break;
        }
        if (*input) {
            add_history(input);
            run_line(input);
        }
        free(input);
    }

    return 0;
}

