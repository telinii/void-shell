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

#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_TOKEN 4096

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

static void print_gradient(const char *s, int offset) {
    int len = strlen(s);
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
    printf("\033[0m\033[K\n");
}

static void print_banner(void) {
    if (!isatty(STDOUT_FILENO)) return;
    for (int frame = 0; frame < 12; frame++) {
        printf("\033[2J\033[H");
        int off = frame * 3;
        print_gradient("                .  *  .  *  .  *  .  *", off);
        print_gradient("          .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", off);
        print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", off);
        print_gradient(".  *  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", off);
        print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", off);
        print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient("          .  *  .  *  .  *  .  *  .  *  .", off);
        print_gradient("                .  *  .  *  .  *  .  *", off);
        printf("\n");
        print_gradient(":::     :::  ::::::::  ::::::::::: :::::::::                   ::::::::  :::    ::: :::::::::: :::        :::        ", off);
        print_gradient(":+:     :+: :+:    :+:     :+:     :+:    :+:                 :+:    :+: :+:    :+: :+:        :+:        :+:        ", off);
        print_gradient("+:+     +:+ +:+    +:+     +:+     +:+    +:+                 +:+        +:+    +:+ +:+        +:+        +:+        ", off);
        print_gradient("+#+     +:+ +#+    +:+     +#+     +#+    +:+  +#++:++#++:++  +#++:++#++ +#++:++#++ +#++:++#   +#+        +#+        ", off);
        print_gradient(" +#+   +#+  +#+    +#+     +#+     +#+    +#+                        +#+ +#+    +#+ +#+        +#+        +#+        ", off);
        print_gradient("  #+#+#+#   #+#    #+#     #+#     #+#    #+#                 #+#    #+# #+#    #+# #+#        #+#        #+#        ", off);
        print_gradient("    ###      ########  ########### #########                   ########  ###    ### ########## ########## ##########  ", off);
        printf("\n");
        fflush(stdout);
        struct timespec ts = {0, 80000000L};
        nanosleep(&ts, NULL);
    }
    /* final static frame */
    printf("\033[2J\033[H");
    print_gradient("                .  *  .  *  .  *  .  *", 0);
    print_gradient("          .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", 0);
    print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", 0);
    print_gradient(".  *  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", 0);
    print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", 0);
    print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient("          .  *  .  *  .  *  .  *  .  *  .", 0);
    print_gradient("                .  *  .  *  .  *  .  *", 0);
    printf("\n");
    print_gradient(":::     :::  ::::::::  ::::::::::: :::::::::                   ::::::::  :::    ::: :::::::::: :::        :::        ", 0);
    print_gradient(":+:     :+: :+:    :+:     :+:     :+:    :+:                 :+:    :+: :+:    :+: :+:        :+:        :+:        ", 0);
    print_gradient("+:+     +:+ +:+    +:+     +:+     +:+    +:+                 +:+        +:+    +:+ +:+        +:+        +:+        ", 0);
    print_gradient("+#+     +:+ +#+    +:+     +#+     +#+    +:+  +#++:++#++:++  +#++:++#++ +#++:++#++ +#++:++#   +#+        +#+        ", 0);
    print_gradient(" +#+   +#+  +#+    +#+     +#+     +#+    +#+                        +#+ +#+    +#+ +#+        +#+        +#+        ", 0);
    print_gradient("  #+#+#+#   #+#    #+#     #+#     #+#    #+#                 #+#    #+# #+#    #+# #+#        #+#        #+#        ", 0);
    print_gradient("    ###      ########  ########### #########                   ########  ###    ### ########## ########## ##########  ", 0);
    printf("\n");
}

static void save_history(void) {
    write_history(histfile);
}

static int anim_tick(void);

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

    rl_event_hook = anim_tick;
    
    for (;;) {
        char *input = readline("\001\033[38;2;140;90;220m\002void-shell> \001\033[0m\002");
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

static int anim_tick(void) {
    static struct timespec last = {0,0};
    static int offset = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec-last.tv_sec)*1000 + (now.tv_nsec-last.tv_nsec)/1000000;
    if (last.tv_sec == 0 && last.tv_nsec == 0) { last = now; return 0; }
    if (ms < 80) return 0;
    last = now;
    offset++;

    printf("\0337");   // save cursor
    printf("\033[H");  // home
    print_gradient("                .  *  .  *  .  *  .  *", offset);
    print_gradient("          .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", offset);
    print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", offset);
    print_gradient(".  *  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient(" .  *  .  *  .  *  .  *  .  *  .  *  .  *  .  .", offset);
    print_gradient("  .  *  .  *  .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient("    .  *  .  *  .  *  .  *  .  *  .  *  .  *", offset);
    print_gradient("      .  *  .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient("          .  *  .  *  .  *  .  *  .  *  .", offset);
    print_gradient("                .  *  .  *  .  *  .  *", offset);
    printf("\n");
    print_gradient(":::     :::  ::::::::  ::::::::::: :::::::::                   ::::::::  :::    ::: :::::::::: :::        :::        ", offset);
    print_gradient(":+:     :+: :+:    :+:     :+:     :+:    :+:                 :+:    :+: :+:    :+: :+:        :+:        :+:        ", offset);
    print_gradient("+:+     +:+ +:+    +:+     +:+     +:+    +:+                 +:+        +:+    +:+ +:+        +:+        +:+        ", offset);
    print_gradient("+#+     +:+ +#+    +:+     +#+     +#+    +:+  +#++:++#++:++  +#++:++#++ +#++:++#++ +#++:++#   +#+        +#+        ", offset);
    print_gradient(" +#+   +#+  +#+    +#+     +#+     +#+    +#+                        +#+ +#+    +#+ +#+        +#+        +#+        ", offset);
    print_gradient("  #+#+#+#   #+#    #+#     #+#     #+#    #+#                 #+#    #+# #+#    #+# #+#        #+#        #+#        ", offset);
    print_gradient("    ###      ########  ########### #########                   ########  ###    ### ########## ########## ##########  ", offset);
    printf("\n");
    printf("\0338");  // restore cursor
    fflush(stdout);
    return 0;
}

