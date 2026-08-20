// r36pda — PDA-десктоп для R36S (ArkOS) и для теста на ПК.
// C++17 + SDL2 + Lua 5.3. Без внешних GUI-библиотек.
// Встроенные приложения: терминал, FAR-файлы, система, процессы,
// калькулятор, настройки, Lua-скрипты.
#include "lua.h"

// режимы и struct App — определены в app.h (включается через lua.h)
static std::vector<App> load_external_apps(const std::string &path) {
    std::vector<App> out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t p1 = s.find('|');
        if (p1 == std::string::npos) continue;
        size_t p2 = s.find('|', p1 + 1);
        if (p2 == std::string::npos) continue;
        App a;
        a.name = s.substr(0, p1);
        a.command = s.substr(p1 + 1, p2 - p1 - 1);
        int r = 80, g = 180, b = 230;
        sscanf(s.substr(p2 + 1).c_str(), "%d,%d,%d", &r, &g, &b);
        a.color[0] = (Uint8)r; a.color[1] = (Uint8)g; a.color[2] = (Uint8)b;
        a.mode = M_EXTERNAL;
        if (!a.name.empty() && !a.command.empty()) out.push_back(a);
    }
    fclose(f);
    return out;
}

static std::vector<App> builtin_apps() {
    std::vector<App> out;
    App a;
    a.name = "Terminal";  a.mode = M_TERM;    a.color[0]=80;  a.color[1]=200; a.color[2]=120; out.push_back(a);
    a.name = "Files";     a.mode = M_FILES;   a.color[0]=120; a.color[1]=120; a.color[2]=220; out.push_back(a);
    a.name = "System";    a.mode = M_SYSINFO; a.color[0]=100; a.color[1]=200; a.color[2]=200; out.push_back(a);
    a.name = "Processes"; a.mode = M_PROC;    a.color[0]=240; a.color[1]=90;  a.color[2]=90;  out.push_back(a);
    a.name = "Calculator";a.mode = M_CALC;    a.color[0]=255; a.color[1]=200; a.color[2]=80;  out.push_back(a);
    a.name = "Settings";  a.mode = M_SETTINGS; a.color[0]=170; a.color[1]=170; a.color[2]=180; out.push_back(a);
    return out;
}

// ----------------------------- иконки -----------------------------
static void drawicon(Renderer &R, const App &a, int x, int y, int sz, bool sel) {
    Uint32 base = rgb(a.color[0], a.color[1], a.color[2]);
    if (sel) base = rgb(255, 255, 255);
    fillrect(R, x, y, sz, sz, base);
    Uint32 border = sel ? rgb(255, 220, 60) : rgb(40, 40, 40);
    for (int i = 0; i < sz; ++i) {
        putpx(R, x, y + i, border);
        putpx(R, x + sz - 1, y + i, border);
        putpx(R, x + i, y, border);
        putpx(R, x + i, y + sz - 1, border);
    }
    char buf[8];
    int len = 0;
    int cp = utf8_decode((const unsigned char *)a.name.c_str(), &len);
    if (cp > 0) {
        int n = 0; const unsigned char *q = (const unsigned char *)a.name.c_str();
        while (n < len) { buf[n] = (char)q[n]; ++n; }
        buf[n] = 0;
    } else { buf[0] = '?'; buf[1] = 0; }
    int ls = sz / 8 > 1 ? sz / 8 : 2;
    int tw = textw(buf, ls);
    drawtext(R, buf, x + (sz - tw) / 2, y + (sz - 7 * ls) / 2, ls, sel ? rgb(0,0,0) : rgb(255,255,255));
}

static void drawlabel(Renderer &R, const std::string &name, int x, int y, int w, int s, bool sel) {
    Uint32 c = sel ? rgb(255, 220, 60) : rgb(230, 230, 230);
    std::string t = name;
    int avail = w / (5 * s + s);
    if ((int)t.size() > avail) t = t.substr(0, avail);
    drawtext(R, t.c_str(), x, y, s, c);
}

// ----------------------------- курсор-стрелка -----------------------------
static void draw_cursor(Renderer &R, int cx, int cy) {
    for (int i = 0; i < 7; ++i) {
        putpx(R, cx + i, cy, rgb(255, 255, 255));
        putpx(R, cx, cy + i, rgb(255, 255, 255));
        putpx(R, cx + i, cy + i, rgb(255, 255, 255));
    }
    fillrect(R, cx, cy, 6, 6, rgb(255, 255, 0));
}

// ----------------------------- крестик закрытия -----------------------------
// возвращает true, если курсор над крестиком
static bool draw_close(Renderer &R, int cx, int cy, int x, int y) {
    fillrect(R, x, y, 20, 20, rgb(120, 30, 30));
    drawtext(R, "x", x + 5, y + 3, 2, rgb(255, 255, 255));
    bool over = cx >= x && cx <= x + 20 && cy >= y && cy <= y + 20;
    if (over) fillrect(R, x - 1, y - 1, 22, 22, rgb(255, 220, 60));
    return over;
}

// ----------------------------- экраны -----------------------------
static void render_desktop(Renderer &R, const std::vector<App> &apps, int sel,
                           int cx, int cy, const char *clock, const char *batt,
                           const Settings &S, int &hover) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    for (int y = STATUS_H; y < SCREEN_H; ++y) {
        Uint8 shade = (Uint8)(16 + (y * 18) / SCREEN_H);
        Uint32 c = rgb(shade, shade, shade + 5);
        for (int x = 0; x < SCREEN_W; ++x) R.pix[y * (R.pitch / 4) + x] = c;
    }
    draw_statusbar(R, clock, batt, "r36pda");
    int sz = S.icon_size;
    int pad = 14;
    int cols = (SCREEN_W - 2 * pad) / (sz + 20);
    if (cols < 1) cols = 1;
    int rows = ((int)apps.size() + cols - 1) / cols;
    int grid_w = cols * (sz + 20) - 20;
    int grid_h = rows * (sz + 26);
    int gx = (SCREEN_W - grid_w) / 2;
    int gy = STATUS_H + 8 + std::max(0, (SCREEN_H - STATUS_H - 8 - grid_h) / 2 - 6);
    hover = -1;
    for (size_t i = 0; i < apps.size(); ++i) {
        int r = (int)(i / cols), c = (int)(i % cols);
        int x = gx + c * (sz + 20);
        int y = gy + r * (sz + 26);
        bool selb = ((int)i == sel);
        if (cx >= x && cx <= x + sz && cy >= y && cy <= y + sz) { selb = true; hover = (int)i; }
        drawicon(R, apps[i], x, y, sz, selb);
        drawlabel(R, apps[i].name, x - 10, y + sz + 3, sz + 20, 2, selb);
    }
    draw_cursor(R, cx, cy);
    const char *h = "A: run  stick: cursor  Select+Start: quit";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(110, 110, 125));
}

static void render_term(Renderer &R, TermState &T, const char *clock, const char *batt, const Settings &S) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Terminal");
    int kb_h = 4 * 42 + 34;
    int area_bottom = SCREEN_H - kb_h - 2;
    // подсказки
    if (!T.suggestions.empty()) {
        std::string sug;
        for (auto &s : T.suggestions) { sug += s; sug += " "; }
        if ((int)sug.size() > 60) sug = sug.substr(0, 60);
        drawtext(R, sug.c_str(), 8, area_bottom - 40, 2, rgb(140, 220, 255));
        drawtext(R, "Tab: дополнить", 8, area_bottom - 22, 2, rgb(120, 120, 135));
    }
    int line_h = 16;
    int maxlines = (area_bottom - STATUS_H - 4) / line_h;
    size_t start = T.lines.size() > (size_t)maxlines ? T.lines.size() - (size_t)maxlines : 0;
    for (size_t i = start; i < T.lines.size(); ++i) {
        std::string s = T.lines[i];
        if ((int)s.size() > 60) s = s.substr(0, 60);
        drawtext(R, s.c_str(), 8, STATUS_H + 4 + (int)(i - start) * line_h, 2, rgb(220, 220, 220));
    }
    if (T.running) drawtext(R, "...", SCREEN_W - 40, area_bottom - 14, 2, rgb(255, 200, 80));
    std::string full = "$ " + T.input;
    if ((int)full.size() > 58) full = full.substr(full.size() - 58);
    int cw = textw(full.c_str(), 2);
    drawtext(R, full.c_str(), 8, area_bottom - 18, 2, rgb(255, 255, 255));
    fillrect(R, 8 + cw, area_bottom - 18, 2, 14, rgb(255, 255, 255));
    draw_keyboard(R, T.kb, SCREEN_H - kb_h, S);
}

static void render_sysinfo(Renderer &R, const std::vector<std::string> &lines, int scroll, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "System info");
    int line_h = 16;
    int y = STATUS_H + 8;
    int maxlines = (SCREEN_H - y - 20) / line_h;
    if (scroll < 0) scroll = 0;
    for (size_t i = (size_t)scroll; i < lines.size() && (int)(i - scroll) < maxlines; ++i) {
        std::string s = lines[i];
        if ((int)s.size() > 58) s = s.substr(0, 58);
        drawtext(R, s.c_str(), 8, y + (int)(i - scroll) * line_h, 2, rgb(220, 220, 220));
    }
    const char *h = "B: back";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
}

static void render_proc(Renderer &R, ProcState &P, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Processes");
    if (!P.msg.empty()) drawtext(R, P.msg.c_str(), 8, STATUS_H + 4, 2, rgb(255, 120, 120));
    int line_h = 16;
    int y = STATUS_H + 20;
    int maxlines = (SCREEN_H - y - 16) / line_h;
    int start = P.sel - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)P.procs.size() && i < start + maxlines; ++i) {
        bool sel = (i == P.sel);
        char b[32];
        snprintf(b, sizeof b, "%6d", P.procs[i].first);
        std::string s = b;
        s += "  " + P.procs[i].second;
        if ((int)s.size() > 50) s = s.substr(0, 50);
        drawtext(R, s.c_str(), 8, y + (i - start) * line_h, 2, sel ? rgb(255, 220, 60) : rgb(220, 220, 220));
    }
    if (P.confirm > 0) {
        fillrect(R, 120, 200, 400, 60, rgb(70, 25, 25));
        char b[64];
        snprintf(b, sizeof b, "Kill PID %d?", P.confirm);
        drawtext(R, b, 140, 214, 2, rgb(255, 255, 255));
        drawtext(R, "A: yes  B: no", 140, 236, 2, rgb(255, 200, 80));
    } else {
        const char *h = "Start: kill  B: back";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
    }
}

static const char *CALC_GRID[4] = { "789/", "456*", "123-", "0.C=" };
static void render_calc(Renderer &R, CalcState &C, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Calculator");
    fillrect(R, 8, STATUS_H + 8, SCREEN_W - 16, 40, rgb(20, 20, 30));
    std::string d = C.disp;
    if ((int)d.size() > 20) d = d.substr(d.size() - 20);
    drawtext(R, d.c_str(), SCREEN_W - 8 - textw(d.c_str(), 3), STATUS_H + 16, 3, rgb(255, 255, 255));
    int kw = 150, kh = 60;
    int x0 = (SCREEN_W - 4 * kw) / 2;
    int y0 = STATUS_H + 60;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            int x = x0 + c * kw, y = y0 + r * kh;
            bool sel = (C.brow == r && C.bcol == c);
            char ch = CALC_GRID[r][c];
            Uint32 col = sel ? rgb(255, 220, 60) : (ch >= '0' && ch <= '9' ? rgb(60, 60, 80) : rgb(80, 80, 100));
            fillrect(R, x, y, kw - 4, kh - 4, col);
            char b[2] = { ch, 0 };
            drawtext(R, b, x + (kw - textw(b, 3)) / 2, y + (kh - 21) / 2, 3, sel ? rgb(0,0,0) : rgb(230,230,230));
        }
    const char *h = "B: back";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
}

// ----------------------------- настройки -----------------------------
struct SettingsUI {
    int item = 0;
    int remap = -1;    // какой пункт переназначаем (-1=нет)
    std::vector<std::string> items;
    SettingsUI() {
        items = { "Кнопка A", "Кнопка B", "Кнопка Start", "Кнопка Select",
                  "Кнопка Fn", "Вверх", "Вниз", "Влево", "Вправо",
                  "Размер иконок", "Масштаб клавиатуры", "Лог debug.log" };
    }
};

static void render_settings(Renderer &R, SettingsUI &UI, Settings &S, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Settings");
    int y = STATUS_H + 8;
    for (size_t i = 0; i < UI.items.size(); ++i) {
        bool sel = (int)i == UI.item;
        std::string v;
        if (UI.remap == (int)i) v = "... нажми кнопку";
        else if (i <= 8) v = std::to_string(S.btn[i]);
        else if (i == 9) v = std::to_string(S.icon_size);
        else if (i == 10) v = std::to_string(S.kb_scale);
        else v = S.log_enabled ? "вкл" : "выкл";
        std::string line = UI.items[i] + ": " + v;
        if ((int)line.size() > 56) line = line.substr(0, 56);
        drawtext(R, line.c_str(), 8, y + (int)i * 18, 2, sel ? rgb(255, 220, 60) : rgb(220, 220, 220));
        if (sel) fillrect(R, SCREEN_W - 8, y + (int)i * 18, 6, 14, rgb(255, 220, 60));
    }
    const char *h = "A: изменить  B: back";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
    if (UI.remap >= 0) {
        fillrect(R, 140, 220, 360, 50, rgb(40, 40, 70));
        drawtext(R, "Нажми любую кнопку на геймпаде", 156, 232, 2, rgb(255, 255, 255));
        drawtext(R, "или Start чтобы отменить", 156, 252, 2, rgb(255, 200, 80));
    }
}

// ----------------------------- ввод -----------------------------
static int gp_btn(Settings &S, int b) {
    for (int i = 0; i < BTN_MAX; ++i) if (S.btn[i] == b) return i;
    return -1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Renderer R;
    extern Renderer *g_R;
    g_R = &R;
    Uint32 flags = 0;
    const char *fs = getenv("R36PDA_FULLSCREEN");
    if (fs && fs[0] == '1') flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    R.win = SDL_CreateWindow("r36pda", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, flags);
    if (!R.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R.ren) R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_SOFTWARE);
    if (!R.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    R.fb = SDL_CreateTexture(R.ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!R.fb) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 1; }
    if (SDL_NumJoysticks() > 0) R.joy = SDL_JoystickOpen(0);

    Settings S;
    S.load();
    S.log("=== r36pda start ===");

    // приложения
    std::vector<App> apps = builtin_apps();
#ifdef USE_LUA
    for (auto &a : lua_apps()) apps.push_back(a);
#endif
    std::vector<App> ext = load_external_apps("config/apps.cfg");
    if (ext.empty()) ext = load_external_apps("apps.cfg");
    for (auto &a : ext) apps.push_back(a);

    AppMode mode = M_DESKTOP;
    int sel = 0;
    int hover = -1;
    bool running = true;
    bool fn_pressed = false;
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;

    char clockbuf[16], battbuf[16];
    read_clock(clockbuf, sizeof clockbuf);
    read_battery(battbuf, sizeof battbuf);

    TermState term;
    FileState files;
    ProcState procs;
    CalcState calc;
    SettingsUI su;
    std::vector<std::string> sysinfo;
    int sysinfo_scroll = 0;
    bool sysinfo_loaded = false;

    SDL_StartTextInput();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = false; break;

            case SDL_JOYAXISMOTION: {
                int ax = ev.jaxis.axis, v = ev.jaxis.value;
                if (ax == S.axis_x) cx += v / (S.axis_thresh / 4);
                else if (ax == S.axis_y) cy += v / (S.axis_thresh / 4);
                if (cx < 0) cx = 0; if (cx >= SCREEN_W) cx = SCREEN_W - 1;
                if (cy < 0) cy = 0; if (cy >= SCREEN_H) cy = SCREEN_H - 1;
                break;
            }

            case SDL_JOYBUTTONDOWN: {
                int b = ev.jbutton.button;
                int act = gp_btn(S, b);   // 0..8 или -1
                if (act == BTN_FN) { fn_pressed = true; S.log("Fn down"); break; }
                if (act == BTN_START && fn_pressed) { running = false; fn_pressed = false; break; }
                if (fn_pressed) {
                    if (act == BTN_A || act == BTN_B) { if (mode != M_DESKTOP) mode = M_DESKTOP; }
                    fn_pressed = false;
                    break;
                }
                if (act < 0) break;

                // настройки: режим переназначения
                if (mode == M_SETTINGS && su.remap >= 0) {
                    S.btn[su.remap] = b;
                    su.remap = -1;
                    S.save();
                    break;
                }

                switch (mode) {
                case M_DESKTOP: {
                    if (act == BTN_UP || act == BTN_DOWN || act == BTN_LEFT || act == BTN_RIGHT) {
                        int cols = (SCREEN_W - 28) / (S.icon_size + 20);
                        if (cols < 1) cols = 1;
                        int row = sel / cols, col = sel % cols;
                        if (act == BTN_UP && row > 0) sel -= cols;
                        if (act == BTN_DOWN) { int rr = row + 1; int maxrow = ((int)apps.size() - 1) / cols; if (rr <= maxrow) sel = rr * cols + std::min(col, cols - 1); }
                        if (act == BTN_LEFT && col > 0) sel -= 1;
                        if (act == BTN_RIGHT) { if (col < cols - 1 && sel + 1 < (int)apps.size()) sel += 1; }
                    } else if (act == BTN_A) {
                        int t = hover >= 0 ? hover : sel;
                        if (!apps.empty() && t >= 0 && t < (int)apps.size()) {
                            App &a = apps[t];
                            if (a.mode == M_EXTERNAL) {
                                SDL_HideWindow(R.win);
                                launch_and_wait(a.command);
                                SDL_ShowWindow(R.win); SDL_RaiseWindow(R.win);
                            } else {
                                mode = a.mode; sel = 0;
                                if (mode == M_TERM) { term.kb = Kbd(); term.input.clear(); term.lines.clear(); term.add_line("Введи команду. Tab - дополнение, Select - подсказка."); }
                                if (mode == M_FILES) { files.refresh(); }
                                if (mode == M_PROC) procs.refresh();
                                if (mode == M_SYSINFO && !sysinfo_loaded) { sysinfo = sysinfo_lines(); sysinfo_loaded = true; }
#ifdef USE_LUA
                                if (mode == M_SCRIPT) lua_load_file(a.command);
#endif
                            }
                        }
                    }
                    break;
                }
                case M_TERM:
                    if (act == BTN_UP) term.kb.move(-1, 0);
                    else if (act == BTN_DOWN) term.kb.move(1, 0);
                    else if (act == BTN_LEFT) term.kb.move(0, -1);
                    else if (act == BTN_RIGHT) term.kb.move(0, 1);
                    else if (act == BTN_A || act == BTN_B) {
                        const char *k = term.kb.cell_char();
                        if (!k) break;
                        std::string key = k;
                        if (key == "space") term.input += ' ';
                        else if (key == "del") { if (!term.input.empty()) term.input.pop_back(); }
                        else if (key == "tab") term.complete();
                        else if (key == "enter") term.run(term.input);
                        else if (key == "abc") term.kb.page = 1 - term.kb.page;
                        else if (key == "shift") term.kb.shift = !term.kb.shift;
                        else if (key == "exit") mode = M_DESKTOP;
                        else term.input += key;
                        term.update_suggestions();
                    }
                    else if (act == BTN_START) term.run(term.input);
                    else if (act == BTN_SELECT) term.update_suggestions();
                    break;
                case M_FILES:
                    if (act == BTN_UP) { Panel &p = files.cur(); if (p.viewing) p.vscroll = std::max(0, p.vscroll - 1); else if (p.sel > 0) --p.sel; }
                    else if (act == BTN_DOWN) { Panel &p = files.cur(); if (p.viewing) p.vscroll = std::min((int)p.view.size(), p.vscroll + 1); else if (p.sel < (int)p.entries.size() - 1) ++p.sel; }
                    else if (act == BTN_LEFT) files.focus = 0;
                    else if (act == BTN_RIGHT) files.focus = 1;
                    else if (act == BTN_A) { if (files.confirm_delete) files.do_delete(); else files.cur().enter(); }
                    else if (act == BTN_B) { if (files.confirm_delete) files.confirm_delete = false; else files.cur().up(); }
                    else if (act == BTN_SELECT) files.focus = 1 - files.focus;
                    else if (act == BTN_START) files.cmd_copy();
                    break;
                case M_SYSINFO:
                    if (act == BTN_UP) sysinfo_scroll = std::max(0, sysinfo_scroll - 1);
                    else if (act == BTN_DOWN) sysinfo_scroll = std::min((int)sysinfo.size() - 1, sysinfo_scroll + 1);
                    else if (act == BTN_B) mode = M_DESKTOP;
                    break;
                case M_PROC:
                    if (act == BTN_UP) procs.sel = std::max(0, procs.sel - 1);
                    else if (act == BTN_DOWN) procs.sel = std::min((int)procs.procs.size() - 1, procs.sel + 1);
                    else if (act == BTN_START) { if (procs.sel >= 0 && procs.sel < (int)procs.procs.size()) procs.confirm = procs.procs[procs.sel].first; }
                    else if (act == BTN_A && procs.confirm > 0) procs.do_kill();
                    else if (act == BTN_B) { if (procs.confirm > 0) procs.confirm = -1; else mode = M_DESKTOP; }
                    break;
                case M_CALC:
                    if (act == BTN_UP) calc.brow = std::max(0, calc.brow - 1);
                    else if (act == BTN_DOWN) calc.brow = std::min(3, calc.brow + 1);
                    else if (act == BTN_LEFT) calc.bcol = std::max(0, calc.bcol - 1);
                    else if (act == BTN_RIGHT) calc.bcol = std::min(3, calc.bcol + 1);
                    else if (act == BTN_A || act == BTN_B) calc_press(calc, CALC_GRID[calc.brow][calc.bcol]);
                    else if (act == BTN_B) mode = M_DESKTOP;
                    break;
                case M_SETTINGS:
                    if (act == BTN_UP) su.item = std::max(0, su.item - 1);
                    else if (act == BTN_DOWN) su.item = std::min((int)su.items.size() - 1, su.item + 1);
                    else if (act == BTN_A) { if (su.item <= 8) su.remap = su.item; }
                    else if (act == BTN_B) mode = M_DESKTOP;
                    else if (act == BTN_LEFT || act == BTN_RIGHT) {
                        if (su.item == 9) { S.icon_size += (act == BTN_RIGHT ? 8 : -8); S.save(); }
                        else if (su.item == 10) { S.kb_scale += (act == BTN_RIGHT ? 1 : -1); S.save(); }
                        else if (su.item == 11) { S.log_enabled = !S.log_enabled; S.save(); }
                    }
                    break;
#ifdef USE_LUA
                case M_SCRIPT:
                    g_lua.keys.push_back(act == BTN_A ? "a" : act == BTN_B ? "b" :
                                          act == BTN_UP ? "up" : act == BTN_DOWN ? "down" :
                                          act == BTN_LEFT ? "left" : act == BTN_RIGHT ? "right" :
                                          act == BTN_START ? "start" : "select");
                    break;
#endif
                case M_EXTERNAL: break;
                }
                break;
            }

            case SDL_JOYBUTTONUP:
                if (gp_btn(S, ev.jbutton.button) == BTN_FN) fn_pressed = false;
                break;

            case SDL_KEYDOWN: {
                if (ev.key.repeat) break;
                int sym = ev.key.keysym.sym;
                switch (mode) {
                case M_DESKTOP:
                    if (sym == SDLK_UP || sym == SDLK_w) sel = std::max(0, sel - 4);
                    else if (sym == SDLK_DOWN || sym == SDLK_s) sel += 4;
                    else if (sym == SDLK_LEFT || sym == SDLK_a) sel = std::max(0, sel - 1);
                    else if (sym == SDLK_RIGHT || sym == SDLK_d) sel += 1;
                    else if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
                        if (!apps.empty()) {
                            App &a = apps[hover >= 0 ? hover : sel];
                            if (a.mode == M_EXTERNAL) {
                                SDL_HideWindow(R.win);
                                launch_and_wait(a.command);
                                SDL_ShowWindow(R.win); SDL_RaiseWindow(R.win);
                            } else { mode = a.mode; }
                        }
                    }
                    else if (sym == SDLK_ESCAPE) running = false;
                    break;
                case M_TERM:
                    if (sym == SDLK_ESCAPE) mode = M_DESKTOP;
                    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) term.run(term.input);
                    else if (sym == SDLK_BACKSPACE && !term.input.empty()) term.input.pop_back();
                    else if (sym == SDLK_UP) term.kb.move(-1, 0);
                    else if (sym == SDLK_DOWN) term.kb.move(1, 0);
                    else if (sym == SDLK_LEFT) term.kb.move(0, -1);
                    else if (sym == SDLK_RIGHT) term.kb.move(0, 1);
                    else if (sym == SDLK_TAB) term.complete();
                    else if (sym == SDLK_F1) mode = M_DESKTOP;
                    break;
                default:
                    if (sym == SDLK_ESCAPE || sym == SDLK_F1) mode = M_DESKTOP;
                    break;
                }
                break;
            }

            case SDL_TEXTINPUT: {
                if (mode == M_TERM) {
                    std::string t = ev.text.text;
                    for (size_t i = 0; i < t.size(); ++i) {
                        unsigned char ch = (unsigned char)t[i];
                        if (ch >= 32 && ch != 127) term.input += t[i];
                    }
                    term.update_suggestions();
                }
                break;
            }
            }
        }

        if (!apps.empty()) {
            if (sel < 0) sel = 0;
            if (sel >= (int)apps.size()) sel = (int)apps.size() - 1;
        }

        term.pump();
        if (term.exit_requested) { term.exit_requested = false; mode = M_DESKTOP; }

        // Lua скрипт
#ifdef USE_LUA
        if (mode == M_SCRIPT) {
            lua_frame();
            if (!g_lua.running) mode = M_DESKTOP;
        }
#endif

        if (SDL_LockTexture(R.fb, nullptr, (void **)&R.pix, &R.pitch) != 0) {
            fprintf(stderr, "SDL_LockTexture: %s\n", SDL_GetError());
            break;
        }

        // крестик закрытия в приложениях (кроме рабочего стола)
        bool close_over = false;
        switch (mode) {
        case M_DESKTOP:
            render_desktop(R, apps, sel, cx, cy, clockbuf, battbuf, S, hover);
            break;
        case M_TERM:
            render_term(R, term, clockbuf, battbuf, S);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        case M_FILES:
            render_files(R, files, clockbuf, battbuf);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        case M_SYSINFO:
            render_sysinfo(R, sysinfo, sysinfo_scroll, clockbuf, battbuf);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        case M_PROC:
            render_proc(R, procs, clockbuf, battbuf);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        case M_CALC:
            render_calc(R, calc, clockbuf, battbuf);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        case M_SETTINGS:
            render_settings(R, su, S, clockbuf, battbuf);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
#ifdef USE_LUA
        case M_SCRIPT: {
            fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
            draw_statusbar(R, clockbuf, battbuf, "Script");
            int y = STATUS_H + 8;
            for (size_t i = 0; i < g_lua.out.size() && i < 24; ++i) {
                std::string s = g_lua.out[i];
                if ((int)s.size() > 58) s = s.substr(0, 58);
                drawtext(R, s.c_str(), 8, y + (int)i * 16, 2, rgb(220, 220, 220));
            }
            draw_cursor(R, cx, cy);
            close_over = draw_close(R, cx, cy, SCREEN_W - 28, 2);
            break;
        }
#endif
        case M_EXTERNAL:
            render_desktop(R, apps, sel, cx, cy, clockbuf, battbuf, S, hover);
            break;
        }

        // A по крестику = выход из приложения
        if (close_over && (mode == M_TERM || mode == M_FILES || mode == M_SYSINFO ||
                           mode == M_PROC || mode == M_CALC || mode == M_SETTINGS
#ifdef USE_LUA
                           || mode == M_SCRIPT
#endif
                           )) mode = M_DESKTOP;

        SDL_UnlockTexture(R.fb);
        SDL_RenderCopy(R.ren, R.fb, nullptr, nullptr);
        SDL_RenderPresent(R.ren);

        static Uint32 last_tick = 0;
        if (SDL_GetTicks() - last_tick > 1000) {
            read_clock(clockbuf, sizeof clockbuf);
            read_battery(battbuf, sizeof battbuf);
            last_tick = SDL_GetTicks();
        }
        SDL_Delay(16);
    }

    S.log("=== r36pda exit ===");
    if (R.joy) SDL_JoystickClose(R.joy);
    SDL_DestroyTexture(R.fb);
    SDL_DestroyRenderer(R.ren);
    SDL_DestroyWindow(R.win);
    SDL_Quit();
    return 0;
}

Renderer *g_R = nullptr;