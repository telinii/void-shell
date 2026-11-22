/* void-term: um terminal GTK4 + VTE com a cara do void-shell.
 *
 * Recursos:
 *  - abas (GtkNotebook)
 *  - splits horizontais/verticais (GtkPaned)
 *  - busca no scrollback (VTE)
 *  - barra de titulo e botoes proprios
 *  - paleta roxa, fonte mono e CSS customizado
 *  - roda o void-shell por padrao, se ele existir
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *search_revealer;
    GtkWidget *search_entry;
    GtkWidget *title_label;
    GtkWidget *current_pane;
    GtkWidget *current_term;
    GtkWidget *menu_button;
    GtkCssProvider *chrome_css;
    PangoFontDescription *font;
    int font_size;
    int font_default_size;
    guint tab_count;
    gboolean search_open;
} VoidTerm;

static VoidTerm vt;

static void search_update(void);
static void set_current(GtkWidget *pane, GtkWidget *term);
static void close_pane(GtkWidget *pane);
static void close_tab(GtkWidget *page);
static GtkWidget *pane_new(void);
static GtkWidget *find_first_terminal(GtkWidget *widget);

/* ------------------------------------------------------------------ */
/* Paleta derivada do fundo do terminal (inspirada no kitty)           */
/* ------------------------------------------------------------------ */

/* Todo o chrome (header, abas, bordas, scrollbar, menu, busca, cursor e
 * selecao) e gerado a partir da cor de fundo do VTE. Quando o void-shell
 * troca o fundo via OSC 11, o chrome inteiro acompanha. */

typedef struct {
    double r, g, b;
} Rgb;

typedef struct {
    Rgb bg;
    Rgb bar;
    Rgb tab_hover;
    Rgb tab_active;
    Rgb line;
    Rgb line_hot;
    Rgb text;
    Rgb dim;
    Rgb accent;
    Rgb accent_soft;
    Rgb sel;
} Palette;

static Palette chrome_pal;
static gboolean chrome_valid;

static double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

static Rgb rgb_of(double r, double g, double b) {
    Rgb c = {clamp01(r), clamp01(g), clamp01(b)};
    return c;
}

static Rgb rgb_from_rgba(const GdkRGBA *c) {
    return rgb_of(c->red, c->green, c->blue);
}

static GdkRGBA rgba_of(Rgb c) {
    GdkRGBA out = {c.r, c.g, c.b, 1.0};
    return out;
}

static Rgb rgb_mix(Rgb a, Rgb b, double t) {
    return rgb_of(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                  a.b + (b.b - a.b) * t);
}

static void rgb_to_hsl(Rgb c, double *hh, double *ss, double *ll) {
    double max = MAX(c.r, MAX(c.g, c.b));
    double min = MIN(c.r, MIN(c.g, c.b));
    double l = (max + min) / 2.0;
    double h = 0.0, s = 0.0;
    double d = max - min;

    if (d > 1e-6) {
        s = l > 0.5 ? d / (2.0 - max - min) : d / (max + min);
        if (max == c.r)
            h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (max == c.g)
            h = (c.b - c.r) / d + 2.0;
        else
            h = (c.r - c.g) / d + 4.0;
        h *= 60.0;
    }
    *hh = h;
    *ss = s;
    *ll = l;
}

static double hue_channel(double p, double q, double t) {
    if (t < 0.0)
        t += 1.0;
    if (t > 1.0)
        t -= 1.0;
    if (t < 1.0 / 6.0)
        return p + (q - p) * 6.0 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3.0)
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

static Rgb rgb_hsl(double h, double s, double l) {
    h /= 360.0;
    while (h < 0.0)
        h += 1.0;
    while (h >= 1.0)
        h -= 1.0;
    if (s <= 0.0)
        return rgb_of(l, l, l);
    double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    double p = 2.0 * l - q;
    return rgb_of(hue_channel(p, q, h + 1.0 / 3.0), hue_channel(p, q, h),
                  hue_channel(p, q, h - 1.0 / 3.0));
}

static void rgb_hex(char out[8], Rgb c) {
    g_snprintf(out, 8, "#%02x%02x%02x",
               (guint)(clamp01(c.r) * 255.0 + 0.5),
               (guint)(clamp01(c.g) * 255.0 + 0.5),
               (guint)(clamp01(c.b) * 255.0 + 0.5));
}

static Palette palette_make(Rgb bg) {
    double h, s, l;
    rgb_to_hsl(bg, &h, &s, &l);

    double sat = s > 0.55 ? 0.55 : s;
    double accent_sat = sat < 0.04 ? 0.0 : sat * 1.45;
    if (accent_sat > 0.62)
        accent_sat = 0.62;
    if (accent_sat < 0.30 && sat >= 0.05)
        accent_sat = 0.30;

    /* Fundo claro (raro): texto escuro. */
    double text_l = l < 0.45 ? 0.90 : 0.14;
    double dim_l = l < 0.45 ? 0.58 : 0.42;

    Palette p;
    p.bg = bg;
    p.bar = rgb_hsl(h, sat * 0.85, l + 0.045);
    p.tab_hover = rgb_hsl(h, sat * 0.90, l + 0.080);
    p.tab_active = rgb_hsl(h, sat * 0.90, l + 0.115);
    p.line = rgb_hsl(h, sat * 0.80, l + 0.135);
    p.line_hot = rgb_hsl(h, sat * 0.90, l + 0.230);
    p.text = rgb_hsl(h, sat * 0.35, text_l);
    p.dim = rgb_hsl(h, sat * 0.35, dim_l);
    p.accent = rgb_hsl(h, accent_sat, l < 0.45 ? 0.68 : 0.35);
    p.accent_soft = rgb_mix(bg, p.accent, 0.55);
    p.sel = rgb_mix(bg, p.accent, 0.30);
    return p;
}

static void chrome_build_css(const Palette *p) {
    char bg[8], bar[8], tabh[8], taba[8], line[8], lineh[8];
    char text[8], dim[8], acc[8], accs[8], sel[8];

    rgb_hex(bg, p->bg);
    rgb_hex(bar, p->bar);
    rgb_hex(tabh, p->tab_hover);
    rgb_hex(taba, p->tab_active);
    rgb_hex(line, p->line);
    rgb_hex(lineh, p->line_hot);
    rgb_hex(text, p->text);
    rgb_hex(dim, p->dim);
    rgb_hex(acc, p->accent);
    rgb_hex(accs, p->accent_soft);
    rgb_hex(sel, p->sel);

    GString *css = g_string_new(NULL);

    g_string_append_printf(
        css, "window, .background { background-color: %s; }\n", bg);
    g_string_append_printf(
        css,
        "headerbar {\n"
        "  background-color: %s;\n"
        "  background-image: none;\n"
        "  border-bottom: 1px solid %s;\n"
        "  min-height: 44px;\n"
        "  box-shadow: none;\n"
        "  padding: 0 6px;\n"
        "}\n"
        "headerbar .brand { color: %s; font-size: 9px; font-weight: 700; }\n"
        "headerbar .title { color: %s; font-weight: 600; font-size: 13px; }\n"
        "headerbar button, headerbar menubutton > button {\n"
        "  background: transparent; border: none; box-shadow: none;\n"
        "  color: %s; border-radius: 0; padding: 6px; min-width: 22px;\n"
        "}\n"
        "headerbar button:hover, headerbar menubutton > button:hover {\n"
        "  background: %s; color: %s;\n"
        "}\n"
        "headerbar button:active, headerbar menubutton > button:active {\n"
        "  background: %s; color: %s;\n"
        "}\n"
        "headerbar button.close:hover { background: #c94f5f; color: #ffffff; }\n",
        bar, line, dim, text, dim, tabh, text, taba, text);

    g_string_append_printf(
        css,
        "notebook { background-color: %s; }\n"
        "notebook > header {\n"
        "  background-color: %s;\n"
        "  border-bottom: 1px solid %s;\n"
        "  padding: 0 6px;\n"
        "}\n"
        "notebook > header > tabs > tab {\n"
        "  background-color: transparent; color: %s;\n"
        "  padding: 8px 12px; margin: 0 1px;\n"
        "  border-radius: 0; border: none;\n"
        "  box-shadow: inset 0 2px transparent;\n"
        "}\n"
        "notebook > header > tabs > tab:hover {\n"
        "  background-color: %s; color: %s;\n"
        "}\n"
        "notebook > header > tabs > tab:checked {\n"
        "  background-color: %s; color: %s;\n"
        "  box-shadow: inset 0 2px %s;\n"
        "}\n"
        "notebook > header > tabs > tab .tab-dot {\n"
        "  color: %s; font-size: 9px;\n"
        "}\n"
        "notebook > header > tabs > tab:checked .tab-dot { color: %s; }\n"
        "notebook > header > tabs > tab .tab-close {\n"
        "  background: transparent; border: none; box-shadow: none;\n"
        "  padding: 0; margin: 0; min-width: 16px; min-height: 16px;\n"
        "  color: %s;\n"
        "}\n"
        "notebook > header > tabs > tab .tab-close:hover {\n"
        "  background: %s; color: %s; border-radius: 3px;\n"
        "}\n",
        bg, bar, line, dim, tabh, text, taba, text, acc, dim, acc, dim, lineh,
        text);

    g_string_append_printf(
        css,
        ".tab-page { background-color: %s; }\n"
        ".pane {\n"
        "  padding: 8px 10px;\n"
        "  border: 1px solid transparent;\n"
        "  border-radius: 0;\n"
        "  background-color: %s;\n"
        "}\n"
        ".pane.focused { border-color: %s; }\n"
        ".split > separator {\n"
        "  background-color: %s; min-width: 2px; min-height: 2px;\n"
        "}\n"
        ".split > separator:hover { background-color: %s; }\n"
        ".search-bar {\n"
        "  background-color: %s;\n"
        "  border-bottom: 1px solid %s;\n"
        "  padding: 6px 10px;\n"
        "}\n"
        ".search-bar entry {\n"
        "  background-color: %s; color: %s;\n"
        "  border: 1px solid %s; border-radius: 0;\n"
        "  padding: 5px 9px;\n"
        "}\n"
        ".search-bar entry:focus { border-color: %s; outline: none; box-shadow: none; }\n"
        ".search-bar entry:focus-within { outline: none; box-shadow: none; }\n"
        ".search-bar button {\n"
        "  background: transparent; border: none; box-shadow: none;\n"
        "  color: %s; border-radius: 0; padding: 4px;\n"
        "}\n"
        ".search-bar button:hover { background: %s; color: %s; }\n"
        "scrollbar { background: transparent; }\n"
        "scrollbar slider {\n"
        "  background: %s; border-radius: 3px;\n"
        "  min-width: 6px; min-height: 30px;\n"
        "}\n"
        "scrollbar slider:hover { background: %s; }\n"
        "tooltip { background: %s; color: %s; border-radius: 0; }\n"
        "popover.menu > contents {\n"
        "  background-color: %s;\n"
        "  border: 1px solid %s;\n"
        "  border-radius: 0;\n"
        "  padding: 5px;\n"
        "}\n"
        "popover.menu modelbutton {\n"
        "  color: %s; padding: 6px 12px; border-radius: 0;\n"
        "}\n"
        "popover.menu modelbutton:hover,\n"
        "popover.menu modelbutton:focus,\n"
        "popover.menu modelbutton:selected {\n"
        "  background-color: %s; color: %s; outline: none;\n"
        "}\n"
        "popover.menu modelbutton accelerator { color: %s; }\n"
        "popover.menu separator {\n"
        "  background-color: %s; min-height: 1px; margin: 4px 8px;\n"
        "}\n"
        "headerbar .winbtn { min-width: 28px; padding: 2px 6px; }\n"
        "headerbar .winbtn label { font-size: 15px; font-weight: 600; }\n",
        bg, bg, accs, line, accs, bar, line, bg, text, line, acc, dim, tabh,
        text, line, accs, taba, text, bar, line, text, tabh, text, dim, line);

    gtk_css_provider_load_from_string(vt.chrome_css, css->str);
    g_string_free(css, TRUE);
}

static void vte_chrome_apply(GtkWidget *widget, const Palette *p) {
    if (VTE_IS_TERMINAL(widget)) {
        GdkRGBA cursor = rgba_of(p->accent);
        GdkRGBA cursor_fg = rgba_of(p->bg);
        GdkRGBA highlight = rgba_of(p->sel);
        GdkRGBA highlight_fg = rgba_of(p->text);
        vte_terminal_set_color_cursor(VTE_TERMINAL(widget), &cursor);
        vte_terminal_set_color_cursor_foreground(VTE_TERMINAL(widget),
                                                 &cursor_fg);
        vte_terminal_set_color_highlight(VTE_TERMINAL(widget), &highlight);
        vte_terminal_set_color_highlight_foreground(VTE_TERMINAL(widget),
                                                    &highlight_fg);
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        vte_chrome_apply(child, p);
}

static void chrome_apply(Rgb bg) {
    if (chrome_valid && chrome_pal.bg.r == bg.r && chrome_pal.bg.g == bg.g &&
        chrome_pal.bg.b == bg.b)
        return;

    chrome_pal = palette_make(bg);
    chrome_valid = TRUE;

    if (vt.chrome_css)
        chrome_build_css(&chrome_pal);
    if (vt.notebook)
        vte_chrome_apply(vt.notebook, &chrome_pal);
}

static void chrome_init(void) {
    vt.chrome_css = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(vt.chrome_css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    chrome_apply(rgb_of(0.043, 0.031, 0.071));
}

/* ------------------------------------------------------------------ */
/* Cores e fonte                                                       */
/* ------------------------------------------------------------------ */

static GdkRGBA color(double r, double g, double b) {
    GdkRGBA c = {r, g, b, 1.0};
    return c;
}

static void terminal_apply_colors(VteTerminal *term) {
    GdkRGBA fg = color(0.90, 0.86, 1.00);
    GdkRGBA bg = color(0.043, 0.031, 0.071);
    GdkRGBA palette[16] = {
        {0.10, 0.08, 0.15, 1.0}, {1.00, 0.42, 0.51, 1.0},
        {0.49, 0.91, 0.53, 1.0}, {1.00, 0.83, 0.47, 1.0},
        {0.48, 0.64, 1.00, 1.0}, {0.78, 0.57, 0.92, 1.0},
        {0.50, 0.86, 1.00, 1.0}, {0.90, 0.87, 1.00, 1.0},
        {0.27, 0.22, 0.36, 1.0}, {1.00, 0.55, 0.62, 1.0},
        {0.60, 1.00, 0.64, 1.0}, {1.00, 0.90, 0.60, 1.0},
        {0.62, 0.74, 1.00, 1.0}, {0.88, 0.70, 1.00, 1.0},
        {0.65, 0.93, 1.00, 1.0}, {1.00, 1.00, 1.00, 1.0},
    };
    GdkRGBA cursor = color(0.72, 0.54, 1.00);
    GdkRGBA cursor_fg = color(0.06, 0.04, 0.10);
    GdkRGBA highlight = color(0.23, 0.16, 0.38);
    GdkRGBA highlight_fg = color(1.00, 1.00, 1.00);

    vte_terminal_set_colors(term, &fg, &bg, palette, G_N_ELEMENTS(palette));
    vte_terminal_set_color_cursor(term, &cursor);
    vte_terminal_set_color_cursor_foreground(term, &cursor_fg);
    vte_terminal_set_color_highlight(term, &highlight);
    vte_terminal_set_color_highlight_foreground(term, &highlight_fg);
    vte_terminal_set_color_bold(term, &palette[15]);
}

/* ------------------------------------------------------------------ */
/* Fundo do painel: acompanha o fundo real do terminal                 */
/* ------------------------------------------------------------------ */

/* O void-shell muda o fundo via OSC 11 (/theme -> void-term bg color).
 * O VTE nao avisa por sinal, entao o padding do painel e atualizado
 * por polling barato: uma leitura de cor a cada 150 ms por painel. */

static GtkCssProvider *pane_bg_provider;
static GHashTable *pane_bg_colors; /* pane -> "#rrggbb" */
static guint pane_bg_seq;

static void pane_bg_rebuild(void) {
    GString *css = g_string_new(NULL);
    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init(&it, pane_bg_colors);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        if (!value)
            continue;
        g_string_append_printf(css, "#%s { background-color: %s; }\n",
                               gtk_widget_get_name(GTK_WIDGET(key)),
                               (const char *)value);
    }
    gtk_css_provider_load_from_string(pane_bg_provider, css->str);
    g_string_free(css, TRUE);
}

static gboolean pane_bg_tick(gpointer data) {
    (void)data;
    gboolean changed = FALSE;
    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init(&it, pane_bg_colors);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        GtkWidget *pane = GTK_WIDGET(key);
        GtkWidget *term = g_object_get_data(G_OBJECT(pane), "term");
        if (!term)
            continue;

        GdkRGBA bg;
        char hex[16];
        vte_terminal_get_color_background_for_draw(VTE_TERMINAL(term), &bg);
        g_snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                   (guint)(bg.red * 255.0 + 0.5),
                   (guint)(bg.green * 255.0 + 0.5),
                   (guint)(bg.blue * 255.0 + 0.5));
        if (!value || strcmp((const char *)value, hex) != 0) {
            g_hash_table_iter_replace(&it, g_strdup(hex));
            changed = TRUE;
        }
    }
    if (changed)
        pane_bg_rebuild();

    /* O chrome inteiro segue o terminal em foco (ou o primeiro da janela). */
    GtkWidget *ref = vt.current_term;
    if (!ref && vt.notebook)
        ref = find_first_terminal(vt.notebook);
    if (ref && VTE_IS_TERMINAL(ref)) {
        GdkRGBA bg;
        vte_terminal_get_color_background_for_draw(VTE_TERMINAL(ref), &bg);
        chrome_apply(rgb_from_rgba(&bg));
    }
    return G_SOURCE_CONTINUE;
}

static void pane_bg_forget(gpointer data, GObject *pane) {
    (void)data;
    if (g_hash_table_remove(pane_bg_colors, pane))
        pane_bg_rebuild();
}

static void pane_bg_track(GtkWidget *pane) {
    if (!pane_bg_provider) {
        pane_bg_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(pane_bg_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        pane_bg_colors = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, g_free);
        g_timeout_add(150, pane_bg_tick, NULL);
    }

    char *name = g_strdup_printf("vpane-%u", ++pane_bg_seq);
    gtk_widget_set_name(pane, name);
    g_free(name);
    g_hash_table_insert(pane_bg_colors, pane, NULL);
    g_object_weak_ref(G_OBJECT(pane), pane_bg_forget, NULL);
}

/* ------------------------------------------------------------------ */
/* Comando inicial                                                     */
/* ------------------------------------------------------------------ */

static char *exe_dir(void) {
    char *path = g_file_read_link("/proc/self/exe", NULL);
    if (!path)
        return NULL;
    char *dir = g_path_get_dirname(path);
    g_free(path);
    return dir;
}

static char **shell_argv(void) {
    const char *custom = g_getenv("VOID_TERM_SHELL");
    if (custom && *custom) {
        char **argv = NULL;
        GError *error = NULL;
        if (g_shell_parse_argv(custom, NULL, &argv, &error))
            return argv;
        g_clear_error(&error);
    }

    char **argv = g_new0(char *, 2);

    /* Build "embutida": void-shell ao lado do executavel do terminal. */
    char *dir = exe_dir();
    if (dir) {
        char *bundled = g_build_filename(dir, "void-shell", NULL);
        if (g_file_test(bundled, G_FILE_TEST_IS_EXECUTABLE)) {
            argv[0] = bundled;
            g_free(dir);
            return argv;
        }
        g_free(bundled);
        g_free(dir);
    }

    char *candidate = g_build_filename(g_get_home_dir(), "void-shell",
                                       "void-shell", NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE)) {
        argv[0] = candidate;
    } else {
        g_free(candidate);
        const char *sh = g_getenv("SHELL");
        argv[0] = g_strdup((sh && *sh) ? sh : "/bin/sh");
    }
    return argv;
}

static void on_spawn(VteTerminal *term, GPid pid, GError *error,
                     gpointer user_data) {
    (void)term;
    (void)pid;
    (void)user_data;
    if (error) {
        g_printerr("void-term: nao consegui iniciar o shell: %s\n",
                   error->message);
    }
}

static GtkWidget *terminal_new(void) {
    GtkWidget *term = vte_terminal_new();
    VteTerminal *vte = VTE_TERMINAL(term);

    gtk_widget_set_hexpand(term, TRUE);
    gtk_widget_set_vexpand(term, TRUE);

    vte_terminal_set_font(vte, vt.font);
    vte_terminal_set_scrollback_lines(vte, 10000);
    vte_terminal_set_scroll_on_output(vte, FALSE);
    vte_terminal_set_scroll_on_keystroke(vte, TRUE);
    vte_terminal_set_mouse_autohide(vte, TRUE);
    vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
    vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_allow_hyperlink(vte, TRUE);
    terminal_apply_colors(vte);
    if (chrome_valid)
        vte_chrome_apply(term, &chrome_pal);

    char **argv = shell_argv();
    vte_terminal_spawn_async(vte, VTE_PTY_DEFAULT, NULL, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                             on_spawn, NULL);
    g_strfreev(argv);

    return term;
}

/* ------------------------------------------------------------------ */
/* Paineis, abas e foco                                                */
/* ------------------------------------------------------------------ */

static void update_header_title(void) {
    const char *title = "void-shell";
    char *owned = NULL;

    if (vt.current_term) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        owned = g_strdup(vte_terminal_get_window_title(
                             VTE_TERMINAL(vt.current_term)));
        G_GNUC_END_IGNORE_DEPRECATIONS
        if (owned && *owned)
            title = owned;
    }
    if (vt.title_label)
        gtk_label_set_text(GTK_LABEL(vt.title_label), title);
    g_free(owned);
}

static void set_current(GtkWidget *pane, GtkWidget *term) {
    if (vt.current_pane && vt.current_pane != pane)
        gtk_widget_remove_css_class(vt.current_pane, "focused");

    vt.current_pane = pane;
    vt.current_term = term;

    if (pane)
        gtk_widget_add_css_class(pane, "focused");

    update_header_title();
    if (vt.search_open)
        search_update();
}

static void on_term_focus_enter(GtkEventControllerFocus *controller,
                                gpointer user_data) {
    (void)controller;
    GtkWidget *pane = user_data;
    GtkWidget *term = g_object_get_data(G_OBJECT(pane), "term");
    if (term)
        set_current(pane, term);
}

static void on_window_title_changed(VteTerminal *term, gpointer user_data) {
    (void)user_data;
    GtkWidget *label = g_object_get_data(G_OBJECT(term), "tab-label");
    char *owned = NULL;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    owned = g_strdup(vte_terminal_get_window_title(term));
    G_GNUC_END_IGNORE_DEPRECATIONS
    const char *title = (owned && *owned) ? owned : "void-shell";

    if (label)
        gtk_label_set_text(GTK_LABEL(label), title);
    if (GTK_WIDGET(term) == vt.current_term && vt.title_label)
        gtk_label_set_text(GTK_LABEL(vt.title_label), title);
    g_free(owned);
}

static gboolean close_pane_idle(gpointer data) {
    GtkWidget *pane = data;
    close_pane(pane);
    g_object_unref(pane);
    return G_SOURCE_REMOVE;
}

static void on_child_exited(VteTerminal *term, gint status, gpointer user_data) {
    (void)term;
    (void)status;
    GtkWidget *pane = user_data;

    /* O VTE emite child-exited de novo durante o dispose do terminal.
     * Nesse ponto o g_signal_connect_object já invalidou o weak ref e
     * entrega user_data NULL; também pode não haver mais pai. */
    if (!pane || !GTK_IS_WIDGET(pane) || !gtk_widget_get_parent(pane))
        return;

    /* Evita agendar dois fechamentos para o mesmo painel. */
    if (g_object_get_data(G_OBJECT(pane), "closing"))
        return;
    g_object_set_data(G_OBJECT(pane), "closing", GINT_TO_POINTER(1));

    g_object_ref(pane);
    g_idle_add(close_pane_idle, pane);
}

static GtkWidget *pane_new(void) {
    GtkWidget *pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(pane, "pane");
    gtk_widget_set_hexpand(pane, TRUE);
    gtk_widget_set_vexpand(pane, TRUE);
    pane_bg_track(pane);

    GtkWidget *term = terminal_new();
    gtk_box_append(GTK_BOX(pane), term);
    g_object_set_data(G_OBJECT(pane), "term", term);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_term_focus_enter), pane);
    gtk_widget_add_controller(term, focus);

    g_signal_connect_object(term, "child-exited", G_CALLBACK(on_child_exited),
                            pane, 0);
    g_signal_connect(term, "window-title-changed",
                     G_CALLBACK(on_window_title_changed), NULL);

    return pane;
}

static GtkWidget *find_first_terminal(GtkWidget *widget) {
    if (VTE_IS_TERMINAL(widget))
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_first_terminal(child);
        if (found)
            return found;
    }
    return NULL;
}

static void on_tab_close_clicked(GtkButton *button, gpointer user_data) {
    (void)user_data;
    GtkWidget *page = g_object_get_data(G_OBJECT(button), "page");
    if (page)
        close_tab(page);
}

static GtkWidget *make_tab_label(GtkWidget *page, GtkWidget *term,
                                 const char *title) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "tab-box");

    GtkWidget *dot = gtk_label_new("\u25cf");
    gtk_widget_add_css_class(dot, "tab-dot");

    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    gtk_widget_add_css_class(close, "tab-close");
    gtk_widget_set_tooltip_text(close, "Fechar aba");
    g_object_set_data(G_OBJECT(close), "page", page);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close_clicked), NULL);

    gtk_box_append(GTK_BOX(box), dot);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), close);

    g_object_set_data(G_OBJECT(term), "tab-label", label);
    return box;
}

static void focus_page_terminal(void) {
    int idx = gtk_notebook_get_current_page(GTK_NOTEBOOK(vt.notebook));
    if (idx < 0)
        return;
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(vt.notebook), idx);
    GtkWidget *term = find_first_terminal(page);
    if (!term)
        return;
    GtkWidget *pane = gtk_widget_get_parent(term);
    set_current(pane, term);
    if (!vt.search_open)
        gtk_widget_grab_focus(term);
}

static GtkWidget *add_tab(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "tab-page");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget *pane = pane_new();
    gtk_box_append(GTK_BOX(page), pane);

    char title[48];
    vt.tab_count++;
    if (vt.tab_count == 1)
        g_snprintf(title, sizeof title, "void-shell");
    else
        g_snprintf(title, sizeof title, "void-shell %u", vt.tab_count);

    GtkWidget *term = g_object_get_data(G_OBJECT(pane), "term");
    GtkWidget *label = make_tab_label(page, term, title);

    int idx = gtk_notebook_append_page(GTK_NOTEBOOK(vt.notebook), page, label);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(vt.notebook), idx);

    set_current(pane, term);
    gtk_widget_grab_focus(term);
    return pane;
}

static void on_switch_page(GtkNotebook *notebook, GtkWidget *page,
                           guint page_num, gpointer user_data) {
    (void)notebook;
    (void)page_num;
    (void)user_data;
    GtkWidget *term = find_first_terminal(page);
    if (!term)
        return;
    GtkWidget *pane = gtk_widget_get_parent(term);
    set_current(pane, term);
    if (!vt.search_open)
        gtk_widget_grab_focus(term);
}

static void close_tab(GtkWidget *page) {
    GtkNotebook *nb = GTK_NOTEBOOK(vt.notebook);
    int idx = gtk_notebook_page_num(nb, page);
    if (idx < 0)
        return;

    if (gtk_notebook_get_n_pages(nb) <= 1) {
        gtk_window_close(GTK_WINDOW(vt.window));
        return;
    }

    if (vt.current_pane && gtk_widget_is_ancestor(vt.current_pane, page)) {
        vt.current_pane = NULL;
        vt.current_term = NULL;
    }
    gtk_notebook_remove_page(nb, idx);
    focus_page_terminal();
}

static void on_paned_map_once(GtkWidget *paned, gpointer user_data) {
    (void)user_data;
    GtkOrientation orientation =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(paned));
    int size = orientation == GTK_ORIENTATION_HORIZONTAL
                   ? gtk_widget_get_width(paned)
                   : gtk_widget_get_height(paned);
    if (size > 0)
        gtk_paned_set_position(GTK_PANED(paned), size / 2);
    g_signal_handlers_disconnect_by_func(paned, on_paned_map_once, NULL);
}

static void split_current(GtkOrientation orientation) {
    if (!vt.current_pane)
        focus_page_terminal();
    if (!vt.current_pane)
        return;

    GtkWidget *pane = vt.current_pane;
    GtkWidget *parent = gtk_widget_get_parent(pane);
    if (!parent)
        return;

    GtkWidget *paned = gtk_paned_new(orientation);
    gtk_widget_add_css_class(paned, "split");
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_wide_handle(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    g_object_ref(pane);
    if (GTK_IS_PANED(parent)) {
        gboolean is_start =
            gtk_paned_get_start_child(GTK_PANED(parent)) == pane;
        if (is_start) {
            gtk_paned_set_start_child(GTK_PANED(parent), NULL);
            gtk_paned_set_start_child(GTK_PANED(parent), paned);
        } else {
            gtk_paned_set_end_child(GTK_PANED(parent), NULL);
            gtk_paned_set_end_child(GTK_PANED(parent), paned);
        }
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), pane);
        gtk_box_append(GTK_BOX(parent), paned);
    } else {
        g_object_unref(pane);
        gtk_widget_unparent(paned);
        return;
    }

    GtkWidget *new_pane = pane_new();
    gtk_paned_set_start_child(GTK_PANED(paned), pane);
    gtk_paned_set_end_child(GTK_PANED(paned), new_pane);
    g_object_unref(pane);

    g_signal_connect(paned, "map", G_CALLBACK(on_paned_map_once), NULL);

    GtkWidget *new_term = g_object_get_data(G_OBJECT(new_pane), "term");
    set_current(new_pane, new_term);
    gtk_widget_grab_focus(new_term);
}

static void close_pane(GtkWidget *pane) {
    GtkWidget *parent = gtk_widget_get_parent(pane);
    if (!parent)
        return;

    gboolean was_current = (vt.current_pane == pane);
    if (was_current) {
        vt.current_pane = NULL;
        vt.current_term = NULL;
    }

    if (!GTK_IS_PANED(parent)) {
        close_tab(parent);
        return;
    }

    GtkPaned *paned = GTK_PANED(parent);
    gboolean pane_is_start = gtk_paned_get_start_child(paned) == pane;
    GtkWidget *other = pane_is_start ? gtk_paned_get_end_child(paned)
                                     : gtk_paned_get_start_child(paned);
    if (!other) {
        close_tab(gtk_widget_get_parent(parent));
        return;
    }

    g_object_ref(other);
    if (pane_is_start)
        gtk_paned_set_end_child(paned, NULL);
    else
        gtk_paned_set_start_child(paned, NULL);

    GtkWidget *grand = gtk_widget_get_parent(parent);
    if (!grand) {
        g_object_unref(other);
        return;
    }

    if (GTK_IS_PANED(grand)) {
        gboolean is_start =
            gtk_paned_get_start_child(GTK_PANED(grand)) == parent;
        if (is_start) {
            gtk_paned_set_start_child(GTK_PANED(grand), NULL);
            gtk_paned_set_start_child(GTK_PANED(grand), other);
        } else {
            gtk_paned_set_end_child(GTK_PANED(grand), NULL);
            gtk_paned_set_end_child(GTK_PANED(grand), other);
        }
    } else if (GTK_IS_BOX(grand)) {
        gtk_box_remove(GTK_BOX(grand), parent);
        gtk_box_append(GTK_BOX(grand), other);
    }
    g_object_unref(other);

    if (was_current)
        focus_page_terminal();
}

/* ------------------------------------------------------------------ */
/* Busca                                                               */
/* ------------------------------------------------------------------ */

static void search_update(void) {
    if (!vt.current_term)
        return;

    VteTerminal *term = VTE_TERMINAL(vt.current_term);
    const char *text =
        gtk_editable_get_text(GTK_EDITABLE(vt.search_entry));

    if (!text || !*text) {
        vte_terminal_search_set_regex(term, NULL, 0);
        return;
    }

    char *escaped = g_regex_escape_string(text, -1);
    GError *error = NULL;
    VteRegex *regex = vte_regex_new_for_search(
        escaped, -1,
        VTE_REGEX_FLAGS_DEFAULT | PCRE2_CASELESS | PCRE2_MULTILINE, &error);
    g_free(escaped);

    if (!regex) {
        g_clear_error(&error);
        return;
    }
    vte_terminal_search_set_regex(term, regex, 0);
    vte_regex_unref(regex);
    vte_terminal_search_set_wrap_around(term, TRUE);
    vte_terminal_search_find_next(term);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    (void)user_data;
    search_update();
}

static void on_search_next(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    (void)user_data;
    if (vt.current_term)
        vte_terminal_search_find_next(VTE_TERMINAL(vt.current_term));
}

static void on_search_previous(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    (void)user_data;
    if (vt.current_term)
        vte_terminal_search_find_previous(VTE_TERMINAL(vt.current_term));
}

static void search_set_open(gboolean open) {
    vt.search_open = open;
    gtk_revealer_set_reveal_child(GTK_REVEALER(vt.search_revealer), open);

    if (open) {
        gtk_widget_grab_focus(vt.search_entry);
        gtk_editable_select_region(GTK_EDITABLE(vt.search_entry), 0, -1);
        search_update();
    } else {
        if (vt.current_term)
            vte_terminal_search_set_regex(VTE_TERMINAL(vt.current_term), NULL, 0);
        if (vt.current_term)
            gtk_widget_grab_focus(vt.current_term);
    }
}

static void on_search_stop(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    (void)user_data;
    search_set_open(FALSE);
}

static void on_search_close_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    search_set_open(FALSE);
}

/* ------------------------------------------------------------------ */
/* Fonte                                                               */
/* ------------------------------------------------------------------ */

static void apply_font_recursive(GtkWidget *widget) {
    if (VTE_IS_TERMINAL(widget))
        vte_terminal_set_font(VTE_TERMINAL(widget), vt.font);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        apply_font_recursive(child);
}

static void apply_font(void) {
    pango_font_description_set_size(vt.font, vt.font_size * PANGO_SCALE);
    GtkWidget *root = gtk_window_get_child(GTK_WINDOW(vt.window));
    if (root)
        apply_font_recursive(root);
}

/* ------------------------------------------------------------------ */
/* Acoes da janela                                                     */
/* ------------------------------------------------------------------ */

static void action_new_tab(GSimpleAction *action, GVariant *param,
                           gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    add_tab();
}

static void action_close_pane(GSimpleAction *action, GVariant *param,
                              gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (vt.current_pane)
        close_pane(vt.current_pane);
}

static void action_close_tab(GSimpleAction *action, GVariant *param,
                             gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    int idx = gtk_notebook_get_current_page(GTK_NOTEBOOK(vt.notebook));
    if (idx < 0)
        return;
    close_tab(gtk_notebook_get_nth_page(GTK_NOTEBOOK(vt.notebook), idx));
}

static void action_split_right(GSimpleAction *action, GVariant *param,
                               gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    split_current(GTK_ORIENTATION_HORIZONTAL);
}

static void action_split_down(GSimpleAction *action, GVariant *param,
                              gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    split_current(GTK_ORIENTATION_VERTICAL);
}

static void action_search(GSimpleAction *action, GVariant *param,
                          gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    search_set_open(!vt.search_open);
}

static void action_copy(GSimpleAction *action, GVariant *param,
                        gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (vt.current_term)
        vte_terminal_copy_clipboard_format(VTE_TERMINAL(vt.current_term),
                                          VTE_FORMAT_TEXT);
}

static void action_paste(GSimpleAction *action, GVariant *param,
                         gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (vt.current_term)
        vte_terminal_paste_clipboard(VTE_TERMINAL(vt.current_term));
}

static void action_font_increase(GSimpleAction *action, GVariant *param,
                                 gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (vt.font_size < 32) {
        vt.font_size++;
        apply_font();
    }
}

static void action_font_decrease(GSimpleAction *action, GVariant *param,
                                 gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (vt.font_size > 6) {
        vt.font_size--;
        apply_font();
    }
}

static void action_font_reset(GSimpleAction *action, GVariant *param,
                              gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    vt.font_size = vt.font_default_size;
    apply_font();
}

static void action_next_tab(GSimpleAction *action, GVariant *param,
                            gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    GtkNotebook *nb = GTK_NOTEBOOK(vt.notebook);
    int n = gtk_notebook_get_n_pages(nb);
    if (n < 2)
        return;
    int idx = gtk_notebook_get_current_page(nb);
    gtk_notebook_set_current_page(nb, (idx + 1) % n);
}

static void action_prev_tab(GSimpleAction *action, GVariant *param,
                            gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    GtkNotebook *nb = GTK_NOTEBOOK(vt.notebook);
    int n = gtk_notebook_get_n_pages(nb);
    if (n < 2)
        return;
    int idx = gtk_notebook_get_current_page(nb);
    gtk_notebook_set_current_page(nb, (idx + n - 1) % n);
}

static void action_quit(GSimpleAction *action, GVariant *param,
                        gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    gtk_window_close(GTK_WINDOW(vt.window));
}

static void action_menu(GSimpleAction *action, GVariant *param,
                        gpointer user_data) {
    (void)action;
    (void)param;
    (void)user_data;
    if (!vt.menu_button)
        return;
    if (gtk_menu_button_get_active(GTK_MENU_BUTTON(vt.menu_button)))
        gtk_menu_button_popdown(GTK_MENU_BUTTON(vt.menu_button));
    else
        gtk_menu_button_popup(GTK_MENU_BUTTON(vt.menu_button));
}

#define WIN_ACTION(action_name, callback) \
    { .name = action_name, .activate = callback }

static const GActionEntry win_actions[] = {
    WIN_ACTION("new-tab", action_new_tab),
    WIN_ACTION("close-pane", action_close_pane),
    WIN_ACTION("close-tab", action_close_tab),
    WIN_ACTION("split-right", action_split_right),
    WIN_ACTION("split-down", action_split_down),
    WIN_ACTION("search", action_search),
    WIN_ACTION("menu", action_menu),
    WIN_ACTION("copy", action_copy),
    WIN_ACTION("paste", action_paste),
    WIN_ACTION("font-increase", action_font_increase),
    WIN_ACTION("font-decrease", action_font_decrease),
    WIN_ACTION("font-reset", action_font_reset),
    WIN_ACTION("next-tab", action_next_tab),
    WIN_ACTION("prev-tab", action_prev_tab),
    WIN_ACTION("quit", action_quit),
};

#undef WIN_ACTION

/* Atalhos tratados na fase de captura, antes do VTE consumir a tecla. */
static gboolean on_window_key(GtkEventControllerKey *controller,
                              guint keyval, guint keycode,
                              GdkModifierType state, gpointer user_data) {
    (void)controller;
    (void)keycode;
    (void)user_data;

    if (!(state & GDK_CONTROL_MASK))
        return FALSE;

    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    if (keyval == GDK_KEY_Tab) {
        if (shift)
            action_prev_tab(NULL, NULL, NULL);
        else
            action_next_tab(NULL, NULL, NULL);
        return TRUE;
    }

    if (shift) {
        switch (keyval) {
        case GDK_KEY_t:
        case GDK_KEY_T:
            action_new_tab(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_n:
        case GDK_KEY_N:
            action_new_tab(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_w:
        case GDK_KEY_W:
            action_close_tab(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_x:
        case GDK_KEY_X:
            action_close_pane(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_d:
        case GDK_KEY_D:
            action_split_right(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_e:
        case GDK_KEY_E:
            action_split_down(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_f:
        case GDK_KEY_F:
            action_search(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_c:
        case GDK_KEY_C:
            action_copy(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_v:
        case GDK_KEY_V:
            action_paste(NULL, NULL, NULL);
            return TRUE;
        case GDK_KEY_q:
        case GDK_KEY_Q:
            action_quit(NULL, NULL, NULL);
            return TRUE;
        default:
            return FALSE;
        }
    }

    switch (keyval) {
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add:
        action_font_increase(NULL, NULL, NULL);
        return TRUE;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
        action_font_decrease(NULL, NULL, NULL);
        return TRUE;
    case GDK_KEY_0:
    case GDK_KEY_KP_0:
        action_font_reset(NULL, NULL, NULL);
        return TRUE;
    default:
        return FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* CSS                                                                 */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Construcao da janela                                                */
/* ------------------------------------------------------------------ */

static GtkWidget *header_button(const char *icon, const char *tooltip,
                                const char *action) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "header-btn");
    if (action) {
        char *detailed = g_strdup_printf("win.%s", action);
        gtk_actionable_set_action_name(GTK_ACTIONABLE(button), detailed);
        g_free(detailed);
    }
    return button;
}

static GtkWidget *build_menu_button(void) {
    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button),
                                  "open-menu-symbolic");
    gtk_widget_set_tooltip_text(button, "Menu");
    gtk_widget_add_css_class(button, "header-btn");
    vt.menu_button = button;

    GMenu *root = g_menu_new();

    GMenu *tabs = g_menu_new();
    g_menu_append(tabs, "Nova aba", "win.new-tab");
    g_menu_append(tabs, "Fechar aba", "win.close-tab");
    g_menu_append_section(root, NULL, G_MENU_MODEL(tabs));

    GMenu *panes = g_menu_new();
    g_menu_append(panes, "Dividir \u00e0 direita", "win.split-right");
    g_menu_append(panes, "Dividir abaixo", "win.split-down");
    g_menu_append(panes, "Fechar painel", "win.close-pane");
    g_menu_append_section(root, NULL, G_MENU_MODEL(panes));

    GMenu *edit = g_menu_new();
    g_menu_append(edit, "Buscar", "win.search");
    g_menu_append(edit, "Copiar", "win.copy");
    g_menu_append(edit, "Colar", "win.paste");
    g_menu_append_section(root, NULL, G_MENU_MODEL(edit));

    GMenu *font = g_menu_new();
    g_menu_append(font, "Aumentar fonte", "win.font-increase");
    g_menu_append(font, "Diminuir fonte", "win.font-decrease");
    g_menu_append(font, "Fonte padr\u00e3o", "win.font-reset");
    g_menu_append_section(root, NULL, G_MENU_MODEL(font));

    GMenu *quit = g_menu_new();
    g_menu_append(quit, "Sair", "win.quit");
    g_menu_append_section(root, NULL, G_MENU_MODEL(quit));

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(root));
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    g_object_unref(root);
    return button;
}

static void on_minimize_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    gtk_window_minimize(GTK_WINDOW(vt.window));
}

static void on_maximize_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    if (gtk_window_is_maximized(GTK_WINDOW(vt.window)))
        gtk_window_unmaximize(GTK_WINDOW(vt.window));
    else
        gtk_window_maximize(GTK_WINDOW(vt.window));
}

static void on_close_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    gtk_window_close(GTK_WINDOW(vt.window));
}

static void build_window(GtkApplication *app) {
    vt.window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(vt.window), 1000, 640);
    gtk_window_set_title(GTK_WINDOW(vt.window), "void-term");
    gtk_window_set_icon_name(GTK_WINDOW(vt.window), "utilities-terminal");

    vt.font = pango_font_description_from_string(
        g_getenv("VOID_TERM_FONT") ? g_getenv("VOID_TERM_FONT")
                                   : "Fira Code 11");
    vt.font_size = pango_font_description_get_size(vt.font) / PANGO_SCALE;
    if (vt.font_size <= 0)
        vt.font_size = 11;
    vt.font_default_size = vt.font_size;

    /* Barra de titulo customizada. */
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(title_box, GTK_ALIGN_CENTER);
    GtkWidget *brand = gtk_label_new("VOID-TERM");
    gtk_widget_add_css_class(brand, "brand");
    vt.title_label = gtk_label_new("void-shell");
    gtk_widget_add_css_class(vt.title_label, "title");
    gtk_label_set_ellipsize(GTK_LABEL(vt.title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(vt.title_label), 32);
    gtk_box_append(GTK_BOX(title_box), brand);
    gtk_box_append(GTK_BOX(title_box), vt.title_label);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    gtk_header_bar_pack_start(
        GTK_HEADER_BAR(header),
        header_button("window-new-symbolic", "Nova aba (Ctrl+Shift+T)",
                      "new-tab"));
    gtk_header_bar_pack_start(
        GTK_HEADER_BAR(header),
        header_button("view-dual-symbolic", "Dividir \u00e0 direita (Ctrl+Shift+D)",
                      "split-right"));
    gtk_header_bar_pack_start(
        GTK_HEADER_BAR(header),
        header_button("sidebar-show-symbolic", "Dividir abaixo (Ctrl+Shift+E)",
                      "split-down"));

    GtkWidget *close_button = gtk_button_new_with_label("\u00d7");
    gtk_widget_set_tooltip_text(close_button, "Fechar");
    gtk_widget_add_css_class(close_button, "header-btn");
    gtk_widget_add_css_class(close_button, "winbtn");
    gtk_widget_add_css_class(close_button, "close");
    g_signal_connect(close_button, "clicked",
                     G_CALLBACK(on_close_clicked), NULL);

    GtkWidget *max_button = gtk_button_new_with_label("\u25a1");
    gtk_widget_set_tooltip_text(max_button, "Maximizar");
    gtk_widget_add_css_class(max_button, "header-btn");
    gtk_widget_add_css_class(max_button, "winbtn");
    g_signal_connect(max_button, "clicked",
                     G_CALLBACK(on_maximize_clicked), NULL);

    GtkWidget *min_button = gtk_button_new_with_label("\u2212");
    gtk_widget_set_tooltip_text(min_button, "Minimizar");
    gtk_widget_add_css_class(min_button, "header-btn");
    gtk_widget_add_css_class(min_button, "winbtn");
    g_signal_connect(min_button, "clicked",
                     G_CALLBACK(on_minimize_clicked), NULL);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), close_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), max_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), min_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), build_menu_button());
    gtk_header_bar_pack_end(
        GTK_HEADER_BAR(header),
        header_button("edit-find-symbolic", "Buscar (Ctrl+Shift+F)",
                      "search"));

    gtk_window_set_titlebar(GTK_WINDOW(vt.window), header);

    /* Barra de busca. */
    vt.search_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(
        GTK_REVEALER(vt.search_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(vt.search_revealer), 140);

    GtkWidget *search_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(search_bar, "search-bar");

    vt.search_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(vt.search_entry),
                                          "Buscar no scrollback...");
    gtk_widget_set_hexpand(vt.search_entry, TRUE);
    g_signal_connect(vt.search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(vt.search_entry, "activate",
                     G_CALLBACK(on_search_next), NULL);
    g_signal_connect(vt.search_entry, "next-match",
                     G_CALLBACK(on_search_next), NULL);
    g_signal_connect(vt.search_entry, "previous-match",
                     G_CALLBACK(on_search_previous), NULL);
    g_signal_connect(vt.search_entry, "stop-search",
                     G_CALLBACK(on_search_stop), NULL);

    GtkWidget *prev_button = gtk_button_new_from_icon_name(
        "go-previous-symbolic");
    gtk_widget_set_tooltip_text(prev_button, "Ocorr\u00eancia anterior");
    g_signal_connect(prev_button, "clicked",
                     G_CALLBACK(on_search_previous), NULL);

    GtkWidget *next_button = gtk_button_new_from_icon_name(
        "go-next-symbolic");
    gtk_widget_set_tooltip_text(next_button, "Pr\u00f3xima ocorr\u00eancia");
    g_signal_connect(next_button, "clicked",
                     G_CALLBACK(on_search_next), NULL);

    GtkWidget *search_close = gtk_button_new_from_icon_name(
        "window-close-symbolic");
    gtk_widget_set_tooltip_text(search_close, "Fechar busca (Esc)");
    g_signal_connect(search_close, "clicked",
                     G_CALLBACK(on_search_close_clicked), NULL);

    gtk_box_append(GTK_BOX(search_bar), vt.search_entry);
    gtk_box_append(GTK_BOX(search_bar), prev_button);
    gtk_box_append(GTK_BOX(search_bar), next_button);
    gtk_box_append(GTK_BOX(search_bar), search_close);
    gtk_revealer_set_child(GTK_REVEALER(vt.search_revealer), search_bar);

    /* Abas. */
    vt.notebook = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(vt.notebook), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(vt.notebook), TRUE);
    gtk_widget_set_vexpand(vt.notebook, TRUE);
    g_signal_connect(vt.notebook, "switch-page",
                     G_CALLBACK(on_switch_page), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), vt.search_revealer);
    gtk_box_append(GTK_BOX(root), vt.notebook);
    gtk_window_set_child(GTK_WINDOW(vt.window), root);

    /* Acoes e atalhos. */
    g_action_map_add_action_entries(G_ACTION_MAP(vt.window), win_actions,
                                    G_N_ELEMENTS(win_actions), &vt);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_window_key), NULL);
    gtk_widget_add_controller(vt.window, keys);

    static const char *accels[][2] = {
        {"win.new-tab", "<Control><Shift>t"},
        {"win.new-tab", "<Control><Shift>n"},
        {"win.close-tab", "<Control><Shift>w"},
        {"win.close-pane", "<Control><Shift>x"},
        {"win.split-right", "<Control><Shift>d"},
        {"win.split-down", "<Control><Shift>e"},
        {"win.search", "<Control><Shift>f"},
        {"win.menu", "<Control><Shift>m"},
        {"win.copy", "<Control><Shift>c"},
        {"win.paste", "<Control><Shift>v"},
        {"win.font-increase", "<Control>plus"},
        {"win.font-increase", "<Control>equal"},
        {"win.font-decrease", "<Control>minus"},
        {"win.font-reset", "<Control>0"},
        {"win.next-tab", "<Control>Tab"},
        {"win.prev-tab", "<Control><Shift>Tab"},
        {"win.quit", "<Control><Shift>q"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(accels); i++)
        gtk_application_set_accels_for_action(GTK_APPLICATION(vt.app),
                                              accels[i][0], (const char *[]){
                                                  accels[i][1], NULL});

    add_tab();
}

/* ------------------------------------------------------------------ */
/* Aplicacao                                                           */
/* ------------------------------------------------------------------ */

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    if (vt.window) {
        gtk_window_present(GTK_WINDOW(vt.window));
        return;
    }
    vt.app = app;
    chrome_init();
    build_window(app);
    gtk_window_present(GTK_WINDOW(vt.window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("dev.telinii.VoidTerm",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (vt.font)
        pango_font_description_free(vt.font);
    g_object_unref(app);
    return status;
}
