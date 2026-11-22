#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>

#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_TOKEN 4096

/* ------------------------------------------------------------------ */
/* Temas: cores do prompt, do texto, do cabecalho, do HUD e do gradiente */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *id;
    const char *name;
    int prompt[3];
    int text[3];
    int head[3];
    int dim[3];
    int sel_bg[3];
    int hud_bg[3];
    int hud_dim[3];
    int ok[3];
    int err[3];
    int grad[5][3];
} Theme;

static const Theme themes[] = {
    {
        "galaxy", "Galaxy",
        {140, 90, 220}, {190, 150, 255}, {180, 130, 255}, {120, 110, 140},
        {70, 40, 120}, {24, 14, 42}, {140, 125, 175}, {120, 220, 150}, {255, 120, 120},
        {{255, 255, 255}, {180, 130, 255}, {90, 40, 150}, {80, 80, 90}, {20, 20, 25}},
    },
    {
        "matrix", "Matrix",
        {60, 180, 90}, {150, 255, 180}, {110, 230, 150}, {90, 150, 110},
        {20, 70, 40}, {6, 24, 12}, {90, 150, 110}, {150, 255, 180}, {255, 120, 120},
        {{255, 255, 255}, {120, 255, 160}, {20, 140, 70}, {60, 90, 70}, {5, 15, 10}},
    },
    {
        "synthwave", "Synthwave",
        {255, 80, 170}, {255, 160, 225}, {110, 230, 255}, {150, 120, 180},
        {90, 20, 80}, {30, 10, 40}, {160, 130, 190}, {110, 240, 230}, {255, 140, 90},
        {{255, 255, 255}, {255, 120, 200}, {150, 60, 220}, {90, 60, 130}, {20, 10, 30}},
    },
    {
        "mono", "Mono",
        {150, 150, 160}, {230, 230, 235}, {210, 210, 220}, {140, 140, 150},
        {70, 70, 80}, {20, 20, 24}, {150, 150, 160}, {220, 220, 220}, {255, 120, 120},
        {{255, 255, 255}, {200, 200, 210}, {120, 120, 130}, {70, 70, 80}, {15, 15, 18}},
    },
    {
        "ocean", "Ocean",
        {70, 170, 240}, {160, 220, 255}, {100, 220, 240}, {110, 140, 170},
        {20, 50, 80}, {8, 20, 35}, {100, 150, 180}, {110, 240, 200}, {255, 130, 120},
        {{240, 250, 255}, {120, 220, 255}, {40, 130, 220}, {30, 70, 120}, {8, 18, 30}},
    },
    {
        "sunset", "Sunset",
        {255, 140, 60}, {255, 210, 150}, {255, 190, 90}, {170, 130, 110},
        {90, 40, 20}, {30, 14, 10}, {190, 150, 120}, {160, 230, 140}, {255, 90, 90},
        {{255, 250, 230}, {255, 200, 100}, {255, 120, 50}, {160, 50, 60}, {35, 12, 15}},
    },
    {
        "dracula", "Dracula",
        {189, 147, 249}, {248, 248, 242}, {255, 121, 198}, {130, 130, 160},
        {68, 71, 90}, {40, 42, 54}, {160, 160, 180}, {80, 250, 123}, {255, 85, 85},
        {{248, 248, 242}, {255, 121, 198}, {189, 147, 249}, {98, 114, 164}, {40, 42, 54}},
    },
    {
        "nord", "Nord",
        {136, 192, 208}, {216, 222, 233}, {129, 161, 193}, {120, 130, 150},
        {59, 66, 82}, {46, 52, 64}, {143, 161, 179}, {163, 190, 140}, {191, 97, 106},
        {{236, 239, 244}, {136, 192, 208}, {94, 129, 172}, {76, 86, 106}, {46, 52, 64}},
    },
    {
        "amber", "Amber",
        {255, 176, 0}, {255, 210, 100}, {255, 190, 60}, {170, 130, 60},
        {70, 45, 10}, {24, 16, 4}, {180, 140, 70}, {200, 230, 100}, {255, 100, 80},
        {{255, 250, 220}, {255, 190, 60}, {200, 120, 20}, {100, 60, 20}, {20, 12, 4}},
    },
};
#define THEME_N ((int)(sizeof(themes) / sizeof(themes[0])))

/* Cores de fundo do terminal (OSC 11). Independentes do tema do shell. */
typedef struct {
    const char *id;
    const char *name;
    int rgb[3];
} TermBg;

static const TermBg term_bgs[] = {
    { "void",     "Void",      {11, 8, 18} },   /* padrao do void-term */
    { "black",    "Black",     {0, 0, 0} },
    { "midnight", "Midnight",  {4, 6, 12} },
    { "slate",    "Slate",     {13, 17, 23} },
    { "deep",     "Deep Sea",  {6, 16, 31} },
    { "plum",     "Plum",      {20, 10, 30} },
    { "ember",    "Ember",     {26, 15, 8} },
    { "forest",   "Forest",    {8, 20, 14} },
    { "wine",     "Wine",      {24, 8, 16} },
};
#define TERM_BG_N ((int)(sizeof(term_bgs) / sizeof(term_bgs[0])))

static int current_term_bg = 0;
static int term_bg_custom = 0;
static int term_bg_custom_rgb[3];

static int current_theme = 0;
static char theme_prompt[24];
static char theme_text[24];
static char theme_head[24];
static char theme_dim[24];
static char theme_sel[48];
static char theme_hud_bg[24];
static char theme_hud_dim[24];
static char theme_ok[24];
static char theme_err[24];
static char theme_grad[5][24];

static void theme_fg(char *buf, size_t n, const int c[3]) {
    snprintf(buf, n, "\033[38;2;%d;%d;%dm", c[0], c[1], c[2]);
}

static void theme_bg(char *buf, size_t n, const int c[3]) {
    snprintf(buf, n, "\033[48;2;%d;%d;%dm", c[0], c[1], c[2]);
}

static void term_bg_current_rgb(int rgb[3]);
static void hud_invalidate(void);

/* Cores de UI do shell (HUD e barra de selecao) derivadas do fundo do
 * terminal, para o conjunto nao brigar com a cor escolhida. */
static int term_bg_hud_rgb[3];
static int term_bg_sel_rgb[3];

static int lighten_channel(int v, double t) {
    int out = (int)(v + (255 - v) * t + 0.5);
    return out > 255 ? 255 : out;
}

static void term_bg_derive_ui(void) {
    int rgb[3];
    term_bg_current_rgb(rgb);
    for (int i = 0; i < 3; i++) {
        term_bg_hud_rgb[i] = lighten_channel(rgb[i], 0.075);
        term_bg_sel_rgb[i] = lighten_channel(rgb[i], 0.22);
    }
}

static void theme_build(void) {
    const Theme *t = &themes[current_theme];
    char bg[24];
    theme_fg(theme_prompt, sizeof(theme_prompt), t->prompt);
    theme_fg(theme_text, sizeof(theme_text), t->text);
    theme_fg(theme_head, sizeof(theme_head), t->head);
    theme_fg(theme_dim, sizeof(theme_dim), t->dim);
    theme_fg(theme_hud_dim, sizeof(theme_hud_dim), t->hud_dim);
    theme_fg(theme_ok, sizeof(theme_ok), t->ok);
    theme_fg(theme_err, sizeof(theme_err), t->err);
    term_bg_derive_ui();
    theme_bg(theme_hud_bg, sizeof(theme_hud_bg), term_bg_hud_rgb);
    theme_bg(bg, sizeof(bg), term_bg_sel_rgb);
    snprintf(theme_sel, sizeof(theme_sel), "%s\033[38;2;255;255;255m", bg);
    for (int i = 0; i < 5; i++)
        theme_fg(theme_grad[i], sizeof(theme_grad[i]), t->grad[i]);
}

/* ------------------------------------------------------------------ */
/* Fundo do terminal (OSC 11)                                          */
/* ------------------------------------------------------------------ */

static void term_bg_emit_rgb(const int rgb[3]) {
    if (!isatty(STDOUT_FILENO)) return;
    printf("\033]11;#%02x%02x%02x\a", rgb[0], rgb[1], rgb[2]);
    fflush(stdout);
}

static void term_bg_current_rgb(int rgb[3]) {
    if (term_bg_custom) {
        rgb[0] = term_bg_custom_rgb[0];
        rgb[1] = term_bg_custom_rgb[1];
        rgb[2] = term_bg_custom_rgb[2];
    } else {
        const TermBg *b = &term_bgs[current_term_bg];
        rgb[0] = b->rgb[0];
        rgb[1] = b->rgb[1];
        rgb[2] = b->rgb[2];
    }
}

static const char *term_bg_label(void) {
    static char buf[16];
    int rgb[3];
    if (!term_bg_custom) return term_bgs[current_term_bg].name;
    term_bg_current_rgb(rgb);
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    return buf;
}

static int term_bg_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int term_bg_parse_hex(const char *s, int rgb[3]) {
    if (*s == '#') s++;
    size_t n = strlen(s);
    if (n == 3) {
        for (int i = 0; i < 3; i++) {
            int d = term_bg_hex_digit(s[i]);
            if (d < 0) return 0;
            rgb[i] = d * 17;
        }
        return 1;
    }
    if (n == 6) {
        for (int i = 0; i < 3; i++) {
            int hi = term_bg_hex_digit(s[i * 2]);
            int lo = term_bg_hex_digit(s[i * 2 + 1]);
            if (hi < 0 || lo < 0) return 0;
            rgb[i] = hi * 16 + lo;
        }
        return 1;
    }
    return 0;
}

static int term_bg_find(const char *s) {
    if (!s || !*s) return -1;
    for (int i = 0; i < TERM_BG_N; i++)
        if (strcasecmp(s, term_bgs[i].id) == 0 ||
            strcasecmp(s, term_bgs[i].name) == 0)
            return i;
    if (strcasecmp(s, "default") == 0) return 0;
    if (strcasecmp(s, "deepsea") == 0 || strcasecmp(s, "deep-sea") == 0)
        return 4;
    return -1;
}

static void term_bg_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.void_shell_term_bg", home ? home : ".");
}

static void term_bg_save(void) {
    char path[4096];
    term_bg_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    if (term_bg_custom) {
        int rgb[3];
        term_bg_current_rgb(rgb);
        fprintf(f, "#%02x%02x%02x\n", rgb[0], rgb[1], rgb[2]);
    } else {
        fprintf(f, "%s\n", term_bgs[current_term_bg].id);
    }
    fclose(f);
}

static void term_bg_load(void) {
    current_term_bg = 0;
    term_bg_custom = 0;

    char path[4096], buf[64];
    term_bg_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            if (buf[0] == '#') {
                int rgb[3];
                if (term_bg_parse_hex(buf, rgb)) {
                    term_bg_custom = 1;
                    memcpy(term_bg_custom_rgb, rgb, sizeof(rgb));
                }
            } else {
                int idx = term_bg_find(buf);
                if (idx >= 0) current_term_bg = idx;
            }
        }
        fclose(f);
    }

    int rgb[3];
    term_bg_current_rgb(rgb);
    term_bg_emit_rgb(rgb);
}

static int term_bg_apply(int idx) {
    if (idx < 0 || idx >= TERM_BG_N) return 0;
    if (!term_bg_custom && idx == current_term_bg) return 0;
    current_term_bg = idx;
    term_bg_custom = 0;
    term_bg_emit_rgb(term_bgs[idx].rgb);
    term_bg_save();
    theme_build();
    hud_invalidate();
    return 1;
}

static int term_bg_apply_hex(const char *hex) {
    int rgb[3];
    if (!term_bg_parse_hex(hex, rgb)) return 0;
    if (term_bg_custom && memcmp(rgb, term_bg_custom_rgb, sizeof(rgb)) == 0)
        return 0;
    term_bg_custom = 1;
    memcpy(term_bg_custom_rgb, rgb, sizeof(rgb));
    term_bg_emit_rgb(rgb);
    term_bg_save();
    theme_build();
    hud_invalidate();
    return 1;
}

static void theme_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.void_shell_theme", home ? home : ".");
}

static void theme_save(void) {
    char path[4096];
    theme_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", themes[current_theme].id);
    fclose(f);
}

static void theme_load(void) {
    char path[4096], buf[64];
    theme_build();
    theme_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        for (int i = 0; i < THEME_N; i++) {
            if (strcasecmp(buf, themes[i].id) == 0) {
                current_theme = i;
                break;
            }
        }
    }
    fclose(f);
    theme_build();
}

static int theme_find(const char *s) {
    if (!s || !*s) return -1;
    for (int i = 0; i < THEME_N; i++)
        if (strcasecmp(s, themes[i].id) == 0) return i;
    if (strcasecmp(s, "default") == 0) return 0;
    if (strcasecmp(s, "green") == 0) return 1;
    if (strcasecmp(s, "pink") == 0 || strcasecmp(s, "retro") == 0) return 2;
    if (strcasecmp(s, "gray") == 0 || strcasecmp(s, "grey") == 0) return 3;
    if (strcasecmp(s, "blue") == 0 || strcasecmp(s, "sea") == 0) return 4;
    if (strcasecmp(s, "orange") == 0 || strcasecmp(s, "warm") == 0) return 5;
    if (strcasecmp(s, "drac") == 0) return 6;
    if (strcasecmp(s, "nordic") == 0) return 7;
    if (strcasecmp(s, "gold") == 0) return 8;
    return -1;
}

static int theme_apply(int idx) {
    if (idx < 0 || idx >= THEME_N) return 0;
    if (idx == current_theme) return 0;
    current_theme = idx;
    theme_build();
    theme_save();
    return 1;
}

static void anim_clear_screen(void);
static void anim_reapply_region(void);
static void sugg_forget(void);
static void suggestion_erase(void);
static void redisplay_now(void);
static int  sigint_hook(void);
static void kalc_run(const char *start);
static int lang_run(const char *start);
static void lang_load(void);
static const char *tr(const char *en, const char *pt);
static void print_tutorial(void);
static int theme_run(const char *start);
static int anim_is_enabled(void);
static void hud_draw(void);
static void hud_tick(void);
static void hud_cpu_update(void);
static void hud_mem_update(void);

/* Sugestoes inline: estado compartilhado com a animacao e o readline. */
#define SUGG_TEXT_MAX 1024
#define SUGG_CMD_MAX  9000
#define SUGG_PROMPT_W 12   /* largura visivel de "void-shell> " */

static char sugg_text[SUGG_TEXT_MAX];
static int  sugg_active = 0;
static int  sugg_drawn = 0;
static char *sugg_cmds[SUGG_CMD_MAX];
static int   sugg_ncmds = 0;
static int   sugg_cmds_loaded = 0;
static volatile sig_atomic_t sigint_seen = 0;

static int last_status = 0;
static long last_duration_ms = -1;

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

static int is_shell_builtin(const char *name) {
    static const char *const names[] = {
        "exit", "clear", "cd", "pwd", "jobs", "fg", "bg",
        "export", "unset", "set", "echo", NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(name, names[i]) == 0) return 1;
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
    fflush(stdout);  /* nao duplicar o buffer do shell no filho */
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
    fflush(stdout);  /* nao duplicar o buffer do shell nos filhos */
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
            fflush(stdout);  /* nao duplicar o buffer do shell no filho */
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
            fflush(stdout);  /* nao duplicar o buffer do shell no filho */
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

    /* Builtins rodam no proprio processo: as redirecoes precisam ser
     * aplicadas aqui (o run_external so redirege o filho). */
    int builtin = is_shell_builtin(nargv[0]);
    int saved_out = -1, saved_in = -1;
    if (builtin && (infile || outfile)) {
        fflush(stdout);
        if (outfile) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(outfile, flags, 0644);
            if (fd < 0) { perror(outfile); last_status = 1; return; }
            saved_out = dup(STDOUT_FILENO);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (infile) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror(infile);
                last_status = 1;
                if (saved_out >= 0) {
                    dup2(saved_out, STDOUT_FILENO);
                    close(saved_out);
                }
                return;
            }
            saved_in = dup(STDIN_FILENO);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
    }

    int handled = do_builtin(nargv);

    if (builtin && (saved_out >= 0 || saved_in >= 0)) {
        fflush(stdout);
        if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
        if (saved_in >= 0) { dup2(saved_in, STDIN_FILENO); close(saved_in); }
    }
    if (handled) return;

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
    /* /lang: troca o idioma do shell (en / pt) */
    {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        int lang_cmd = 0, off = 0;
        if (strncmp(s, "/language", 9) == 0 &&
            (s[9] == '\0' || s[9] == ' ' || s[9] == '\t')) {
            lang_cmd = 1; off = 9;
        } else if (strncmp(s, "/lang", 5) == 0 &&
                   (s[5] == '\0' || s[5] == ' ' || s[5] == '\t')) {
            lang_cmd = 1; off = 5;
        }
        if (lang_cmd) {
            char *arg = s + off;
            while (*arg == ' ' || *arg == '\t') arg++;
            int alen = (int)strlen(arg);
            while (alen > 0 && (arg[alen - 1] == ' ' || arg[alen - 1] == '\t'))
                arg[--alen] = '\0';
            int changed = lang_run(*arg ? arg : NULL);
            anim_reapply_region();
            if (changed)
                printf("%s\n", tr("Language set to English.",
                                  "Idioma definido para Português (Brasil)."));
            last_status = 0;
            return;
        }
    }
    /* /theme: troca o tema de cores */
    {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "/theme", 6) == 0 &&
            (s[6] == '\0' || s[6] == ' ' || s[6] == '\t')) {
            char *arg = s + 6;
            while (*arg == ' ' || *arg == '\t') arg++;
            int alen = (int)strlen(arg);
            while (alen > 0 && (arg[alen - 1] == ' ' || arg[alen - 1] == '\t'))
                arg[--alen] = '\0';
            int changed = theme_run(*arg ? arg : NULL);
            anim_reapply_region();
            if (changed & 1) {
                if (!anim_is_enabled()) anim_clear_screen();
                printf("%s %s.\n", tr("Theme set to", "Tema definido para"),
                       themes[current_theme].name);
            }
            if (changed & 2) {
                printf("%s %s.\n",
                       tr("Terminal background set to",
                          "Fundo do terminal definido para"),
                       term_bg_label());
            }
            last_status = 0;
            return;
        }
    }
    /* /tutorial (alias /help): mostra o guia de boas-vindas de novo */
    {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        int tut = 0;
        if (strncmp(s, "/tutorial", 9) == 0 &&
            (s[9] == '\0' || s[9] == ' ' || s[9] == '\t'))
            tut = 1;
        else if (strncmp(s, "/help", 5) == 0 &&
                 (s[5] == '\0' || s[5] == ' ' || s[5] == '\t'))
            tut = 1;
        if (tut) {
            print_tutorial();
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
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣠⠤⠴⠒⠒⠋⠉⡉⠉⠉⠉⠉⢉⠉⠉⠉⠒⠒⠲⠤⣄⣀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡤⠖⠋⢁⠀⠀⠐⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠉⠓⠦⣄⡀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⠞⠉⠃⠄⠀⠀⠀⠀⢀⣠⠤⠖⠒⠚⠉⠉⠉⠉⡉⠓⠒⠲⠤⣄⣀⠀⠀⠀⠀⠀⠀⠀⠙⠢⣄",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⠃⠀⠀⠀⠀⠀⠀⣠⠞⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀⠀⠀⠀⠀⠈⠙⠦⣄⠀⠀⠀⠈⠀⠀⠈⠳",
    "⠀⠀⠀⠀⠀⠀⠀⠀⡀⡇⠀⠀⠀⠀⠀⠀⢸⠁⠀⠀⠀⠀⠀⡤⣶⣋⠉⡉⠙⠒⠳⢤⡀⠀⠀⠀⠀⠀⠊⠳⡄⠀⠀⠀⠠",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⣇⠀⠀⠀⠀⠀⠀⠸⣅⠠⡀⠀⠈⠰⣿⣏⣩⣿⠆⠀⡀⡀⠀⠙⡆⠀⠀⢈⠀⠀⠂⢹⠀⠀⠐",
    "⠀⠀⠀⠀⠀⠀⠀⠀⡀⠘⢦⡀⠀⠀⠀⠀⠈⠈⠓⠦⠤⣆⣒⣀⣩⠽⠛⠀⠀⠀⠠⢀⢀⡇⠀⠀⠀⠀⠀⠀⢸",
    "⠀⠀⠀⢦⡀⠀⠀⠀⠀⠀⠀⠙⠲⣄⡀⢀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⣀⡴⠋⠀⠀⠀⠀⠀⠀⢠⠏⠀⠐",
    "⠀⠀⠀⠀⠙⠢⣄⠀⠀⠀⠀⠀⠀⠀⠉⠙⠒⡦⠤⢤⣀⣀⣈⣄⣀⡤⠤⠴⠒⠋⠁⠀⠀⠀⠀⠀⠀⣀⡴⠋",
    "⠀⠀⠀⠀⠀⠀⠈⠙⠲⢤⣀⡀⠀⠀⢨⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀⠀⠀⠀⢀⣤⠶⠚⠁",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠙⠒⠦⠤⠤⣀⣀⣀⣀⣀⣀⣀⣀⣠⣀⣠⠤⠤⢖⠒⠋⠍",
    "",
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

/* Galaxia em braille: desenho em pontos, gradiente radial pulsante e
   rotacao lenta no plano do disco. As linhas sao UTF-8 (U+2800..U+28FF),
   entao decodificamos por code point. */
#define GALAXY_ROWS 13
#define GALAXY_COLS 50
#define GALAXY_DOTS_W (GALAXY_COLS * 2)
#define GALAXY_DOTS_H (GALAXY_ROWS * 4)
#define GALAXY_TILT 0.55
#define GALAXY_PI   3.14159265358979323846

static unsigned char galaxy_dots[GALAXY_DOTS_H][GALAXY_DOTS_W];
static int galaxy_dots_ready = 0;

static const unsigned char braille_bit[8] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};
static const int braille_dx[8] = {0, 0, 0, 1, 1, 1, 0, 1};
static const int braille_dy[8] = {0, 1, 2, 0, 1, 2, 3, 3};

static int utf8_decode(const char *s, unsigned *cp) {
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0) {
        *cp = ((unsigned)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0) {
        *cp = ((unsigned)(p[0] & 0x0F) << 12) |
              ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0) {
        *cp = ((unsigned)(p[0] & 0x07) << 18) |
              ((unsigned)(p[1] & 0x3F) << 12) |
              ((unsigned)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *cp = p[0];
    return 1;
}

/* Decodifica o desenho braille para uma matriz de pontos. */
static void galaxy_init_dots(void) {
    memset(galaxy_dots, 0, sizeof(galaxy_dots));
    for (int row = 0; row < GALAXY_ROWS; row++) {
        const char *p = galaxy_lines[row];
        int col = 0;
        while (*p && col < GALAXY_COLS) {
            unsigned cp;
            p += utf8_decode(p, &cp);
            if (cp >= 0x2800 && cp <= 0x28FF) {
                unsigned mask = cp - 0x2800;
                for (int b = 0; b < 8; b++) {
                    if (mask & braille_bit[b]) {
                        int x = col * 2 + braille_dx[b];
                        int y = row * 4 + braille_dy[b];
                        if (x < GALAXY_DOTS_W && y < GALAXY_DOTS_H)
                            galaxy_dots[y][x] = 1;
                    }
                }
            }
            col++;
        }
    }
    galaxy_dots_ready = 1;
}

static void galaxy_color(int col, int row, int ncols, int nrows,
                         double phase, char *out, size_t n) {
    double cx = (ncols - 1) / 2.0;
    double cy = (nrows - 1) / 2.0;
    double dx = col - cx;
    double dy = (row - cy) * 2.0;
    double maxr = sqrt(cx * cx + (cy * 2.0) * (cy * 2.0));
    double d = maxr > 0.0 ? sqrt(dx * dx + dy * dy) / maxr : 0.0;
    if (d > 1.0) d = 1.0;

    /* Anel de brilho que caminha do centro para fora. */
    double band = 0.5 + 0.5 * cos(2.0 * GALAXY_PI * (d * 2.2 - phase));
    band = band * band * band * band;
    double t = d * (1.0 - 0.55 * band);
    if (t < 0.0) t = 0.0;
    if (t > 0.85) t = 0.85;

    double tt = t * 4.0;
    int seg = (int)tt;
    if (seg < 0) seg = 0;
    if (seg > 3) seg = 3;
    double f = tt - seg;
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;

    const int (*g)[3] = themes[current_theme].grad;
    int r = (int)(g[seg][0] + (g[seg + 1][0] - g[seg][0]) * f);
    int gg = (int)(g[seg][1] + (g[seg + 1][1] - g[seg][1]) * f);
    int b = (int)(g[seg][2] + (g[seg + 1][2] - g[seg][2]) * f);
    snprintf(out, n, "\033[38;2;%d;%d;%dm", r, gg, b);
}

/* Amostra bilinear do desenho de pontos, para a rotacao nao picotar. */
static double galaxy_sample(double sx, double sy) {
    int x0 = (int)floor(sx);
    int y0 = (int)floor(sy);
    double fx = sx - x0;
    double fy = sy - y0;
    double v = 0.0;
    for (int oy = 0; oy <= 1; oy++) {
        double wy = oy ? fy : (1.0 - fy);
        for (int ox = 0; ox <= 1; ox++) {
            double wx = ox ? fx : (1.0 - fx);
            int x = x0 + ox, y = y0 + oy;
            if (x >= 0 && x < GALAXY_DOTS_W &&
                y >= 0 && y < GALAXY_DOTS_H &&
                galaxy_dots[y][x])
                v += wx * wy;
        }
    }
    return v;
}

/* Monta a mascara braille de uma celula girando o desenho no plano do
   disco: desfaz a inclinacao, gira e aplica a inclinacao de volta. */
static unsigned galaxy_cell_mask(int col, int row, double ca, double sa) {
    double cx = (GALAXY_DOTS_W - 1) / 2.0;
    double cy = (GALAXY_DOTS_H - 1) / 2.0;
    unsigned mask = 0;
    for (int b = 0; b < 8; b++) {
        double x = col * 2 + braille_dx[b];
        double y = row * 4 + braille_dy[b];
        double dx = x - cx;
        double dy = (y - cy) / GALAXY_TILT;
        double rx = dx * ca + dy * sa;
        double ry = -dx * sa + dy * ca;
        double sx = cx + rx;
        double sy = cy + ry * GALAXY_TILT;
        if (galaxy_sample(sx, sy) > 0.25)
            mask |= braille_bit[b];
    }
    return mask;
}

static void galaxy_put_braille(unsigned mask) {
    unsigned cp = 0x2800 + mask;
    putchar(0xE0 | (cp >> 12));
    putchar(0x80 | ((cp >> 6) & 0x3F));
    putchar(0x80 | (cp & 0x3F));
}

/* Desenha a galaxia a partir de start_row, quebrando linhas se preciso. */
static void galaxy_draw_block(int start_row, int cols, double phase,
                              double angle) {
    if (!galaxy_dots_ready) galaxy_init_dots();
    if (cols < 1) cols = 1;
    double ca = cos(angle), sa = sin(angle);
    int vrow = start_row;
    for (int row = 0; row < GALAXY_ROWS; row++) {
        int r = (GALAXY_COLS + cols - 1) / cols;
        if (r < 1) r = 1;
        for (int k = 0; k < r; k++)
            printf("\033[%d;1H\033[K", vrow + k);

        int line_row = vrow;
        printf("\033[%d;1H", line_row);
        for (int col = 0; col < GALAXY_COLS; col++) {
            if (col > 0 && col % cols == 0) {
                line_row++;
                printf("\033[%d;1H", line_row);
            }
            unsigned mask = galaxy_cell_mask(col, row, ca, sa);
            if (mask == 0) {
                putchar(' ');
            } else {
                char esc[32];
                galaxy_color(col, row, GALAXY_COLS, GALAXY_ROWS,
                             phase, esc, sizeof esc);
                fputs(esc, stdout);
                galaxy_put_braille(mask);
            }
        }
        vrow += r;
    }
}

static void print_gradient_raw(const char *s, int offset) {
    int len = (int)strlen(s);
    for (int i = 0; i < len; i++) {
        int pos = (i + offset) % (len > 0 ? len : 1);
        int seg = (len > 1) ? (pos * 5 / len) : 0;
        if (seg < 0) seg = 0;
        if (seg > 4) seg = 4;
        printf("%s%c", theme_grad[seg], s[i]);
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
#define LOGO_MIN_COLS    88   /* maior linha do banner (evita quebra feia) */
#define ANIM_MIN_ROWS    16
#define ANIM_FRAME_MS    80

/* HUD fixa na ultima linha. Precisa de espaco para o banner e o prompt. */
#define HUD_MIN_COLS     60
#define HUD_MIN_ROWS     20
#define HUD_REFRESH_MS   1000

static int anim_enabled = 0;
static int anim_offset = 0;
static double galaxy_phase = 0.0;
static double galaxy_angle = 0.0;
static int anim_rows = 24;
static int anim_cols = 80;
static int anim_logo_live = 0;    /* banner pode ser redesenhado agora? */
static int anim_logo_pinned = 0;  /* banner esta fixo acima da regiao? */
static int anim_region_top = 2;   /* topo da regiao de rolagem atual */
static int anim_region_bottom = 24; /* fim da regiao (HUD ocupa a ultima linha) */
static int anim_prompt_row = 2;   /* linha onde o prompt comeca */
static struct timespec anim_last;

static int hud_enabled = 0;
static int hud_row = 24;
static int hud_force = 1;
static long hud_last_ms = -100000;
static int hud_cpu_pct = -1;
static long hud_mem_used_mb = -1;
static long hud_mem_total_mb = -1;
static long long hud_prev_idle = -1;
static long long hud_prev_total = -1;

static void hud_invalidate(void) {
    hud_force = 1;
}

static int anim_is_enabled(void) {
    return anim_enabled;
}

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

/* Redesenha so a galaxia, sem tocar no banner nem na area do shell. */
static void anim_draw_stars(void) {
    galaxy_draw_block(1, anim_cols, galaxy_phase, galaxy_angle);
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);
}

/* Render estatico: sem regiao de rolagem e sem animacao. */
static void anim_render_static(void) {
    int rows = anim_rows, cols = anim_cols;
    sugg_forget();
    anim_logo_live = 0;
    anim_logo_pinned = 0;
    hud_enabled = 0;
    printf("\033[r\033[2J\033[H");
    galaxy_draw_block(1, cols, galaxy_phase, galaxy_angle);
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);
    int bh = cols >= LOGO_MIN_COLS ? block_height(logo_lines, 7, cols) : 0;
    int prompt_row = ANIM_HEADER_ROWS + 1;
    if (bh > 0 && ANIM_HEADER_ROWS + 1 + bh <= rows) {
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
    sugg_forget();
    int banner_top = ANIM_HEADER_ROWS + 1;
    /* Em painéis estreitos (splits) o banner quebra no meio das letras e
     * vira uma bagunça. Nesse caso ele é omitido e o prompt sobe. */
    int bh = cols >= LOGO_MIN_COLS ? block_height(logo_lines, 7, cols) : 0;
    int banner_end = banner_top + bh;

    printf("\033[?2026h"); /* synchronized output (kitty): sem flicker */
    printf("\033[r");
    printf("\033[2J\033[H");
    galaxy_draw_block(1, cols, galaxy_phase, galaxy_angle);
    printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);

    int hud = (cols >= HUD_MIN_COLS && rows >= HUD_MIN_ROWS);
    int region_bottom = hud ? rows - 1 : rows;

    int region_top, prompt_row, logo_pinned = 0, logo_live = 0;
    if (bh > 0 && banner_end <= rows) {
        draw_block(logo_lines, 7, banner_top, cols, anim_offset);
        prompt_row = banner_end;
        if (rows >= banner_end + 8) {
            region_top = banner_end;   /* terminal alto: banner fica fixo */
            logo_pinned = 1;
        } else {
            region_top = banner_top;   /* terminal curto: banner rola junto */
        }
        /* Só continua redesenhando o banner se ele estiver ACIMA da região
         * de rolagem. Quando o terminal é baixo o banner fica dentro dela:
         * redesenhá-lo a cada frame apaga o texto que rolou e faz o prompt
         * "piscar" em cima dele. */
        logo_live = logo_pinned;
    } else {
        prompt_row = banner_top;
        region_top = banner_top;
    }

    if (region_top > region_bottom - 1) region_top = region_bottom - 1;
    if (region_top < 2) region_top = 2;
    if (prompt_row < region_top) prompt_row = region_top;
    if (prompt_row > region_bottom) prompt_row = region_bottom;

    anim_logo_pinned = logo_pinned;
    anim_logo_live = logo_live;
    anim_region_top = region_top;
    anim_region_bottom = region_bottom;
    anim_prompt_row = prompt_row;
    hud_enabled = hud;
    hud_row = rows;
    hud_force = 1;

    printf("\033[%d;%dr", region_top, region_bottom);
    if (hud) {
        hud_cpu_update();
        hud_mem_update();
        hud_draw();
    }
    printf("\033[%d;1H\033[s", prompt_row);
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
    /* Salva o cursor: alguns terminais (kitty incluso) movem o cursor para o
       topo da regiao ao aplicar DECSTBM. Sem isso o prompt volta pra cima. */
    printf("\033[s\033[%d;%dr", anim_region_top, anim_region_bottom);
    hud_force = 1;
    hud_tick();
    printf("\033[u%s", theme_text);
    fflush(stdout);
}

static void anim_cleanup(void) {
    if (!isatty(STDOUT_FILENO)) return;
    printf("\033[?2026l\033[r");
    if (hud_enabled && hud_row > 0) printf("\033[%d;1H\033[K", hud_row);
    if (anim_rows > 0) printf("\033[%d;1H", anim_rows);
    printf("\033[0m");
    fflush(stdout);
}

/* Chamado pelo readline enquanto ele espera entrada. */
static int anim_tick(void) {
    if (sigint_seen) {
        sigint_hook();
        return 0;
    }
    if (anim_poll_resize()) {
        rl_set_screen_size(anim_rows, anim_cols);
        int ok = (anim_cols >= ANIM_MIN_COLS && anim_rows >= ANIM_MIN_ROWS);
        if (ok) {
            anim_enabled = 1;
            anim_render_all();
        } else {
            anim_enabled = 0;
            hud_enabled = 0;
            sugg_forget();
            printf("\033[r\033[%d;1H\033[K", anim_rows > 0 ? anim_rows : 24);
            fflush(stdout);
        }
        rl_on_new_line();
        redisplay_now();
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
    galaxy_phase += 0.035;
    if (galaxy_phase >= 1.0) galaxy_phase -= 1.0;
    galaxy_angle += 0.015;
    if (galaxy_angle >= 2.0 * GALAXY_PI) galaxy_angle -= 2.0 * GALAXY_PI;
    /* Esconde o cursor enquanto os desenhos intermediários sao enviados: o
     * VTE nao entende ?2026 e pode repintar no meio do frame, deixando um
     * "fantasma" do cursor piscando em cima do banner. */
    printf("\033[?2026h\033[?25l\033[s");
    anim_draw_stars();
    if (anim_logo_live) {
        draw_block(logo_lines, 7, ANIM_HEADER_ROWS + 1, anim_cols, anim_offset);
    }
    hud_tick();
    printf("\033[u%s\033[?25h\033[?2026l", theme_text);
    fflush(stdout);
    return 0;
}

/* Ctrl+L do readline: limpa e redesenha o cabecalho sem perder a regiao. */
static int anim_clear_key(int count, int key) {
    (void)count; (void)key;
    anim_clear_screen();
    rl_on_new_line();
    redisplay_now();
    return 0;
}

static void print_banner(void) {
    if (!isatty(STDOUT_FILENO)) return;
    anim_read_size();
    /* Limpa a tela visivel e tambem o scrollback antes do splash. */
    printf("\033[r\033[H\033[2J\033[3J\033[H");
    fflush(stdout);
    for (int frame = 0; frame < 12; frame++) {
        printf("\033[2J\033[H");
        int off = frame * 3;
        galaxy_draw_block(1, anim_cols, frame * 0.12,
                          galaxy_angle + frame * 0.015);
        printf("\033[%d;1H\033[K", ANIM_HEADER_ROWS);
        if (anim_cols >= LOGO_MIN_COLS) {
            for (int i = 0; i < 7; i++) {
                printf("\033[%d;1H", ANIM_HEADER_ROWS + 1 + i);
                print_gradient(logo_lines[i], off);
            }
        }
        fflush(stdout);
        struct timespec ts = {0, 80000000L};
        nanosleep(&ts, NULL);
    }
    galaxy_angle += 12 * 0.015;
    if (galaxy_angle >= 2.0 * GALAXY_PI) galaxy_angle -= 2.0 * GALAXY_PI;
}

/* ================================================================== */
/* i18n + /kalc + /lang                                                */
/* ================================================================== */

typedef enum {
    LANG_EN = 0,
    LANG_PT = 1
} ShellLang;

static ShellLang current_lang = LANG_EN;

/* Retorna o texto no idioma atual. */
static const char *tr(const char *en, const char *pt) {
    return current_lang == LANG_PT ? pt : en;
}

static void lang_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.void_shell_lang", home ? home : ".");
}

static void lang_load(void) {
    char path[4096], buf[32];
    lang_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "pt", 2) == 0) current_lang = LANG_PT;
        else if (strncmp(buf, "en", 2) == 0) current_lang = LANG_EN;
    }
    fclose(f);
}

static void lang_save(void) {
    char path[4096];
    lang_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", current_lang == LANG_PT ? "pt" : "en");
    fclose(f);
}

/* ================================================================== */
/* HUD: barra de status fixa na ultima linha                          */
/* ================================================================== */

static void hud_path(char *out, size_t n) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) { snprintf(out, n, "?"); return; }
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t hl = strlen(home);
        if (strncmp(cwd, home, hl) == 0 && (cwd[hl] == '/' || cwd[hl] == '\0')) {
            snprintf(out, n, "~%s", cwd + hl);
            return;
        }
    }
    snprintf(out, n, "%s", cwd);
}

/* Executa um comando e captura a saida (sem shell, sem herdar o terminal). */
static int hud_capture(const char *const argv[], char *out, size_t cap) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, STDERR_FILENO); close(dn); }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fds[1]);
    size_t total = 0;
    for (;;) {
        char sink[256];
        char *dst = (total + 1 < cap) ? out + total : sink;
        size_t room = (total + 1 < cap) ? cap - 1 - total : sizeof(sink);
        ssize_t r = read(fds[0], dst, room);
        if (r > 0) { if (dst == out) total += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    out[total] = '\0';
    close(fds[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return 0;
}

static int hud_is_repo(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    for (;;) {
        char p[PATH_MAX + 8];
        struct stat st;
        snprintf(p, sizeof(p), "%s/.git", cwd);
        if (stat(p, &st) == 0) return 1;
        char *slash = strrchr(cwd, '/');
        if (!slash || slash == cwd) break;
        *slash = '\0';
    }
    return 0;
}

/* Branch + sujeira do git, com cache curto para nao pesar na digitacao. */
static void hud_git(char *branch, size_t n, int *dirty) {
    static char cached_cwd[PATH_MAX];
    static char cached_branch[128];
    static int cached_dirty = 0;
    static int valid = 0;
    static long long last_ms = -1000000;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';

    if (!valid || strcmp(cwd, cached_cwd) != 0 || ms - last_ms >= 2000) {
        snprintf(cached_cwd, sizeof(cached_cwd), "%s", cwd);
        cached_branch[0] = '\0';
        cached_dirty = 0;
        last_ms = ms;
        if (hud_is_repo()) {
            char out[2048];
            const char *argv[] = {
                "git", "--no-optional-locks", "status", "--porcelain=v1",
                "--branch", "--untracked-files=no", NULL
            };
            if (hud_capture(argv, out, sizeof(out)) == 0) {
                char *nl = strchr(out, '\n');
                char *rest = nl ? nl + 1 : out + strlen(out);
                if (nl) *nl = '\0';
                if (strncmp(out, "## ", 3) == 0) {
                    char *b = out + 3;
                    if (strncmp(b, "No commits yet on ", 18) == 0) b += 18;
                    char *cut = strstr(b, "...");
                    if (!cut) cut = strchr(b, ' ');
                    if (cut) *cut = '\0';
                    snprintf(cached_branch, sizeof(cached_branch), "%s", b);
                }
                for (char *p = rest; *p; ) {
                    char *e = strchr(p, '\n');
                    if (e) *e = '\0';
                    if (*p) cached_dirty = 1;
                    if (!e) break;
                    p = e + 1;
                }
            }
        }
        valid = 1;
    }
    snprintf(branch, n, "%s", cached_branch);
    *dirty = cached_dirty;
}

static void hud_cpu_update(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { hud_cpu_pct = -1; return; }
    char line[256];
    long long v[8] = {0};
    int got = 0;
    if (fgets(line, sizeof(line), f))
        got = sscanf(line, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
                     &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (got < 4) { hud_cpu_pct = -1; return; }
    long long idle = v[3] + (got > 4 ? v[4] : 0);
    long long total = 0;
    for (int i = 0; i < got && i < 8; i++) total += v[i];
    if (hud_prev_total > 0 && total > hud_prev_total) {
        long long dt = total - hud_prev_total;
        long long di = idle - hud_prev_idle;
        hud_cpu_pct = (int)(100 * (dt - di) / dt);
        if (hud_cpu_pct < 0) hud_cpu_pct = 0;
        if (hud_cpu_pct > 100) hud_cpu_pct = 100;
    }
    hud_prev_total = total;
    hud_prev_idle = idle;
}

static void hud_mem_update(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { hud_mem_used_mb = hud_mem_total_mb = -1; return; }
    char line[256];
    long total_kb = -1, avail_kb = -1;
    while (fgets(line, sizeof(line), f)) {
        long val;
        if (sscanf(line, "MemTotal: %ld kB", &val) == 1) { total_kb = val; continue; }
        if (sscanf(line, "MemAvailable: %ld kB", &val) == 1) { avail_kb = val; continue; }
    }
    fclose(f);
    if (total_kb <= 0 || avail_kb < 0) {
        hud_mem_used_mb = hud_mem_total_mb = -1;
        return;
    }
    hud_mem_total_mb = total_kb / 1024;
    hud_mem_used_mb = (total_kb - avail_kb) / 1024;
}

/* Desenha a barra. O chamador cuida de salvar/restaurar o cursor. */
static void hud_draw(void) {
    if (!hud_enabled || anim_cols < 20) return;
    int cols = anim_cols;

    char path[PATH_MAX];
    hud_path(path, sizeof(path));
    char branch[128];
    int dirty = 0;
    hud_git(branch, sizeof(branch), &dirty);

    char head[PATH_MAX + 160];
    int hn = snprintf(head, sizeof(head), " %s", path);
    if (branch[0] && hn > 0 && hn < (int)sizeof(head) - 4)
        hn += snprintf(head + hn, sizeof(head) - hn, "  %s%s",
                       branch, dirty ? "*" : "");

    char stat[64] = "";
    if (last_duration_ms >= 0) {
        if (last_duration_ms < 1000) {
            if (last_status == 0)
                snprintf(stat, sizeof(stat), "ok %ldms", last_duration_ms);
            else
                snprintf(stat, sizeof(stat), "err %d %ldms",
                         last_status, last_duration_ms);
        } else {
            double secs = (double)last_duration_ms / 1000.0;
            if (last_status == 0)
                snprintf(stat, sizeof(stat), "ok %.2fs", secs);
            else
                snprintf(stat, sizeof(stat), "err %d %.2fs", last_status, secs);
        }
    }

    int running = 0;
    for (int i = 0; i < njobs; i++)
        if (jobs[i].running == 1) running++;

    char pieces[6][64];
    int np = 0;
    if (running > 0)
        snprintf(pieces[np++], sizeof(pieces[0]), "%d job%s",
                 running, running == 1 ? "" : "s");
    snprintf(pieces[np++], sizeof(pieces[0]), "%s",
             current_lang == LANG_PT ? "PT" : "EN");
    time_t t = time(NULL);
    struct tm tm;
    if (localtime_r(&t, &tm))
        strftime(pieces[np++], sizeof(pieces[0]), "%H:%M", &tm);
    if (hud_cpu_pct >= 0)
        snprintf(pieces[np++], sizeof(pieces[0]), "cpu %d%%", hud_cpu_pct);
    if (hud_mem_total_mb > 0)
        snprintf(pieces[np++], sizeof(pieces[0]), "ram %.1fG",
                 (double)hud_mem_used_mb / 1024.0);

    /* Reserva no maximo metade da linha para o caminho/branch. */
    int max_head = cols / 2;
    if (max_head < 12) max_head = 12;
    int hl0 = (int)strlen(head);
    if (hl0 > max_head) {
        char tmp[PATH_MAX + 160];
        snprintf(tmp, sizeof(tmp), "...%s", head + hl0 - (max_head - 3));
        snprintf(head, sizeof(head), "%s", tmp);
    }

    int left_len = (int)strlen(head) + (stat[0] ? (int)strlen(stat) + 1 : 0);
    char right[512] = "";
    int rn = 0;
    for (int i = 0; i < np; i++) {
        int plen = (int)strlen(pieces[i]);
        int need = rn + (rn ? 2 : 0) + plen;
        if (left_len + need + 2 > cols) break;
        if (rn) { strcat(right, "  "); rn += 2; }
        strcat(right, pieces[i]);
        rn += plen;
    }

    /* Se ainda nao couber, encurta o caminho mantendo o final. */
    if (left_len + 1 > cols) {
        int keep = cols - 1 - (stat[0] ? (int)strlen(stat) + 1 : 0);
        if (keep < 8) { stat[0] = '\0'; keep = cols - 1; }
        int hl = (int)strlen(head);
        if (keep > 0 && hl > keep) {
            if (keep > 3) {
                char tmp[PATH_MAX + 160];
                snprintf(tmp, sizeof(tmp), "...%s", head + hl - (keep - 3));
                snprintf(head, sizeof(head), "%s", tmp);
            } else {
                head[keep] = '\0';
            }
        }
    }

    int pad = cols - (int)strlen(head)
                  - (stat[0] ? (int)strlen(stat) + 1 : 0) - rn;
    if (pad < 0) pad = 0;

    printf("\033[%d;1H%s", hud_row, theme_hud_bg);
    printf("%s%s", theme_text, head);
    if (stat[0])
        printf(" %s%s", last_status == 0 ? theme_ok : theme_err, stat);
    for (int i = 0; i < pad; i++) putchar(' ');
    if (rn) printf("%s%s", theme_hud_dim, right);
    printf("\033[0m");
}

static void hud_tick(void) {
    if (!hud_enabled) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (!hud_force && ms - hud_last_ms < HUD_REFRESH_MS) return;
    hud_force = 0;
    hud_last_ms = ms;
    hud_cpu_update();
    hud_mem_update();
    hud_draw();
}

static int lang_parse(const char *s) {
    if (!s) return -1;
    if (strcasecmp(s, "en") == 0 || strcasecmp(s, "english") == 0 ||
        strcasecmp(s, "ingles") == 0 || strcasecmp(s, "inglês") == 0)
        return LANG_EN;
    if (strcasecmp(s, "pt") == 0 || strcasecmp(s, "pt-br") == 0 ||
        strcasecmp(s, "ptbr") == 0 || strcasecmp(s, "portugues") == 0 ||
        strcasecmp(s, "português") == 0 || strcasecmp(s, "portuguese") == 0)
        return LANG_PT;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Conteudo do /kalc (ingles + portugues do Brasil)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *title_en;
    const char *title_pt;
    const char *const *lines_en;
    int nlines_en;
    const char *const *lines_pt;
    int nlines_pt;
} KalcTopic;

static const char *const kalc_sum_en[] = {
    "# What it is",
    "Addition means putting quantities together. The result is the sum or total.",
    "a + b = c, where a and b are the addends and c is the sum.",
    "",
    "# Examples: from basic to advanced",
    "- 2 + 3 = 5",
    "- 17 + 25 = 42",
    "- 248 + 137 = 385",
    "- 1/2 + 1/3 = 5/6  (fractions: make the denominators equal)",
    "- 0.75 + 1.50 = 2.25  (line up the decimal points)",
    "- 7 + (-3) = 4  (adding a negative is subtracting)",
    "",
    "# Rules and properties",
    "- Commutative: a + b = b + a",
    "- Associative: (a + b) + c = a + (b + c)",
    "- Identity: a + 0 = a",
    "- Opposite: a + (-a) = 0",
    "- Fractions: a/b + c/d = (ad + cb) / bd",
    "",
    "# Tricks",
    "- Add the tens first, then the ones.",
    "- To add 9, 19, 29...: add 10, 20, 30 and subtract 1.",
    "- For 98, 97...: round up to 100 and adjust.",
    "- 1 + 2 + ... + n = n(n+1)/2.",
    "- Pairs that make 10: 1+9, 2+8, 3+7, 4+6, 5+5.",
    "",
    "# Common mistakes",
    "- Not lining up the decimal places.",
    "- Adding numerators and denominators of fractions directly.",
    "- Getting the sign wrong when adding negatives.",
};

static const char *const kalc_sum_pt[] = {
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

static const char *const kalc_sub_en[] = {
    "# What it is",
    "Subtraction means taking one quantity away from another: a - b = c.",
    "a is the minuend, b the subtrahend, and c the difference.",
    "",
    "# Examples: from basic to advanced",
    "- 9 - 4 = 5",
    "- 52 - 18 = 34",
    "- 300 - 125 = 175",
    "- 3/4 - 1/4 = 2/4 = 1/2",
    "- 2.5 - 0.75 = 1.75",
    "- 5 - (-3) = 8  (minus a negative becomes plus)",
    "- 10 - 25 = -15",
    "",
    "# Rules and properties",
    "- Not commutative: a - b != b - a (in general).",
    "- Subtracting is adding the opposite: a - b = a + (-b).",
    "- a - 0 = a  and  a - a = 0.",
    "- Signs: -(-x) = x  and  -(a + b) = -a - b.",
    "- Fractions: a/b - c/d = (ad - cb) / bd.",
    "",
    "# Tricks",
    "- To subtract 9: subtract 10 and give 1 back.",
    "- To subtract 99: subtract 100 and give 1 back.",
    "- Complete the ten: 63 - 47 = 63 - 50 + 3 = 16.",
    "- Check with addition: if a - b = c, then c + b = a.",
    "- With money, think of the change.",
    "",
    "# Common mistakes",
    "- Reversing the order in 'how many are missing' questions.",
    "- Getting the sign wrong with parentheses.",
    "- Not lining up the decimal points.",
};

static const char *const kalc_sub_pt[] = {
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

static const char *const kalc_mul_en[] = {
    "# What it is",
    "Multiplication is adding equal parts: a x b = c.",
    "3 x 4 = 3 + 3 + 3 + 3 = 12.",
    "",
    "# Examples: from basic to advanced",
    "- 6 x 7 = 42",
    "- 12 x 15 = 180",
    "- 25 x 4 = 100",
    "- 1/2 x 1/3 = 1/6",
    "- 0.5 x 0.2 = 0.10",
    "- (-3) x 4 = -12",
    "- (-3) x (-4) = 12",
    "",
    "# Rules and properties",
    "- Commutative: a x b = b x a",
    "- Associative: (a x b) x c = a x (b x c)",
    "- Distributive: a x (b + c) = ab + ac",
    "- Identity: a x 1 = a   |   Zero: a x 0 = 0",
    "- Signs: (+)(+) = +, (-)(-) = +, (+)(-) = -",
    "- Fractions: (a/b) x (c/d) = ac/bd",
    "",
    "# Tricks",
    "- 9 times table: 9 x n = 10n - n.",
    "- x5 = half of x10; x25 = x100/4; x50 = x100/2.",
    "- x11 (2 digits): 23 x 11 = 2 (2+3) 3 = 253.",
    "- (a+b)(a-b) = a^2 - b^2.",
    "- x4 = double twice.",
    "",
    "# Common mistakes",
    "- Thinking (-)(-) gives a negative.",
    "- Multiplying numerator by denominator.",
    "- Misplacing the decimal point: 0.2 x 0.3 = 0.06 (two places).",
};

static const char *const kalc_mul_pt[] = {
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

static const char *const kalc_div_en[] = {
    "# What it is",
    "Division means splitting into equal parts: a / b = c.",
    "a is the dividend, b the divisor, and c the quotient.",
    "The remainder is what is left when the division is not exact.",
    "",
    "# Examples: from basic to advanced",
    "- 12 / 3 = 4",
    "- 45 / 5 = 9",
    "- 100 / 8 = 12.5",
    "- 7 / 2 = 3.5",
    "- (1/2) / (1/4) = 2",
    "- 10 / 0 = undefined (does not exist)",
    "",
    "# Rules and properties",
    "- Not commutative: a / b != b / a.",
    "- Dividing is multiplying by the inverse: a / b = a x (1/b).",
    "- a / 1 = a   |   a / a = 1 (a != 0)   |   0 / a = 0.",
    "- Signs: (+)/(+) = +, (-)/(-) = +, (+)/(-) = -.",
    "- Fractions: (a/b) / (c/d) = (a/b) x (d/c).",
    "- With remainder: a = bq + r, with 0 <= r < b.",
    "",
    "# Tricks",
    "- /5 = x2 /10;  /25 = x4 /100;  /50 = x2 /100.",
    "- To divide by 4: divide by 2 twice.",
    "- Divisibility: 3 (sum of digits), 9 (same),",
    "  5 (ends in 0 or 5), 2 (even).",
    "- Estimate by rounding and adjust at the end.",
    "- A rule of three is just a proportion.",
    "",
    "# Common mistakes",
    "- Dividing by zero.",
    "- Swapping dividend and divisor.",
    "- Forgetting the remainder in integer division.",
    "- Not flipping the bottom fraction.",
};

static const char *const kalc_div_pt[] = {
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
    {
        "Sum", "Soma",
        kalc_sum_en, (int)(sizeof(kalc_sum_en) / sizeof(kalc_sum_en[0])),
        kalc_sum_pt, (int)(sizeof(kalc_sum_pt) / sizeof(kalc_sum_pt[0])),
    },
    {
        "Subtraction", "Subtração",
        kalc_sub_en, (int)(sizeof(kalc_sub_en) / sizeof(kalc_sub_en[0])),
        kalc_sub_pt, (int)(sizeof(kalc_sub_pt) / sizeof(kalc_sub_pt[0])),
    },
    {
        "Multiplication", "Multiplicação",
        kalc_mul_en, (int)(sizeof(kalc_mul_en) / sizeof(kalc_mul_en[0])),
        kalc_mul_pt, (int)(sizeof(kalc_mul_pt) / sizeof(kalc_mul_pt[0])),
    },
    {
        "Division", "Divisão",
        kalc_div_en, (int)(sizeof(kalc_div_en) / sizeof(kalc_div_en[0])),
        kalc_div_pt, (int)(sizeof(kalc_div_pt) / sizeof(kalc_div_pt[0])),
    },
};
#define KALC_NTOPICS ((int)(sizeof(kalc_topics) / sizeof(kalc_topics[0])))
#define KALC_MENU_TOP 5
#define KALC_MENU_QUIT (KALC_MENU_TOP + KALC_NTOPICS + 1)

#define KALC_PURPLE theme_head
#define KALC_TEXT   theme_text
#define KALC_DIM    theme_dim
#define KALC_SEL    theme_sel
#define KALC_RESET  "\033[0m"

/* Guia rapido mostrado ao abrir o shell e pelo /tutorial. */
static void print_tutorial(void) {
    int color = isatty(STDOUT_FILENO);
    int full = anim_logo_pinned || !color;
    const char *C = color ? KALC_PURPLE : "";
    const char *T = color ? KALC_TEXT : "";
    const char *R = color ? KALC_RESET : "";

    if (!full) {
        printf("%s%s%s\n", T,
               tr("Ctrl+P palette · /kalc · /lang · /theme · /tutorial",
                  "Ctrl+P paleta · /kalc · /lang · /theme · /tutorial"),
               R);
        fflush(stdout);
        return;
    }

    printf("%s%s%s\n", C,
           tr("Welcome to void-shell! Quick guide:",
              "Bem-vindo ao void-shell! Guia rápido:"), R);
    printf("  %s/kalc%s      %s%s%s\n", C, R, T,
           tr("interactive math manual", "manual interativo de matemática"), R);
    printf("  %s/lang%s      %s%s%s\n", C, R, T,
           tr("change language (EN / PT)", "troca o idioma (EN / PT)"), R);
    printf("  %s/theme%s     %s%s%s\n", C, R, T,
           tr("change colors (galaxy, matrix...)",
              "muda as cores (galaxy, matrix...)"), R);
    printf("  %sCtrl+P%s     %s%s%s\n", C, R, T,
           tr("command palette (search & run)", "paleta de comandos (buscar e rodar)"), R);
    printf("  %sclear%s      %s%s%s\n", C, R, T,
           tr("clear the screen (Ctrl+L)", "limpa a tela (Ctrl+L)"), R);
    printf("  %sexit%s       %s%s%s\n", C, R, T,
           tr("leave the shell (Ctrl+D)", "sai do shell (Ctrl+D)"), R);
    printf("  %s↑/↓%s        %s%s%s\n", C, R, T,
           tr("history    Tab  completion", "histórico    Tab  completar"), R);
    printf("  %sCtrl+C%s     %s%s%s\n", C, R, T,
           tr("cancel the current line", "cancela a linha atual"), R);
    printf("  %s→%s          %s%s%s\n", C, R, T,
           tr("accept the gray suggestion (commands, paths)",
              "aceita a sugestão cinza (comandos, caminhos)"), R);
    printf("  %s/tutorial%s  %s%s%s\n", C, R, T,
           tr("show this guide again", "mostra este guia de novo"), R);
    fflush(stdout);
}

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

static const char *kalc_title(const KalcTopic *t) {
    return current_lang == LANG_PT ? t->title_pt : t->title_en;
}

static const char *const *kalc_body(const KalcTopic *t, int *n) {
    if (current_lang == LANG_PT) {
        *n = t->nlines_pt;
        return t->lines_pt;
    }
    *n = t->nlines_en;
    return t->lines_en;
}

static int kalc_find_topic(const char *s) {
    if (!s) return -1;
    if (s[0] >= '1' && s[0] < '1' + KALC_NTOPICS && s[1] == '\0')
        return s[0] - '1';
    if (strcasecmp(s, "sum") == 0 || strcasecmp(s, "soma") == 0) return 0;
    if (strcasecmp(s, "sub") == 0 || strcasecmp(s, "subtract") == 0 ||
        strcasecmp(s, "subtraction") == 0 || strcasecmp(s, "subtracao") == 0 ||
        strcasecmp(s, "subtração") == 0) return 1;
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
        int n = 0;
        const char *const *lines = kalc_body(t, &n);
        printf("%s\n", kalc_title(t));
        for (int i = 0; i < n; i++) {
            const char *l = lines[i];
            if (l[0] == '#' && l[1] == ' ') printf("\n%s\n", l + 2);
            else if (l[0] == '-' && l[1] == ' ') printf("  - %s\n", l + 2);
            else printf("%s\n", l);
        }
        return;
    }
    printf("%s\n", tr("KALC :: basic math", "KALC :: matemática básica"));
    for (int i = 0; i < KALC_NTOPICS; i++)
        printf("  [%d] %s\n", i + 1, kalc_title(&kalc_topics[i]));
    printf("\n%s\n", tr("Use /kalc <topic> to open directly (e.g.: /kalc sum).",
                        "Use /kalc <topico> para abrir direto (ex.: /kalc sum)."));
}

static void kalc_render_menu(int sel) {
    kalc_size();
    printf("\033[?2026h\033[2J\033[H");
    printf("\033[2;3H%s%s%s", KALC_PURPLE,
           tr("KALC :: basic math", "KALC :: matemática básica"), KALC_RESET);
    printf("\033[3;3H%s-------------------------%s", KALC_DIM, KALC_RESET);
    for (int i = 0; i < KALC_NTOPICS; i++) {
        printf("\033[%d;3H", KALC_MENU_TOP + i);
        if (i == sel)
            printf("%s > [%d]  %s %s", KALC_SEL, i + 1, kalc_title(&kalc_topics[i]), KALC_RESET);
        else
            printf("   [%d]  %s%s%s", i + 1, KALC_TEXT, kalc_title(&kalc_topics[i]), KALC_RESET);
    }
    printf("\033[%d;3H", KALC_MENU_QUIT);
    if (sel == KALC_NTOPICS)
        printf("%s > [q]  %s %s", KALC_SEL, tr("Quit", "Sair"), KALC_RESET);
    else
        printf("   [q]  %s%s%s", KALC_TEXT, tr("Quit", "Sair"), KALC_RESET);
    printf("\033[%d;3H%s%s%s", KALC_MENU_QUIT + 2, KALC_DIM,
           tr("Click a topic, or use ↑/↓ + Enter.",
              "Clique em um tópico, ou use ↑/↓ + Enter."), KALC_RESET);
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
    int n = 0;
    const char *const *lines = kalc_body(t, &n);
    int body_top = 4;
    int body_rows = rows - body_top;
    if (body_rows < 1) body_rows = 1;
    int max_scroll = n - body_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0) scroll = 0;

    printf("\033[?2026h\033[2J\033[H");
    printf("\033[1;3H%s%s%s", KALC_PURPLE, kalc_title(t), KALC_RESET);
    printf("\033[2;3H%s-------------------------%s", KALC_DIM, KALC_RESET);
    int avail = kalc_cols - 4;
    if (avail < 1) avail = 1;
    for (int i = 0; i < body_rows; i++) {
        int li = scroll + i;
        if (li >= n) break;
        printf("\033[%d;3H", body_top + i);
        const char *raw = lines[li];
        int extra = (raw[0] == '-' && raw[1] == ' ') ? 2 : 0;
        char fit[512];
        const char *line = kalc_fit(raw, fit, sizeof(fit), avail - extra);
        if (line[0] == '#' && line[1] == ' ') {
            printf("%s%s%s", KALC_PURPLE, line + 2, KALC_RESET);
        } else if (line[0] == '-' && line[1] == ' ') {
            printf("%s•%s %s%s", KALC_PURPLE, KALC_TEXT, line + 2, KALC_RESET);
        } else {
            printf("%s%s%s", KALC_TEXT, line, KALC_RESET);
        }
    }
    printf("\033[%d;3H%s[<] %s%s   ↑/↓ %s   q %s   %d/%d",
           rows, KALC_DIM, tr("Back", "Voltar"), KALC_RESET,
           tr("scroll", "rolar"), tr("back", "voltar"),
           scroll + 1, max_scroll + 1);
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

/* ------------------------------------------------------------------ */
/* /lang - menu de idiomas                                             */
/* ------------------------------------------------------------------ */

#define LANG_MENU_TOP    5
#define LANG_MENU_N      2
#define LANG_PHRASE_ROW  8
#define LANG_MENU_QUIT   10
#define LANG_HINT_ROW    12

static const char *const lang_names[LANG_MENU_N] = {
    "English",
    "Português (Brasil)",
};

static void lang_render(int sel) {
    kalc_size();
    printf("\033[?2026h\033[2J\033[H");
    printf("\033[2;3H%s%s%s", KALC_PURPLE,
           tr("LANGUAGE :: shell language", "IDIOMA :: idioma do shell"), KALC_RESET);
    printf("\033[3;3H%s-------------------------%s", KALC_DIM, KALC_RESET);
    for (int i = 0; i < LANG_MENU_N; i++) {
        printf("\033[%d;3H", LANG_MENU_TOP + i);
        const char *cur = ((int)current_lang == i) ? tr("  (current)", "  (atual)") : "";
        if (i == sel)
            printf("%s > [%d]  %s%s %s", KALC_SEL, i + 1, lang_names[i], cur, KALC_RESET);
        else
            printf("   [%d]  %s%s%s%s", i + 1, KALC_TEXT, lang_names[i], cur, KALC_RESET);
    }
    printf("\033[%d;3H%s%s%s", LANG_PHRASE_ROW, KALC_TEXT,
           tr("More language support coming",
              "Mais suporte a idiomas em breve"), KALC_RESET);
    printf("\033[%d;3H", LANG_MENU_QUIT);
    if (sel == LANG_MENU_N)
        printf("%s > [q]  %s %s", KALC_SEL, tr("Cancel", "Cancelar"), KALC_RESET);
    else
        printf("   [q]  %s%s%s", KALC_TEXT, tr("Cancel", "Cancelar"), KALC_RESET);
    printf("\033[%d;3H%s%s%s", LANG_HINT_ROW, KALC_DIM,
           tr("Click a language, or use ↑/↓ + Enter.",
              "Clique em um idioma, ou use ↑/↓ + Enter."), KALC_RESET);
    printf("\033[?2026l");
    fflush(stdout);
}

/* Aplica um idioma. Retorna 1 se mudou. */
static int lang_apply(int lang) {
    if (lang != LANG_EN && lang != LANG_PT) return 0;
    if ((int)current_lang == lang) return 0;
    current_lang = (ShellLang)lang;
    lang_save();
    return 1;
}

/* Retorna 1 se o idioma foi alterado. */
static int lang_run(const char *start) {
    int idx = lang_parse(start);
    if (idx >= 0) return lang_apply(idx);

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("%s\n", tr("LANGUAGE :: shell language", "IDIOMA :: idioma do shell"));
        for (int i = 0; i < LANG_MENU_N; i++)
            printf("  [%d] %s%s\n", i + 1, lang_names[i],
                   (int)current_lang == i ? tr("  (current)", "  (atual)") : "");
        printf("  %s\n", tr("More language support coming",
                            "Mais suporte a idiomas em breve"));
        printf("%s\n", tr("Use /lang en or /lang pt.",
                          "Use /lang en ou /lang pt."));
        return 0;
    }

    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) return 0;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[r\033[2J\033[H\033[?25l\033[?1000h\033[?1006h");
    fflush(stdout);

    int sel = (int)current_lang;
    kalc_size();
    int last_rows = kalc_rows, last_cols = kalc_cols;
    lang_render(sel);

    int changed = 0;
    int running = 1;
    while (running) {
        KalcKey k;
        int got = kalc_read_key(&k, 200);

        kalc_size();
        if (kalc_rows != last_rows || kalc_cols != last_cols) {
            last_rows = kalc_rows;
            last_cols = kalc_cols;
            lang_render(sel);
        }
        if (!got) continue;

        int choose = -1;
        switch (k.type) {
            case K_CHAR:
                if (k.ch == 'q' || k.ch == 'Q' || k.ch == 3) {
                    running = 0;
                } else if (k.ch >= '1' && k.ch < '1' + LANG_MENU_N) {
                    choose = k.ch - '1';
                }
                break;
            case K_UP:
                sel = (sel + LANG_MENU_N) % (LANG_MENU_N + 1);
                lang_render(sel);
                break;
            case K_DOWN:
                sel = (sel + 1) % (LANG_MENU_N + 1);
                lang_render(sel);
                break;
            case K_ENTER:
                if (sel < LANG_MENU_N) choose = sel;
                else running = 0;
                break;
            case K_ESC:
                running = 0;
                break;
            case K_MOUSE:
                if (k.press && k.mb == 0) {
                    if (k.my >= LANG_MENU_TOP && k.my < LANG_MENU_TOP + LANG_MENU_N)
                        choose = k.my - LANG_MENU_TOP;
                    else if (k.my == LANG_MENU_QUIT)
                        running = 0;
                }
                break;
        }
        if (choose >= 0) {
            changed = lang_apply(choose);
            running = 0;
        }
    }

    printf("\033[?1006l\033[?1000l\033[?25h\033[0m\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return changed;
}

/* ------------------------------------------------------------------ */
/* /theme - menu de temas                                             */
/* ------------------------------------------------------------------ */

#define THEME_MENU_TOP     5
#define THEME_MENU_QUIT    (THEME_MENU_TOP + THEME_N + 1)
#define THEME_PREVIEW_ROW  (THEME_MENU_QUIT + 2)
#define THEME_HINT_ROW     (THEME_PREVIEW_ROW + 2)
#define THEME_BG_COL       48
#define THEME_BG_TOP       THEME_MENU_TOP

static void theme_swatch(const Theme *t) {
    for (int i = 0; i < 10; i++) {
        int seg = i * 5 / 10;
        if (seg > 4) seg = 4;
        printf("\033[48;2;%d;%d;%dm ", t->grad[seg][0],
               t->grad[seg][1], t->grad[seg][2]);
    }
    printf(KALC_RESET);
}

static void term_bg_swatch(const int rgb[3]) {
    printf("\033[48;2;%d;%d;%dm    %s", rgb[0], rgb[1], rgb[2], KALC_RESET);
}

static void theme_draw_theme_column(int col, int sel, int focused) {
    printf("\033[2;%dH%s%s%s", col, theme_head,
           tr("THEME :: shell colors", "TEMA :: cores do shell"), KALC_RESET);
    printf("\033[3;%dH%s-------------------------%s", col, theme_dim,
           KALC_RESET);
    for (int i = 0; i < THEME_N; i++) {
        const Theme *t = &themes[i];
        const char *cur = (i == current_theme)
                              ? tr("  (current)", "  (atual)") : "";
        char label[64];
        snprintf(label, sizeof(label), "%s%s", t->name, cur);
        printf("\033[%d;%dH", THEME_MENU_TOP + i, col);
        if (focused && i == sel)
            printf("%s > [%d]  %-24s %s", theme_sel, i + 1, label, KALC_RESET);
        else
            printf("   [%d]  %s%-24s%s", i + 1, theme_text, label, KALC_RESET);
        theme_swatch(t);
    }
    printf("\033[%d;%dH", THEME_MENU_QUIT, col);
    if (focused && sel == THEME_N)
        printf("%s > [q]  %s %s", theme_sel, tr("Cancel", "Cancelar"),
               KALC_RESET);
    else
        printf("   [q]  %s%s%s", theme_text, tr("Cancel", "Cancelar"),
               KALC_RESET);
}

static void theme_draw_bg_column(int col, int bg_sel, int focused) {
    int cur_rgb[3];
    term_bg_current_rgb(cur_rgb);
    printf("\033[2;%dH%svoid-term bg color%s  %s#%02x%02x%02x%s",
           col, theme_head, KALC_RESET, theme_dim,
           cur_rgb[0], cur_rgb[1], cur_rgb[2], KALC_RESET);
    printf("\033[3;%dH%s-------------------------%s", col, theme_dim,
           KALC_RESET);
    for (int i = 0; i < TERM_BG_N; i++) {
        const TermBg *b = &term_bgs[i];
        const char *cur = (!term_bg_custom && i == current_term_bg)
                              ? tr("  (current)", "  (atual)") : "";
        char label[64];
        snprintf(label, sizeof(label), "%s%s", b->name, cur);
        printf("\033[%d;%dH", THEME_BG_TOP + i, col);
        if (focused && i == bg_sel)
            printf("%s > [%d]  %-19s %s", theme_sel, i + 1, label, KALC_RESET);
        else
            printf("   [%d]  %s%-19s%s", i + 1, theme_text, label, KALC_RESET);
        term_bg_swatch(b->rgb);
    }
}

static void theme_render(int sel, int bg_sel, int focus) {
    kalc_size();
    printf("\033[?2026h\033[2J\033[H");

    if (kalc_cols < 80) {
        /* Terminal estreito (ex.: dentro de um split): mostra apenas a
         * coluna em foco; Tab alterna entre elas. */
        if (focus == 0) {
            theme_draw_theme_column(3, sel, 1);
            printf("\033[%d;3H%s%s%s", THEME_HINT_ROW, theme_dim,
                   tr("Tab: terminal bg. Enter applies. q cancels.",
                      "Tab: fundo do terminal. Enter aplica. q cancela."),
                   KALC_RESET);
        } else {
            theme_draw_bg_column(3, bg_sel, 1);
            printf("\033[%d;3H%s%s%s", THEME_HINT_ROW, theme_dim,
                   tr("Tab: shell colors. Enter applies. q cancels.",
                      "Tab: cores do shell. Enter aplica. q cancela."),
                   KALC_RESET);
        }
        printf("\033[?2026l");
        fflush(stdout);
        return;
    }

    theme_draw_theme_column(3, sel, focus == 0);
    theme_draw_bg_column(THEME_BG_COL, bg_sel, focus == 1);

    const Theme *t = &themes[sel < THEME_N ? sel : current_theme];
    printf("\033[%d;3H\033[38;2;%d;%d;%dmvoid-shell> "
           "\033[38;2;%d;%d;%dmecho hello  "
           "\033[38;2;%d;%d;%dmok 12ms" KALC_RESET,
           THEME_PREVIEW_ROW,
           t->prompt[0], t->prompt[1], t->prompt[2],
           t->text[0], t->text[1], t->text[2],
           t->ok[0], t->ok[1], t->ok[2]);
    printf("\033[%d;3H%s%s%s", THEME_HINT_ROW, theme_dim,
           tr("Click or ↑/↓ + Enter. Tab/←/→ column. q cancels.",
              "Clique ou ↑/↓ + Enter. Tab/←/→ coluna. q cancela."), KALC_RESET);
    printf("\033[?2026l");
    fflush(stdout);
}

/* Retorna bit 0 = tema alterado, bit 1 = fundo do terminal alterado. */
static int theme_run(const char *start) {
    int initial_focus = 0;

    if (start && strncmp(start, "bg", 2) == 0 &&
        (start[2] == '\0' || start[2] == ' ' || start[2] == '\t')) {
        const char *arg = start + 2;
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg) {
            int idx = term_bg_find(arg);
            if (idx >= 0) return term_bg_apply(idx) ? 2 : 0;
            if (term_bg_apply_hex(arg)) return 2;
            printf("%s\n", tr("Unknown background. Use a name or #rrggbb.",
                              "Fundo desconhecido. Use um nome ou #rrggbb."));
            return 0;
        }
        initial_focus = 1;
    } else {
        int idx = theme_find(start);
        if (idx >= 0) return theme_apply(idx) ? 1 : 0;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("%s\n", tr("THEME :: shell colors", "TEMA :: cores do shell"));
        for (int i = 0; i < THEME_N; i++)
            printf("  [%d] %s%s\n", i + 1, themes[i].name,
                   i == current_theme ? tr("  (current)", "  (atual)") : "");
        printf("%s\n", tr("Use /theme <name>: galaxy, matrix, synthwave, mono, ocean, sunset, dracula, nord, amber.",
                          "Use /theme <nome>: galaxy, matrix, synthwave, mono, ocean, sunset, dracula, nord, amber."));
        printf("\nvoid-term bg color\n");
        for (int i = 0; i < TERM_BG_N; i++)
            printf("  [%d] %-10s #%02x%02x%02x%s\n", i + 1,
                   term_bgs[i].name, term_bgs[i].rgb[0], term_bgs[i].rgb[1],
                   term_bgs[i].rgb[2],
                   (!term_bg_custom && i == current_term_bg)
                       ? tr("  (current)", "  (atual)") : "");
        printf("%s\n", tr("Use /theme bg <name|#rrggbb>.",
                          "Use /theme bg <nome|#rrggbb>."));
        return 0;
    }

    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) return 0;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[r\033[2J\033[H\033[?25l\033[?1000h\033[?1006h");
    fflush(stdout);

    int sel = current_theme;
    int bg_sel = current_term_bg;
    int focus = initial_focus;
    kalc_size();
    int last_rows = kalc_rows, last_cols = kalc_cols;
    theme_render(sel, bg_sel, focus);

    int changed = 0;
    int running = 1;
    while (running) {
        KalcKey k;
        int got = kalc_read_key(&k, 200);

        kalc_size();
        if (kalc_rows != last_rows || kalc_cols != last_cols) {
            last_rows = kalc_rows;
            last_cols = kalc_cols;
            theme_render(sel, bg_sel, focus);
        }
        if (!got) continue;

        int choose = -1;
        switch (k.type) {
            case K_CHAR:
                if (k.ch == 'q' || k.ch == 'Q' || k.ch == 3) {
                    running = 0;
                } else if (k.ch == '\t') {
                    focus = !focus;
                    theme_render(sel, bg_sel, focus);
                } else if (focus == 0 && k.ch >= '1' && k.ch < '1' + THEME_N) {
                    choose = k.ch - '1';
                } else if (focus == 1 && k.ch >= '1' && k.ch < '1' + TERM_BG_N) {
                    bg_sel = k.ch - '1';
                    if (term_bg_apply(bg_sel)) changed |= 2;
                    theme_render(sel, bg_sel, focus);
                }
                break;
            case K_UP:
                if (focus == 0) sel = (sel + THEME_N) % (THEME_N + 1);
                else bg_sel = (bg_sel + TERM_BG_N) % TERM_BG_N;
                theme_render(sel, bg_sel, focus);
                break;
            case K_DOWN:
                if (focus == 0) sel = (sel + 1) % (THEME_N + 1);
                else bg_sel = (bg_sel + 1) % TERM_BG_N;
                theme_render(sel, bg_sel, focus);
                break;
            case K_LEFT:
                if (focus != 0) {
                    focus = 0;
                    theme_render(sel, bg_sel, focus);
                }
                break;
            case K_RIGHT:
                if (focus != 1) {
                    focus = 1;
                    theme_render(sel, bg_sel, focus);
                }
                break;
            case K_ENTER:
                if (focus == 0) {
                    if (sel < THEME_N) choose = sel;
                    else running = 0;
                } else {
                    if (term_bg_apply(bg_sel)) changed |= 2;
                    theme_render(sel, bg_sel, focus);
                }
                break;
            case K_ESC:
                running = 0;
                break;
            case K_MOUSE:
                if (k.press && k.mb == 0) {
                    if (k.mx >= THEME_BG_COL &&
                        k.my >= THEME_BG_TOP &&
                        k.my < THEME_BG_TOP + TERM_BG_N) {
                        focus = 1;
                        bg_sel = k.my - THEME_BG_TOP;
                        if (term_bg_apply(bg_sel)) changed |= 2;
                        theme_render(sel, bg_sel, focus);
                    } else if (k.mx < THEME_BG_COL &&
                               k.my >= THEME_MENU_TOP &&
                               k.my < THEME_MENU_TOP + THEME_N) {
                        focus = 0;
                        sel = k.my - THEME_MENU_TOP;
                        choose = sel;
                    } else if (k.my == THEME_MENU_QUIT) {
                        running = 0;
                    }
                }
                break;
        }
        if (choose >= 0) {
            if (theme_apply(choose)) changed |= 1;
            running = 0;
        }
    }

    printf("\033[?1006l\033[?1000l\033[?25h\033[0m\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return changed;
}

/* ------------------------------------------------------------------ */
/* Command palette (Ctrl+P)                                           */
/* ------------------------------------------------------------------ */

#define PAL_MAX       900
#define PAL_QUERY_LEN 64
#define PAL_LIST_TOP  6

typedef struct {
    char *text;
    const char *desc;
} PalItem;

static PalItem pal_items[PAL_MAX];
static int pal_nitems;
static int pal_match[PAL_MAX];
static int pal_score_v[PAL_MAX];
static int pal_nmatch;
static char pal_query[PAL_QUERY_LEN];
static char pal_result[1024];
static int pal_sel;
static int pal_top;

static void pal_add(const char *text, const char *desc) {
    if (!text || !*text || pal_nitems >= PAL_MAX) return;
    for (int i = 0; i < pal_nitems; i++)
        if (strcmp(pal_items[i].text, text) == 0) return;
    pal_items[pal_nitems].text = strdup(text);
    pal_items[pal_nitems].desc = desc;
    pal_nitems++;
}

static void pal_add_history(void) {
    HIST_ENTRY **h = history_list();
    if (!h) return;
    int n = history_length;
    int start = n > 120 ? n - 120 : 0;
    for (int i = n - 1; i >= start; i--) {
        const char *line = h[i] ? h[i]->line : NULL;
        if (!line) continue;
        while (*line == ' ' || *line == '\t') line++;
        if (*line) pal_add(line, tr("history", "histórico"));
    }
}

static void pal_add_path(void) {
    const char *path = getenv("PATH");
    if (!path) return;
    char *copy = strdup(path);
    if (!copy) return;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir;
         dir = strtok_r(NULL, ":", &save)) {
        if (pal_nitems >= PAL_MAX) break;
        DIR *d = opendir(*dir ? dir : ".");
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && pal_nitems < PAL_MAX) {
            if (e->d_name[0] == '.') continue;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", *dir ? dir : ".", e->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (!S_ISREG(st.st_mode) || (st.st_mode & 0111) == 0) continue;
            pal_add(e->d_name, tr("path", "path"));
        }
        closedir(d);
    }
    free(copy);
}

static void pal_collect(void) {
    static const char *const builtins[] = {
        "cd", "pwd", "exit", "echo", "clear", "jobs", "fg", "bg", "export",
    };
    static const char *const slashes[] = {
        "/kalc", "/lang", "/theme", "/theme bg", "/tutorial", "/help",
    };
    static const char *const snippets[] = {
        "git status", "git log --oneline", "git diff", "git add .",
        "git commit -m \"\"", "git push", "git pull", "git switch -c ",
        "ls -la", "find . -name ", "grep -rn ",
    };

    pal_nitems = 0;
    for (int i = 0; i < (int)(sizeof(slashes) / sizeof(slashes[0])); i++)
        pal_add(slashes[i], tr("command", "comando"));
    for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); i++)
        pal_add(builtins[i], tr("builtin", "nativo"));
    for (int i = 0; i < (int)(sizeof(snippets) / sizeof(snippets[0])); i++)
        pal_add(snippets[i], tr("shortcut", "atalho"));
    pal_add_history();
    pal_add_path();
}

static void pal_free(void) {
    for (int i = 0; i < pal_nitems; i++) free(pal_items[i].text);
    pal_nitems = 0;
}

/* Fuzzy: os caracteres da busca precisam aparecer em ordem no texto. */
static int pal_fuzzy(const char *text, const char *q) {
    if (!*q) return 0;
    int score = 0, ti = 0, last = -2;
    for (int qi = 0; q[qi]; qi++) {
        unsigned char c = (unsigned char)tolower((unsigned char)q[qi]);
        int found = -1;
        for (int t = ti; text[t]; t++) {
            if ((unsigned char)tolower((unsigned char)text[t]) == c) {
                found = t;
                break;
            }
        }
        if (found < 0) return -1;
        score += 10;
        if (found == last + 1) score += 8;
        if (found == 0 || text[found - 1] == ' ' || text[found - 1] == '/')
            score += 6;
        score -= found - ti;
        last = found;
        ti = found + 1;
    }
    score -= (int)strlen(text) / 4;
    return score;
}

static void pal_refresh(void) {
    pal_nmatch = 0;
    for (int i = 0; i < pal_nitems; i++) {
        int s = pal_fuzzy(pal_items[i].text, pal_query);
        if (s < 0) continue;
        int j = pal_nmatch;
        while (j > 0 && pal_score_v[j - 1] < s) {
            pal_score_v[j] = pal_score_v[j - 1];
            pal_match[j] = pal_match[j - 1];
            j--;
        }
        pal_score_v[j] = s;
        pal_match[j] = i;
        pal_nmatch++;
    }
    if (pal_sel >= pal_nmatch) pal_sel = pal_nmatch > 0 ? pal_nmatch - 1 : 0;
    if (pal_sel < 0) pal_sel = 0;
}

static void pal_render(void) {
    kalc_size();
    int rows = kalc_rows, cols = kalc_cols;
    int list_h = rows - PAL_LIST_TOP - 1;
    if (list_h < 1) list_h = 1;
    int max_top = pal_nmatch - list_h;
    if (max_top < 0) max_top = 0;
    if (pal_top > max_top) pal_top = max_top;
    if (pal_sel < pal_top) pal_top = pal_sel;
    if (pal_sel >= pal_top + list_h) pal_top = pal_sel - list_h + 1;

    printf("\033[?2026h\033[2J\033[H");
    printf("\033[2;3H%s%s%s", theme_head,
           tr("COMMANDS :: type to search", "COMANDOS :: digite para buscar"),
           KALC_RESET);
    printf("\033[3;3H%s-------------------------%s", theme_dim, KALC_RESET);
    printf("\033[4;3H%s> %s%s▌%s", theme_head, theme_text, pal_query, KALC_RESET);

    for (int i = 0; i < list_h; i++) {
        int idx = pal_top + i;
        if (idx >= pal_nmatch) break;
        const PalItem *it = &pal_items[pal_match[idx]];
        int row = PAL_LIST_TOP + i;
        int dw = 10;
        int avail = cols - 6 - dw;
        if (avail < 1) avail = 1;
        printf("\033[%d;3H", row);
        if (idx == pal_sel)
            printf("%s  %-*.*s%*.*s%s", theme_sel, avail, avail, it->text,
                   dw, dw, it->desc, KALC_RESET);
        else
            printf("%s  %-*.*s%s%s%*.*s%s", theme_text, avail, avail, it->text,
                   KALC_RESET, theme_dim, dw, dw, it->desc, KALC_RESET);
    }

    printf("\033[%d;3H%s%d %s · %s%s", rows, theme_dim, pal_nmatch,
           tr("results", "resultados"),
           tr("Enter run · Tab edit · Esc close",
              "Enter executa · Tab edita · Esc fecha"), KALC_RESET);
    printf("\033[?2026l");
    fflush(stdout);
}

/* Retorna 0 = cancelou, 1 = executar, 2 = editar no prompt. */
static int pal_open(void) {
    pal_collect();
    pal_query[0] = '\0';
    pal_sel = 0;
    pal_top = 0;
    pal_refresh();

    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) { pal_free(); return 0; }
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[r\033[2J\033[H\033[?25l\033[?1000h\033[?1006h");
    fflush(stdout);
    kalc_size();
    int last_rows = kalc_rows, last_cols = kalc_cols;
    pal_render();

    int action = 0;
    int running = 1;
    while (running) {
        KalcKey k;
        int got = kalc_read_key(&k, 200);

        kalc_size();
        if (kalc_rows != last_rows || kalc_cols != last_cols) {
            last_rows = kalc_rows;
            last_cols = kalc_cols;
            pal_render();
        }
        if (!got) continue;

        int changed = 0, qchanged = 0;
        switch (k.type) {
            case K_CHAR:
                if (k.ch == 3 || k.ch == 7) {
                    running = 0;
                } else if (k.ch == '\t') {
                    if (pal_nmatch > 0) {
                        snprintf(pal_result, sizeof(pal_result), "%s",
                                 pal_items[pal_match[pal_sel]].text);
                        action = 2;
                        running = 0;
                    }
                } else if (k.ch == 8 || k.ch == 127) {
                    int l = (int)strlen(pal_query);
                    if (l > 0) { pal_query[l - 1] = '\0'; qchanged = 1; }
                } else if (k.ch == 21) {      /* Ctrl+U */
                    if (pal_query[0]) { pal_query[0] = '\0'; qchanged = 1; }
                } else if (k.ch == 23) {      /* Ctrl+W */
                    int l = (int)strlen(pal_query);
                    while (l > 0 && pal_query[l - 1] == ' ') l--;
                    while (l > 0 && pal_query[l - 1] != ' ') l--;
                    if ((int)strlen(pal_query) != l) {
                        pal_query[l] = '\0';
                        qchanged = 1;
                    }
                } else if (k.ch == 14) {      /* Ctrl+N */
                    if (pal_nmatch > 0) {
                        pal_sel = (pal_sel + 1) % pal_nmatch;
                        changed = 1;
                    }
                } else if (k.ch == 16) {      /* Ctrl+P */
                    if (pal_nmatch > 0) {
                        pal_sel = (pal_sel + pal_nmatch - 1) % pal_nmatch;
                        changed = 1;
                    }
                } else if (k.ch >= 32 && k.ch < 127) {
                    int l = (int)strlen(pal_query);
                    if (l < PAL_QUERY_LEN - 1) {
                        pal_query[l] = (char)k.ch;
                        pal_query[l + 1] = '\0';
                        qchanged = 1;
                    }
                }
                break;
            case K_UP:
                if (pal_nmatch > 0) {
                    pal_sel = (pal_sel + pal_nmatch - 1) % pal_nmatch;
                    changed = 1;
                }
                break;
            case K_DOWN:
                if (pal_nmatch > 0) {
                    pal_sel = (pal_sel + 1) % pal_nmatch;
                    changed = 1;
                }
                break;
            case K_PGUP:
                pal_sel -= 8;
                if (pal_sel < 0) pal_sel = 0;
                changed = 1;
                break;
            case K_PGDN:
                pal_sel += 8;
                if (pal_sel >= pal_nmatch)
                    pal_sel = pal_nmatch > 0 ? pal_nmatch - 1 : 0;
                changed = 1;
                break;
            case K_HOME:
                pal_sel = 0;
                changed = 1;
                break;
            case K_END:
                pal_sel = pal_nmatch > 0 ? pal_nmatch - 1 : 0;
                changed = 1;
                break;
            case K_ENTER:
                if (pal_nmatch > 0) {
                    snprintf(pal_result, sizeof(pal_result), "%s",
                             pal_items[pal_match[pal_sel]].text);
                    action = 1;
                    running = 0;
                }
                break;
            case K_ESC:
                running = 0;
                break;
            case K_MOUSE:
                if (k.press && k.mb == 0) {
                    int idx = pal_top + (k.my - PAL_LIST_TOP);
                    if (k.my >= PAL_LIST_TOP && idx >= 0 && idx < pal_nmatch) {
                        snprintf(pal_result, sizeof(pal_result), "%s",
                                 pal_items[pal_match[idx]].text);
                        action = 1;
                        running = 0;
                    }
                } else if (k.press && (k.mb == 64 || k.mb == 65)) {
                    pal_sel += (k.mb == 64 ? -3 : 3);
                    if (pal_sel < 0) pal_sel = 0;
                    if (pal_sel >= pal_nmatch)
                        pal_sel = pal_nmatch > 0 ? pal_nmatch - 1 : 0;
                    changed = 1;
                }
                break;
        }
        if (!running) break;
        if (qchanged) {
            pal_sel = 0;
            pal_top = 0;
            pal_refresh();
            changed = 1;
        }
        if (changed) pal_render();
    }

    printf("\033[?1006l\033[?1000l\033[?25h\033[0m\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    pal_free();
    return action;
}

static int palette_key(int count, int key) {
    (void)count; (void)key;
    suggestion_erase();
    int action = pal_open();
    if (action == 1 || action == 2) {
        rl_replace_line(pal_result, 0);
        rl_point = rl_end;
        size_t len = strlen(pal_result);
        if (len >= 2 && strcmp(pal_result + len - 2, "\"\"") == 0)
            rl_point = rl_end - 1;
        if (action == 1) rl_done = 1;
    } else {
        rl_on_new_line();
    }
    redisplay_now();
    if (action == 1) rl_crlf();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sugestoes inline (estilo fish)                                      */
/* ------------------------------------------------------------------ */

/* Esquece a sugestao sem tocar na tela (a tela sera limpa/redesenhada). */
static void sugg_forget(void) {
    sugg_active = 0;
    sugg_drawn = 0;
}

static void sugg_set(const char *cand, size_t len) {
    if (!cand) return;
    size_t n = strlen(cand + len);
    if (n == 0 || n >= sizeof(sugg_text)) return;
    memcpy(sugg_text, cand + len, n + 1);
    sugg_active = 1;
}

/* Carrega os executaveis do PATH uma unica vez, para sugestoes de comando. */
static void sugg_load_path(void) {
    if (sugg_cmds_loaded) return;
    sugg_cmds_loaded = 1;
    const char *path = getenv("PATH");
    if (!path) return;
    char *copy = strdup(path);
    if (!copy) return;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir && sugg_ncmds < SUGG_CMD_MAX;
         dir = strtok_r(NULL, ":", &save)) {
        DIR *d = opendir(*dir ? dir : ".");
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && sugg_ncmds < SUGG_CMD_MAX) {
            if (e->d_name[0] == '.') continue;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", *dir ? dir : ".", e->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (!S_ISREG(st.st_mode) || (st.st_mode & 0111) == 0) continue;
            sugg_cmds[sugg_ncmds] = strdup(e->d_name);
            if (sugg_cmds[sugg_ncmds]) sugg_ncmds++;
        }
        closedir(d);
    }
    free(copy);
}

static void sugg_free_path(void) {
    for (int i = 0; i < sugg_ncmds; i++) free(sugg_cmds[i]);
    sugg_ncmds = 0;
    sugg_cmds_loaded = 0;
}

/* Inicio do ultimo token, respeitando aspas simples/duplas e escapes. */
static size_t sugg_last_token(const char *line, size_t len) {
    size_t start = 0;
    int in_s = 0, in_d = 0;
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (c == '\\' && i + 1 < len) { i++; continue; }
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        else if ((c == ' ' || c == '\t') && !in_s && !in_d) start = i + 1;
    }
    return start;
}

/* Comandos que costumam receber caminhos como argumento. */
static int sugg_is_path_cmd(const char *line, size_t len, int *is_cd) {
    static const char *const cmds[] = {
        "cd", "ls", "cat", "less", "more", "head", "tail", "vim", "nvim",
        "nano", "code", "cp", "mv", "rm", "mkdir", "rmdir", "touch", "ln",
        "stat", "file", "du", "df", "find", "python3", "node", "gcc",
    };
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    size_t j = i;
    while (j < len && line[j] != ' ' && line[j] != '\t') j++;
    size_t n = j - i;
    if (is_cd) *is_cd = (n == 2 && strncmp(line + i, "cd", 2) == 0);
    for (int k = 0; k < (int)(sizeof(cmds) / sizeof(cmds[0])); k++) {
        if (strlen(cmds[k]) == n && strncmp(cmds[k], line + i, n) == 0)
            return 1;
    }
    return 0;
}

/* Completa o ultimo token com um caminho do sistema de arquivos. */
static int sugg_path(const char *line, size_t len, int has_space) {
    size_t tok_start = has_space ? sugg_last_token(line, len) : 0;
    const char *tok = line + tok_start;
    size_t tok_len = len - tok_start;

    if (tok_len > 0 && tok[0] == '-') return 0;   /* opcao, nao caminho */

    char quote = 0;
    if (tok_len > 0 && (tok[0] == '\'' || tok[0] == '"')) {
        quote = tok[0];
        if (memchr(tok + 1, quote, tok_len - 1)) return 0; /* ja fechou */
        tok++;
        tok_len--;
    }

    const char *slash = NULL;
    for (size_t i = 0; i < tok_len; i++)
        if (tok[i] == '/') slash = tok + i;

    char dir[PATH_MAX];
    const char *base = tok;
    size_t base_len = tok_len;

    if (slash) {
        size_t dlen = (size_t)(slash - tok) + 1;   /* inclui a barra */
        base = slash + 1;
        base_len = tok_len - dlen;
        if (tok[0] == '~') {
            const char *home = getenv("HOME");
            if (!home) return 0;
            snprintf(dir, sizeof(dir), "%s%.*s", home, (int)(dlen - 1), tok + 1);
        } else {
            snprintf(dir, sizeof(dir), "%.*s", (int)dlen, tok);
        }
    } else {
        if (tok_len == 1 && tok[0] == '~') {
            /* "~" -> "~/" */
            snprintf(sugg_text, sizeof(sugg_text), "/%s", quote ? (char[]){quote, 0} : "");
            sugg_active = 1;
            return 1;
        }
        snprintf(dir, sizeof(dir), ".");
    }

    DIR *d = opendir(dir);
    if (!d) return 0;

    int is_cd = 0;
    sugg_is_path_cmd(line, len, &is_cd);

    char best[PATH_MAX];
    best[0] = '\0';
    int best_is_dir = 0, best_exact_dir = 0;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        size_t nlen = strlen(name);

        if (name[0] == '.' && base_len == 0) continue;   /* ocultos */
        if ((strcmp(name, ".") == 0 || strcmp(name, "..") == 0) &&
            (base_len == 0 || base[0] != '.')) continue;
        if (strncmp(name, base, base_len) != 0) continue;

        char full[PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", dir, name) >= (int)sizeof(full))
            continue;
        struct stat st;
        int is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        if (is_cd && !is_dir) continue;
        if (quote && strchr(name, quote)) continue;

        if (nlen == base_len) {
            /* Caminho ja completo. Para "cd" (ou algo que nao seja "."),
               sugere a barra final; para "cat ." prefere .bashrc etc. */
            if (is_dir && (is_cd || !(base_len == 1 && base[0] == '.'))) {
                snprintf(best, sizeof(best), "%s", name);
                best_is_dir = 1;
                best_exact_dir = 1;
                break;
            }
            continue;
        }
        if (best[0] == '\0' || strcasecmp(name, best) < 0) {
            snprintf(best, sizeof(best), "%s", name);
            best_is_dir = is_dir;
        }
    }
    closedir(d);
    if (best[0] == '\0') return 0;

    char suffix[PATH_MAX];
    if (best_exact_dir)
        snprintf(suffix, sizeof(suffix), "/");
    else
        snprintf(suffix, sizeof(suffix), "%s%s", best + base_len,
                 best_is_dir ? "/" : "");

    size_t out = 0;
    for (size_t i = 0; suffix[i] && out + 4 < sizeof(sugg_text); i++) {
        if (!quote && suffix[i] == ' ') {
            sugg_text[out++] = '\\';
            sugg_text[out++] = ' ';
        } else {
            sugg_text[out++] = suffix[i];
        }
    }
    if (quote) sugg_text[out++] = quote;
    sugg_text[out] = '\0';
    if (out == 0) return 0;
    sugg_active = 1;
    return 1;
}

static void suggestion_compute(void) {
    sugg_active = 0;
    if (!rl_line_buffer || rl_point != rl_end || rl_end <= 0) return;
    if (rl_done) return;
    if (!rl_display_prompt || !strstr(rl_display_prompt, "void-shell>")) return;

    const char *line = rl_line_buffer;
    size_t len = (size_t)rl_end;
    int has_space = strpbrk(line, " \t") != NULL;

    /* 1. historico: entrada mais recente que seja prefixo da linha. */
    HIST_ENTRY **h = history_list();
    if (h) {
        for (int i = history_length - 1; i >= 0; i--) {
            const char *cand = h[i] ? h[i]->line : NULL;
            if (!cand) continue;
            size_t clen = strlen(cand);
            if (clen <= len) continue;
            /* Sem espaco ainda: prefere completar so o nome do comando. */
            if (!has_space && strpbrk(cand, " \t")) continue;
            if (strncmp(cand, line, len) != 0) continue;
            sugg_set(cand, len);
            return;
        }
    }

    if (!has_space) {
        /* 2. caminho explicito como nome do comando (./script, ~/bin/x). */
        if (strchr(line, '/') || line[0] == '~' || line[0] == '.') {
            if (sugg_path(line, len, 0)) return;
        }
        /* 3. comandos internos com barra. */
        if (line[0] == '/') {
            static const char *const slashes[] = {
                "/kalc", "/lang", "/theme", "/tutorial", "/help",
            };
            for (int i = 0; i < (int)(sizeof(slashes) / sizeof(slashes[0])); i++) {
                if (strncmp(slashes[i], line, len) != 0) continue;
                if (strlen(slashes[i]) == len) return;   /* ja esta completo */
                sugg_set(slashes[i], len);
                return;
            }
        }
        /* 3. builtins. */
        static const char *const builtins[] = {
            "cd", "pwd", "exit", "echo", "clear", "jobs", "fg", "bg",
            "export",
        };
        for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); i++) {
            if (strncmp(builtins[i], line, len) != 0) continue;
            if (strlen(builtins[i]) == len) return;   /* ja esta completo */
            sugg_set(builtins[i], len);
            return;
        }
        /* 4. comandos comuns (ordem de preferencia). */
        static const char *const common[] = {
            "git", "docker", "python3", "node", "npm", "gcc", "make",
            "cargo", "go", "java", "curl", "wget", "ssh", "tmux", "vim",
            "nvim", "code", "ls", "less", "grep", "find", "cat", "man",
            "ps", "top", "htop", "df", "du", "date", "cal", "bc", "tar",
            "zip", "unzip", "sed", "awk", "sort", "uniq", "head", "tail",
            "wc", "chmod", "chown", "mkdir", "rm", "cp", "mv", "touch",
            "ln", "file", "stat", "which", "whereis", "uname", "free",
            "kill", "pkill",
        };
        for (int i = 0; i < (int)(sizeof(common) / sizeof(common[0])); i++) {
            if (strncmp(common[i], line, len) != 0) continue;
            if (strlen(common[i]) == len) return;   /* ja esta completo */
            sugg_set(common[i], len);
            return;
        }
        /* 5. executaveis do PATH (so para o nome do comando). */
        if (!strpbrk(line, "/.~")) {
            sugg_load_path();
            int best = -1, exact = 0;
            size_t best_len = (size_t)-1;
            for (int i = 0; i < sugg_ncmds; i++) {
                const char *cand = sugg_cmds[i];
                size_t clen = strlen(cand);
                if (clen < len || strncmp(cand, line, len) != 0) continue;
                if (clen == len) { exact = 1; break; }
                if (clen < best_len) { best = i; best_len = clen; }
            }
            if (exact) return;   /* ja existe um comando com esse nome */
            if (best >= 0) { sugg_set(sugg_cmds[best], len); return; }
        }
    } else {
        /* 6. caminho no ultimo argumento (cd Doc -> Documents/). */
        size_t tok = sugg_last_token(line, len);
        int explicit_path = 0;
        if (tok < len) {
            if (line[tok] == '/' || line[tok] == '~' || line[tok] == '.' ||
                line[tok] == '\'' || line[tok] == '"')
                explicit_path = 1;
            else if (memchr(line + tok, '/', len - tok))
                explicit_path = 1;
        }
        int is_cd = 0;
        int path_cmd = sugg_is_path_cmd(line, len, &is_cd);
        if ((path_cmd || explicit_path) && sugg_path(line, len, 1)) return;

        /* 7. atalhos prontos com argumentos. */
        static const char *const snippets[] = {
            "git status", "git log --oneline", "git diff", "git add .",
            "git commit -m \"\"", "git push", "git pull", "git switch -c ",
            "ls -la", "find . -name ", "grep -rn ",
        };
        for (int i = 0; i < (int)(sizeof(snippets) / sizeof(snippets[0])); i++) {
            if (strncmp(snippets[i], line, len) == 0 &&
                strlen(snippets[i]) > len) {
                sugg_set(snippets[i], len);
                return;
            }
        }
    }
}

static void suggestion_erase(void) {
    if (sugg_drawn) {
        printf("\033[K");
        fflush(stdout);
        sugg_drawn = 0;
    }
    sugg_active = 0;
}

static void suggestion_draw(void) {
    if (!sugg_active || !sugg_text[0]) return;
    int rows = 0, cols = 0;
    rl_get_screen_size(&rows, &cols);
    (void)rows;
    if (cols <= 0) return;
    int col = (SUGG_PROMPT_W + rl_end) % cols;
    int avail = cols - col - 1;
    if (avail <= 0) return;
    size_t n = strlen(sugg_text);
    if ((int)n > avail) n = (size_t)avail;
    if (n == 0) return;
    printf("\033[s%s%.*s%s\033[u", theme_dim, (int)n, sugg_text, theme_text);
    fflush(stdout);
    sugg_drawn = 1;
}

static void suggestion_redisplay(void) {
    suggestion_erase();
    rl_redisplay();
    if (rl_done) return;
    suggestion_compute();
    suggestion_draw();
}

static void redisplay_now(void) {
    if (rl_redisplay_function) (*rl_redisplay_function)();
    else rl_redisplay();
}

/* Seta direita: aceita a sugestao; se nao houver, anda um caractere. */
static int suggestion_accept(int count, int key) {
    if (rl_point == rl_end && rl_end > 0) {
        suggestion_compute();
        if (sugg_active) {
            suggestion_erase();
            rl_insert_text(sugg_text);
            sugg_active = 0;
            redisplay_now();
            return 0;
        }
    }
    return rl_forward_char(count, key);
}

/* Enter/Ctrl+J: apaga o fantasma antes de aceitar a linha. */
static int suggestion_newline(int count, int key) {
    suggestion_erase();
    return rl_newline(count, key);
}

/* Ctrl+C no prompt: cancela a linha e desenha um prompt limpo, sem matar
   o shell. Com rl_catch_signals = 0, o readline chama este hook quando o
   read() e interrompido pelo sinal; aqui ja estamos em contexto seguro. */
static void sigint_handler(int sig) {
    (void)sig;
    sigint_seen = 1;
}

static int sigint_hook(void) {
    if (!sigint_seen) return 0;
    sigint_seen = 0;
    sugg_forget();
    /* A animacao salva/restaura o cursor com ESC[s/ESC[u. Se um frame foi
       interrompido no meio, o cursor pode ter ficado na area das estrelas;
       ESC[u o traz de volta ao prompt antes de limpar a linha. */
    if (anim_enabled) printf("\033[u");
    printf("\r\033[K");
    rl_replace_line("", 0);
    rl_point = rl_end = 0;
    rl_forced_update_display();
    return 0;
}

static void save_history(void) {
    write_history(histfile);
}

int main(void) {
    /* Cada frame da animação tem milhares de sequências de cor. Com o buffer
     * padrão (4 KiB) o VTE recebe o frame em vários writes e repinta o
     * widget no meio dele — o resultado é um "pisca-pisca" do banner, do
     * prompt e da própria galáxia. Um buffer grande deixa cada frame atômico
     * (um único write no fflush do fim do tick). */
    static char out_buf[192 * 1024];
    if (isatty(STDOUT_FILENO))
        setvbuf(stdout, out_buf, _IOFBF, sizeof(out_buf));

    term_bg_load();
    theme_load();
    lang_load();
    const char *home = getenv("HOME");
    snprintf(histfile, sizeof(histfile), "%s/.void_shell_history", home ? home : ".");
    read_history(histfile);
    atexit(save_history);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sigaction(SIGINT, &sa_int, NULL);
    if (isatty(STDIN_FILENO)) {
        shell_terminal = STDIN_FILENO;
        setpgid(0, 0);
        shell_pgid = getpid();
        tcsetpgrp(shell_terminal, shell_pgid);
    }

    print_banner();
    anim_setup();
    if (isatty(STDOUT_FILENO)) print_tutorial();
    rl_catch_signals = 0;   /* Ctrl+C e tratado por sigint_hook, sem sair */
    rl_initialize();
    rl_set_keyboard_input_timeout(80000);
    rl_event_hook = anim_tick;
    rl_add_defun("void-clear-screen", anim_clear_key, '\x0c');
    rl_add_defun("void-command-palette", palette_key, 16);
    rl_redisplay_function = suggestion_redisplay;
    rl_signal_event_hook = sigint_hook;
    rl_add_defun("void-accept-suggestion", suggestion_accept, -1);
    rl_bind_keyseq("\\e[C", suggestion_accept);
    rl_bind_keyseq("\\eOC", suggestion_accept);
    rl_bind_key('\r', suggestion_newline);
    rl_bind_key('\n', suggestion_newline);
    rl_bind_keyseq("\\eOM", suggestion_newline);   /* Enter do teclado numerico */
    atexit(sugg_free_path);
    atexit(anim_cleanup);

    for (;;) {
        if (anim_poll_resize()) {
            rl_set_screen_size(anim_rows, anim_cols);
            int ok = (anim_cols >= ANIM_MIN_COLS && anim_rows >= ANIM_MIN_ROWS);
            if (ok) {
                anim_enabled = 1;
                anim_render_all();
            } else {
                anim_enabled = 0;
                hud_enabled = 0;
                sugg_forget();
                printf("\033[r\033[%d;1H\033[K", anim_rows > 0 ? anim_rows : 24);
                fflush(stdout);
            }
        }
        char prompt[128];
        snprintf(prompt, sizeof(prompt),
                 "\001%s\002void-shell> \001%s\002", theme_prompt, theme_text);
        sigint_seen = 0;
        if (shell_terminal >= 0) tcsetpgrp(shell_terminal, shell_pgid);
        char *input = readline(prompt);
        printf("\033[0m");
        fflush(stdout);
        if (!anim_logo_pinned) anim_logo_live = 0;
        if (!input) { /* EOF (Ctrl+D) */
            printf("\n");
            break;
        }
        if (*input) {
            add_history(input);
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            run_line(input);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            last_duration_ms = (t1.tv_sec - t0.tv_sec) * 1000L +
                               (t1.tv_nsec - t0.tv_nsec) / 1000000L;
            hud_force = 1;
        }
        fflush(stdout);
        free(input);
    }

    return 0;
}

