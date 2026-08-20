// r36pda — PDA-десктоп для R36S (ArkOS) и для теста на ПК.
// C++17 + SDL2. Без внешних библиотек: встроенный шрифт font.h.
// Встроенные приложения: терминал с экранной клавиатурой, файлы,
// системная информация, процессы, калькулятор. Всё работает без X11.

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include "font.h"

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
  #include <direct.h>
  #define GETCWD _getcwd
  #define POPEN _popen
  #define PCLOSE _pclose
  #define ISDIR(st) (0)
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <dirent.h>
  #define GETCWD getcwd
  #define POPEN popen
  #define PCLOSE pclose
  #define ISDIR(st) S_ISDIR((st).st_mode)
#endif

#define SCREEN_W 640
#define SCREEN_H 480
#define STATUS_H 28
#define MARGIN 24
#define ICON_SLOT 152
#define LABEL_H 26

enum Mode { M_DESKTOP, M_TERM, M_FILES, M_SYSINFO, M_PROC, M_CALC, M_EXTERNAL };

struct App {
    std::string name;
    Mode mode = M_DESKTOP;
    std::string command;
    Uint8 color[3] = {80, 180, 230};
};

// внешние команды из config/apps.cfg: имя | команда | R,G,B
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
    return out;
}

struct Renderer {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *fb = nullptr;
    Uint32 *pix = nullptr;
    int pitch = 0;
    SDL_Joystick *joy = nullptr;
};

static Uint32 rgb(Uint8 r, Uint8 g, Uint8 b) {
    return ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

static void putpx(Renderer &R, int x, int y, Uint32 c) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    R.pix[y * (R.pitch / 4) + x] = c;
}

static void fillrect(Renderer &R, int x, int y, int w, int h, Uint32 c) {
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            putpx(R, xx, yy, c);
}

static void drawtext(Renderer &R, const char *str, int x, int y, int s, Uint32 c) {
    const unsigned char *p = (const unsigned char *)str;
    int cx = x;
    while (*p) {
        int len = 0;
        int cp = utf8_decode(p, &len);
        const Glyph *g = font_glyph(cp);
        if (g) {
            for (int r = 0; r < 7; ++r) {
                for (int b = 0; b < 5; ++b) {
                    if ((g->r[r] >> (4 - b)) & 1) {
                        for (int dy = 0; dy < s; ++dy)
                            for (int dx = 0; dx < s; ++dx)
                                putpx(R, cx + b * s + dx, y + r * s + dy, c);
                    }
                }
            }
            cx += 5 * s + s;
        } else {
            cx += 5 * s + s;
        }
        p += len;
    }
}

static int textw(const char *str, int s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        int len = 0;
        utf8_decode(p, &len);
        w += 5 * s + s;
        p += len;
    }
    return w;
}

static void draw_statusbar(Renderer &R, const char *clock, const char *batt, const char *title) {
    fillrect(R, 0, 0, SCREEN_W, STATUS_H, rgb(30, 30, 45));
    drawtext(R, clock, 8, (STATUS_H - 14) / 2, 2, rgb(255, 255, 255));
    if (title) {
        int tw = textw(title, 2);
        drawtext(R, title, (SCREEN_W - tw) / 2, (STATUS_H - 14) / 2, 2, rgb(200, 200, 210));
    }
    std::string bt = std::string("BAT ") + batt;
    int bw = textw(bt.c_str(), 2);
    drawtext(R, bt.c_str(), SCREEN_W - 8 - bw, (STATUS_H - 14) / 2, 2, rgb(120, 255, 120));
    fillrect(R, 0, STATUS_H - 2, SCREEN_W, 2, rgb(60, 60, 90));
}

static void read_battery(char *out, size_t n) {
#ifdef _WIN32
    snprintf(out, n, "--");
#else
    struct stat st;
    if (stat("/sys/class/power_supply", &st) == 0) {
        DIR *d = opendir("/sys/class/power_supply");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (e->d_name[0] == '.') continue;
                char p[256];
                snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
                FILE *f = fopen(p, "r");
                if (f) {
                    char buf[16];
                    if (fgets(buf, sizeof buf, f)) {
                        for (char *q = buf; *q; ++q) if (*q == '\n' || *q == '\r') { *q = 0; break; }
                        snprintf(out, n, "%s%%", buf);
                        fclose(f); closedir(d); return;
                    }
                    fclose(f);
                }
            }
            closedir(d);
        }
    }
    snprintf(out, n, "AC");
#endif
}

static void read_clock(char *out, size_t n) {
    time_t t = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(out, n, "%H:%M", &tmv);
}

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
    } else {
        buf[0] = '?'; buf[1] = 0;
    }
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

static void launch_and_wait(const std::string &cmd) {
#ifdef _WIN32
    system(cmd.c_str());
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
        _exit(127);
    } else if (pid > 0) {
        int st;
        waitpid(pid, &st, 0);
    }
#endif
}

static void run_cmd_lines(const std::string &cmd, std::vector<std::string> &out) {
    FILE *p = POPEN((cmd + " 2>&1").c_str(), "r");
    if (!p) { out.push_back("popen failed"); return; }
    char buf[1024];
    while (fgets(buf, sizeof buf, p)) {
        std::string s = buf;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        out.push_back(s);
    }
    PCLOSE(p);
}

// ----------------------------- экранная клавиатура -----------------------------
static const char *KB_PAGE0[4] = {
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm,.?",
    "1234567890",
};
static const char *KB_PAGE1[4] = {
    "!@#$%^&*()",
    "[]{}<>;:'\"",
    "-_=+\\|/~`",
    "1234567890",
};
static const char *KB_FUNC[6] = { "space", "shift", "del", "enter", "abc", "exit" };

struct Kbd {
    int row = 0, col = 0;
    int page = 0;
    bool shift = false;
    const char *cell_char() const {
        if (row < 4) {
            const char *r = page == 0 ? KB_PAGE0[row] : KB_PAGE1[row];
            if (col < 0 || col >= (int)strlen(r)) return nullptr;
            static char b[2];
            char ch = r[col];
            if (row < 3 && page == 0 && shift && ch >= 'a' && ch <= 'z') b[0] = (char)(ch - 32);
            else b[0] = ch;
            b[1] = 0;
            return b;
        }
        if (col < 0 || col >= 6) return nullptr;
        return KB_FUNC[col];
    }
    void clamp() {
        if (row < 0) row = 0;
        if (row > 4) row = 4;
        if (row < 4) {
            const char *r = page == 0 ? KB_PAGE0[row] : KB_PAGE1[row];
            int n = (int)strlen(r);
            if (col >= n) col = n - 1;
        } else {
            if (col >= 6) col = 5;
        }
        if (col < 0) col = 0;
    }
    void move(int dr, int dc) { row += dr; col += dc; clamp(); }
};

static void draw_keyboard(Renderer &R, const Kbd &kb, int y0) {
    fillrect(R, 0, y0, SCREEN_W, SCREEN_H - y0, rgb(24, 24, 34));
    int kw = SCREEN_W / 10;
    int kh = 34;
    for (int r = 0; r < 4; ++r) {
        const char *line = kb.page == 0 ? KB_PAGE0[r] : KB_PAGE1[r];
        int n = (int)strlen(line);
        for (int c = 0; c < n; ++c) {
            int x = c * kw;
            int y = y0 + 6 + r * (kh + 6);
            char ch = line[c];
            bool sel = kb.row == r && kb.col == c;
            Uint32 col = sel ? rgb(255, 220, 60) : rgb(60, 60, 80);
            fillrect(R, x + 2, y, kw - 4, kh - 4, col);
            char b[2] = { ch, 0 };
            if (r < 3 && kb.page == 0 && kb.shift && ch >= 'a' && ch <= 'z') b[0] = (char)(ch - 32);
            drawtext(R, b, x + (kw - textw(b, 2)) / 2, y + (kh - 14) / 2, 2, sel ? rgb(0,0,0) : rgb(230,230,230));
        }
    }
    int fx[6] = { 4, 154, 244, 364, 484, 564 };
    for (int c = 0; c < 6; ++c) {
        int x = fx[c];
        int y = y0 + 6 + 4 * (kh + 6);
        bool sel = kb.row == 4 && kb.col == c;
        Uint32 col = sel ? rgb(255, 220, 60) : rgb(80, 80, 100);
        fillrect(R, x, y, 128, kh, col);
        drawtext(R, KB_FUNC[c], x + (128 - textw(KB_FUNC[c], 2)) / 2, y + (kh - 14) / 2, 2, sel ? rgb(0,0,0) : rgb(230,230,230));
    }
}

// ----------------------------- терминал -----------------------------
struct TermState {
    std::vector<std::string> lines;
    std::string input;
    Kbd kb;
    std::thread *worker = nullptr;
    std::vector<std::string> pending;
    std::mutex mtx;
    bool running = false;
    const char *prompt = "$ ";

    void add_line(const std::string &s) {
        if (lines.size() > 300) lines.erase(lines.begin());
        lines.push_back(s);
    }
    void start_cmd(const std::string &cmd) {
        if (running) return;
        add_line(std::string(prompt) + cmd);
        input.clear();
        std::string c = cmd;
        running = true;
        worker = new std::thread([this, c]() {
            FILE *p = POPEN((c + " 2>&1").c_str(), "r");
            if (!p) { std::lock_guard<std::mutex> l(mtx); pending.push_back("popen failed"); }
            else {
                char buf[1024];
                while (fgets(buf, sizeof buf, p)) {
                    std::string s = buf;
                    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                    std::lock_guard<std::mutex> l(mtx);
                    pending.push_back(s);
                }
                PCLOSE(p);
            }
            running = false;
        });
    }
    void pump() {
        if (worker) {
            std::vector<std::string> got;
            { std::lock_guard<std::mutex> l(mtx); got.swap(pending); }
            for (auto &s : got) add_line(s);
            if (!running) { worker->join(); delete worker; worker = nullptr; }
        }
    }
};

// ----------------------------- файлы -----------------------------
struct FileState {
    std::string cwd = "/";
    std::vector<std::pair<std::string, bool>> entries;
    int sel = 0;
    bool viewing = false;
    std::string viewname;
    std::vector<std::string> view;
    int vscroll = 0;
    std::string msg;

    void refresh() {
        entries.clear();
        sel = 0;
#ifdef _WIN32
        cwd = std::string(GETCWD(nullptr, 0));
#endif
        DIR *d = opendir(cwd.c_str());
        if (!d) { msg = "cannot open dir"; return; }
        std::vector<std::pair<std::string, bool>> tmp;
        struct dirent *e;
        while ((e = readdir(d))) {
            std::string name = e->d_name;
            if (name == ".") continue;
            bool isd = false;
#ifndef _WIN32
            if (e->d_type == DT_DIR) isd = true;
            else if (e->d_type == DT_UNKNOWN) {
                struct stat st;
                std::string full = cwd + (cwd == "/" ? "" : "/") + name;
                if (stat(full.c_str(), &st) == 0) isd = ISDIR(st);
            }
#endif
            tmp.push_back({name, isd});
        }
        closedir(d);
        std::sort(tmp.begin(), tmp.end(), [](const auto &a, const auto &b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        entries = tmp;
        msg.clear();
    }
    void enter() {
        if (sel < 0 || sel >= (int)entries.size()) return;
        auto &e = entries[sel];
        if (e.second) {
            cwd = (cwd == "/" ? "" : cwd) + "/" + e.first;
            refresh();
        } else {
            open_preview(e.first);
        }
    }
    void up() {
        if (viewing) { viewing = false; return; }
        if (cwd == "/") return;
        size_t p = cwd.rfind('/');
        if (p == std::string::npos) cwd = "/";
        else if (p == 0) cwd = "/";
        else cwd = cwd.substr(0, p);
        refresh();
    }
    void open_preview(const std::string &fname) {
        std::string full = cwd + (cwd == "/" ? "" : "/") + fname;
        FILE *f = fopen(full.c_str(), "rb");
        if (!f) { msg = "cannot open file"; return; }
        char buf[32768];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        view.clear();
        bool binary = n > 0 && memchr(buf, 0, n) != nullptr;
        if (binary) {
            view.push_back("(binary file, " + std::to_string((int)n) + " bytes)");
        } else {
            std::string acc;
            for (size_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') { view.push_back(acc); acc.clear(); }
                else acc += buf[i];
            }
            if (!acc.empty()) view.push_back(acc);
        }
        viewname = fname;
        vscroll = 0;
        viewing = true;
    }
};

// ----------------------------- процессы -----------------------------
struct ProcState {
    std::vector<std::pair<int, std::string>> procs;
    int sel = 0;
    int confirm = -1;
    std::string msg;

    void refresh() {
        procs.clear();
        sel = 0;
#ifdef _WIN32
        procs.push_back({0, "no /proc on windows"});
        return;
#else
        DIR *d = opendir("/proc");
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos) continue;
            int pid = atoi(n.c_str());
            std::string comm = "?";
            char p[256];
            snprintf(p, sizeof p, "/proc/%d/comm", pid);
            FILE *f = fopen(p, "r");
            if (f) {
                char b[128];
                if (fgets(b, sizeof b, f)) {
                    for (char *q = b; *q; ++q) if (*q == '\n' || *q == '\r') { *q = 0; break; }
                    comm = b;
                }
                fclose(f);
            }
            procs.push_back({pid, comm});
        }
        closedir(d);
        std::sort(procs.begin(), procs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
#endif
    }
    void do_kill() {
        if (confirm <= 0) return;
#ifdef _WIN32
        msg = "no kill on windows";
#else
        std::vector<std::string> out;
        run_cmd_lines("kill -9 " + std::to_string(confirm) + " 2>&1", out);
        msg = out.empty() ? "sent" : out[0];
#endif
        confirm = -1;
        refresh();
    }
};

// ----------------------------- калькулятор -----------------------------
struct CalcState {
    double acc = 0, cur = 0;
    int op = 0;
    bool fresh = true;
    std::string disp = "0";
    int brow = 0, bcol = 0;

    void digit(int d) {
        if (fresh) { cur = 0; fresh = false; }
        cur = cur * 10 + d;
        update();
    }
    void apply_op(int newop) {
        if (!fresh) { compute(); fresh = true; }
        op = newop;
        update();
    }
    void compute() {
        switch (op) {
            case 1: acc = acc + cur; break;
            case 2: acc = acc - cur; break;
            case 3: acc = acc * cur; break;
            case 4: acc = (cur != 0) ? acc / cur : 0; break;
            default: acc = cur; break;
        }
        op = 0;
    }
    void equals() {
        if (!fresh) { compute(); fresh = true; }
        update();
    }
    void clear() { acc = 0; cur = 0; op = 0; fresh = true; disp = "0"; }
    void update() {
        char b[64];
        snprintf(b, sizeof b, "%.6g", fresh ? acc : cur);
        disp = b;
    }
};

static std::vector<std::string> sysinfo_lines() {
    std::vector<std::string> out;
#ifdef _WIN32
    out.push_back("Platform: Windows (PC test)");
    return out;
#else
    std::vector<std::string> t;
    run_cmd_lines("uname -a", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("cat /etc/os-release 2>/dev/null | head -2", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("uptime", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("free -h 2>/dev/null", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("df -h / 2>/dev/null", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("grep -m1 'model name' /proc/cpuinfo 2>/dev/null", t);
    for (auto &s : t) out.push_back(s);
    t.clear();
    run_cmd_lines("cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null", t);
    if (!t.empty()) {
        int mC = atoi(t[0].c_str());
        char b[64];
        snprintf(b, sizeof b, "CPU temp: %d.%02d C", mC / 1000, (mC % 1000) / 10);
        out.push_back(b);
    }
    return out;
#endif
}

// ----------------------------- рендер экранов -----------------------------
static void render_desktop(Renderer &R, const std::vector<App> &apps, int sel, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    for (int y = STATUS_H; y < SCREEN_H; ++y) {
        Uint8 shade = (Uint8)(18 + (y * 20) / SCREEN_H);
        Uint32 c = rgb(shade, shade, shade + 6);
        for (int x = 0; x < SCREEN_W; ++x) R.pix[y * (R.pitch / 4) + x] = c;
    }
    draw_statusbar(R, clock, batt, "r36pda");
    int cols = (SCREEN_W - 2 * MARGIN) / ICON_SLOT;
    if (cols < 1) cols = 1;
    for (size_t i = 0; i < apps.size(); ++i) {
        int r = (int)(i / cols);
        int c = (int)(i % cols);
        int x = MARGIN + c * ICON_SLOT + (ICON_SLOT - 96) / 2;
        int y = STATUS_H + MARGIN / 2 + r * (ICON_SLOT + LABEL_H);
        if (y + ICON_SLOT > SCREEN_H - 8) break;
        drawicon(R, apps[i], x, y, 96, (int)i == sel);
        drawlabel(R, apps[i].name, MARGIN + c * ICON_SLOT, y + 96 + 2, ICON_SLOT, 2, (int)i == sel);
    }
    const char *h = "A: run  arrows: move  Fn+Start: quit";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 130));
}

static void render_term(Renderer &R, TermState &T, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Terminal");
    int line_h = 16;
    int area_h = 4 * 40 + 34;
    int term_bottom = SCREEN_H - area_h - 22;
    int maxlines = (term_bottom - STATUS_H) / line_h;
    size_t start = T.lines.size() > (size_t)maxlines ? T.lines.size() - (size_t)maxlines : 0;
    for (size_t i = start; i < T.lines.size(); ++i) {
        std::string s = T.lines[i];
        if ((int)s.size() > 50) s = s.substr(0, 50);
        drawtext(R, s.c_str(), 8, STATUS_H + 4 + (int)(i - start) * line_h, 2, rgb(220, 220, 220));
    }
    if (T.running) drawtext(R, "... running", 8, term_bottom - 16, 2, rgb(255, 200, 80));
    std::string full = std::string("$ ") + T.input;
    if ((int)full.size() > 50) full = full.substr(full.size() - 50);
    int cw = textw(full.c_str(), 2);
    drawtext(R, full.c_str(), 8, term_bottom - 16, 2, rgb(255, 255, 255));
    fillrect(R, 8 + cw, term_bottom - 16, 2, 14, rgb(255, 255, 255));
    draw_keyboard(R, T.kb, SCREEN_H - area_h);
}

static void render_files(Renderer &R, FileState &F, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Files");
    drawtext(R, F.cwd.c_str(), 8, STATUS_H + 4, 2, rgb(255, 220, 60));
    if (!F.msg.empty()) drawtext(R, F.msg.c_str(), 8, SCREEN_H - 12, 2, rgb(255, 120, 120));
    int line_h = 16;
    int y = STATUS_H + 24;
    int maxlines = (SCREEN_H - y - 16) / line_h;
    if (F.viewing) {
        drawtext(R, F.viewname.c_str(), 8, y - 16, 2, rgb(120, 220, 120));
        if (F.vscroll > (int)F.view.size()) F.vscroll = (int)F.view.size();
        for (size_t i = (size_t)F.vscroll; i < F.view.size() && (int)(i - F.vscroll) < maxlines; ++i) {
            std::string s = F.view[i];
            if ((int)s.size() > 50) s = s.substr(0, 50);
            drawtext(R, s.c_str(), 8, y + (int)(i - F.vscroll) * line_h, 2, rgb(220, 220, 220));
        }
        const char *h = "A/B: up  Select: back";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
        return;
    }
    int start = F.sel - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)F.entries.size() && i < start + maxlines; ++i) {
        bool sel = (i == F.sel);
        std::string s = F.entries[i].second ? "[d] " : "    ";
        s += F.entries[i].first;
        if ((int)s.size() > 50) s = s.substr(0, 50);
        drawtext(R, s.c_str(), 8, y + (i - start) * line_h, 2, sel ? rgb(255, 220, 60) : rgb(220, 220, 220));
    }
    const char *h = "A: open  B: up";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
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
        if ((int)s.size() > 55) s = s.substr(0, 55);
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
        fillrect(R, 80, 200, 480, 60, rgb(60, 20, 20));
        char b[64];
        snprintf(b, sizeof b, "Kill PID %d?", P.confirm);
        drawtext(R, b, 100, 214, 2, rgb(255, 255, 255));
        drawtext(R, "A: yes  B: no", 100, 236, 2, rgb(255, 200, 80));
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
    drawtext(R, d.c_str(), SCREEN_W - 8 - textw(d.c_str(), 3), STATUS_H + 18, 3, rgb(255, 255, 255));
    int kw = 150, kh = 60;
    int x0 = (SCREEN_W - 4 * kw) / 2;
    int y0 = STATUS_H + 60;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            int x = x0 + c * kw;
            int y = y0 + r * kh;
            bool sel = (C.brow == r && C.bcol == c);
            char ch = CALC_GRID[r][c];
            Uint32 col = sel ? rgb(255, 220, 60) : (ch >= '0' && ch <= '9' ? rgb(60, 60, 80) : rgb(80, 80, 100));
            fillrect(R, x, y, kw - 4, kh - 4, col);
            char b[2] = { ch, 0 };
            drawtext(R, b, x + (kw - textw(b, 3)) / 2, y + (kh - 21) / 2, 3, sel ? rgb(0,0,0) : rgb(230,230,230));
        }
    }
    const char *h = "B: back";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 130));
}

// ----------------------------- ввод -----------------------------
static void open_app(Renderer &R, Mode &mode, App &a, TermState &term,
                     FileState &files, ProcState &procs,
                     std::vector<std::string> &sysinfo, bool &sysinfo_loaded) {
    if (a.mode == M_EXTERNAL) {
        SDL_HideWindow(R.win);
        launch_and_wait(a.command);
        SDL_ShowWindow(R.win);
        SDL_RaiseWindow(R.win);
        return;
    }
    mode = a.mode;
    if (mode == M_TERM) { term.kb = Kbd(); term.input.clear(); }
    if (mode == M_FILES) files.refresh();
    if (mode == M_PROC) procs.refresh();
    if (mode == M_SYSINFO && !sysinfo_loaded) { sysinfo = sysinfo_lines(); sysinfo_loaded = true; }
}

static void calc_press(CalcState &C, char ch) {
    if (ch >= '0' && ch <= '9') C.digit(ch - '0');
    else if (ch == 'C') C.clear();
    else if (ch == '=') C.equals();
    else if (ch == '+') C.apply_op(1);
    else if (ch == '-') C.apply_op(2);
    else if (ch == '*') C.apply_op(3);
    else if (ch == '/') C.apply_op(4);
}

// ----------------------------- главный цикл -----------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Renderer R;
    Uint32 flags = 0;
    const char *fs = getenv("R36PDA_FULLSCREEN");
    if (fs && fs[0] == '1') flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    R.win = SDL_CreateWindow("r36pda", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W, SCREEN_H, flags);
    if (!R.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R.ren) R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_SOFTWARE);
    if (!R.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    R.fb = SDL_CreateTexture(R.ren, SDL_PIXELFORMAT_RGB888,
                             SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!R.fb) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 1; }

    if (SDL_NumJoysticks() > 0) R.joy = SDL_JoystickOpen(0);

    std::vector<App> apps = builtin_apps();
    std::vector<App> ext = load_external_apps("config/apps.cfg");
    if (ext.empty()) ext = load_external_apps("apps.cfg");
    for (auto &a : ext) apps.push_back(a);

    Mode mode = M_DESKTOP;
    int sel = 0;
    bool running = true;
    bool fn_pressed = false;

    char clockbuf[16], battbuf[16];
    read_clock(clockbuf, sizeof clockbuf);
    read_battery(battbuf, sizeof battbuf);

    TermState term;
    FileState files;
    ProcState procs;
    CalcState calc;
    std::vector<std::string> sysinfo;
    int sysinfo_scroll = 0;
    bool sysinfo_loaded = false;

    SDL_StartTextInput();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = false; break;

            case SDL_KEYDOWN: {
                if (ev.key.repeat) break;
                int sym = ev.key.keysym.sym;
                switch (mode) {
                case M_DESKTOP:
                    if (sym == SDLK_UP || sym == SDLK_w) sel = std::max(0, sel - 4);
                    else if (sym == SDLK_DOWN || sym == SDLK_s) sel += 4;
                    else if (sym == SDLK_LEFT || sym == SDLK_a) sel = std::max(0, sel - 1);
                    else if (sym == SDLK_RIGHT || sym == SDLK_d) sel += 1;
                    else if (sym == SDLK_RETURN || sym == SDLK_SPACE || sym == SDLK_KP_ENTER) {
                        if (!apps.empty()) open_app(R, mode, apps[sel % (int)apps.size()],
                                                    term, files, procs, sysinfo, sysinfo_loaded);
                    }
                    else if (sym == SDLK_ESCAPE) running = false;
                    else if (sym == SDLK_F1) {
                        SDL_HideWindow(R.win);
                        launch_and_wait("pkill emulationstation; emulationstation");
                        SDL_ShowWindow(R.win);
                    }
                    break;
                case M_TERM:
                    if (sym == SDLK_ESCAPE) mode = M_DESKTOP;
                    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) term.start_cmd(term.input);
                    else if (sym == SDLK_BACKSPACE && !term.input.empty()) term.input.pop_back();
                    else if (sym == SDLK_UP) term.kb.move(-1, 0);
                    else if (sym == SDLK_DOWN) term.kb.move(1, 0);
                    else if (sym == SDLK_LEFT) term.kb.move(0, -1);
                    else if (sym == SDLK_RIGHT) term.kb.move(0, 1);
                    else if (sym == SDLK_TAB) term.kb.page = 1 - term.kb.page;
                    break;
                case M_FILES:
                    if (sym == SDLK_UP) { if (files.viewing) files.vscroll = std::max(0, files.vscroll - 1); else files.sel = std::max(0, files.sel - 1); }
                    else if (sym == SDLK_DOWN) { if (files.viewing) files.vscroll = std::min((int)files.view.size(), files.vscroll + 1); else files.sel = std::min((int)files.entries.size() - 1, files.sel + 1); }
                    else if (sym == SDLK_LEFT) { if (files.viewing) files.vscroll = std::max(0, files.vscroll - 10); else files.sel = std::max(0, files.sel - 1); }
                    else if (sym == SDLK_RIGHT) { if (files.viewing) files.vscroll = std::min((int)files.view.size(), files.vscroll + 10); else files.sel = std::min((int)files.entries.size() - 1, files.sel + 1); }
                    else if (sym == SDLK_RETURN || sym == SDLK_SPACE) files.enter();
                    else if (sym == SDLK_BACKSPACE || sym == SDLK_ESCAPE) files.up();
                    break;
                case M_SYSINFO:
                    if (sym == SDLK_UP) sysinfo_scroll = std::max(0, sysinfo_scroll - 1);
                    else if (sym == SDLK_DOWN) sysinfo_scroll = std::min((int)sysinfo.size() - 1, sysinfo_scroll + 1);
                    else if (sym == SDLK_BACKSPACE || sym == SDLK_ESCAPE) mode = M_DESKTOP;
                    break;
                case M_PROC:
                    if (sym == SDLK_UP) procs.sel = std::max(0, procs.sel - 1);
                    else if (sym == SDLK_DOWN) procs.sel = std::min((int)procs.procs.size() - 1, procs.sel + 1);
                    else if (sym == SDLK_RETURN && procs.confirm > 0) procs.do_kill();
                    else if (sym == SDLK_BACKSPACE || sym == SDLK_ESCAPE) {
                        if (procs.confirm > 0) procs.confirm = -1;
                        else mode = M_DESKTOP;
                    }
                    break;
                case M_CALC:
                    if (sym == SDLK_UP) calc.brow = std::max(0, calc.brow - 1);
                    else if (sym == SDLK_DOWN) calc.brow = std::min(3, calc.brow + 1);
                    else if (sym == SDLK_LEFT) calc.bcol = std::max(0, calc.bcol - 1);
                    else if (sym == SDLK_RIGHT) calc.bcol = std::min(3, calc.bcol + 1);
                    else if (sym == SDLK_RETURN || sym == SDLK_SPACE) calc_press(calc, CALC_GRID[calc.brow][calc.bcol]);
                    else if (sym == SDLK_BACKSPACE || sym == SDLK_ESCAPE) mode = M_DESKTOP;
                    break;
                case M_EXTERNAL: break;
                }
                break;
            }

            case SDL_TEXTINPUT: {
                if (mode == M_TERM) {
                    std::string t = ev.text.text;
                    for (size_t i = 0; i < t.size(); ++i) {
                        if ((unsigned char)t[i] < 32) continue;
                        if ((unsigned char)t[i] == 127) continue;
                        term.input += t[i];
                    }
                }
                break;
            }

            case SDL_JOYBUTTONDOWN: {
                int b = ev.jbutton.button;
                if (b == 16) { fn_pressed = true; break; }
                if (b == 13 && fn_pressed) { running = false; fn_pressed = false; break; }
                if (fn_pressed) {
                    if (b == 0 || b == 1) { if (mode != M_DESKTOP) mode = M_DESKTOP; }
                    fn_pressed = false;
                    break;
                }
                switch (mode) {
                case M_DESKTOP:
                    if (b == 8) sel = std::max(0, sel - 4);
                    else if (b == 9) sel += 4;
                    else if (b == 10) sel = std::max(0, sel - 1);
                    else if (b == 11) sel += 1;
                    else if (b == 0 || b == 1 || b == 13) {
                        if (!apps.empty()) open_app(R, mode, apps[sel % (int)apps.size()],
                                                    term, files, procs, sysinfo, sysinfo_loaded);
                    }
                    break;
                case M_TERM:
                    if (b == 8) term.kb.move(-1, 0);
                    else if (b == 9) term.kb.move(1, 0);
                    else if (b == 10) term.kb.move(0, -1);
                    else if (b == 11) term.kb.move(0, 1);
                    else if (b == 0 || b == 1) {
                        const char *k = term.kb.cell_char();
                        if (!k) break;
                        std::string key = k;
                        if (key == "space") term.input += ' ';
                        else if (key == "del") { if (!term.input.empty()) term.input.pop_back(); }
                        else if (key == "enter") term.start_cmd(term.input);
                        else if (key == "abc") term.kb.page = 1 - term.kb.page;
                        else if (key == "shift") term.kb.shift = !term.kb.shift;
                        else if (key == "exit") mode = M_DESKTOP;
                        else term.input += key;
                    }
                    else if (b == 12) { if (!term.input.empty()) term.input.pop_back(); }
                    else if (b == 13) term.start_cmd(term.input);
                    break;
                case M_FILES:
                    if (b == 8) { if (files.viewing) files.vscroll = std::max(0, files.vscroll - 1); else files.sel = std::max(0, files.sel - 1); }
                    else if (b == 9) { if (files.viewing) files.vscroll = std::min((int)files.view.size(), files.vscroll + 1); else files.sel = std::min((int)files.entries.size() - 1, files.sel + 1); }
                    else if (b == 10) { if (files.viewing) files.vscroll = std::max(0, files.vscroll - 10); else files.sel = std::max(0, files.sel - 1); }
                    else if (b == 11) { if (files.viewing) files.vscroll = std::min((int)files.view.size(), files.vscroll + 10); else files.sel = std::min((int)files.entries.size() - 1, files.sel + 1); }
                    else if (b == 0 || b == 1) files.enter();
                    else if (b == 12 || b == 13) files.up();
                    break;
                case M_SYSINFO:
                    if (b == 8) sysinfo_scroll = std::max(0, sysinfo_scroll - 1);
                    else if (b == 9) sysinfo_scroll = std::min((int)sysinfo.size() - 1, sysinfo_scroll + 1);
                    else if (b == 12 || b == 13 || b == 0 || b == 1) mode = M_DESKTOP;
                    break;
                case M_PROC:
                    if (b == 8) procs.sel = std::max(0, procs.sel - 1);
                    else if (b == 9) procs.sel = std::min((int)procs.procs.size() - 1, procs.sel + 1);
                    else if (b == 13) { if (procs.sel >= 0 && procs.sel < (int)procs.procs.size()) procs.confirm = procs.procs[procs.sel].first; }
                    else if (b == 1 && procs.confirm > 0) procs.do_kill();
                    else if (b == 0 || b == 12 || b == 13) {
                        if (procs.confirm > 0) procs.confirm = -1;
                        else mode = M_DESKTOP;
                    }
                    break;
                case M_CALC:
                    if (b == 8) calc.brow = std::max(0, calc.brow - 1);
                    else if (b == 9) calc.brow = std::min(3, calc.brow + 1);
                    else if (b == 10) calc.bcol = std::max(0, calc.bcol - 1);
                    else if (b == 11) calc.bcol = std::min(3, calc.bcol + 1);
                    else if (b == 0 || b == 1) calc_press(calc, CALC_GRID[calc.brow][calc.bcol]);
                    else if (b == 12 || b == 13) mode = M_DESKTOP;
                    break;
                case M_EXTERNAL: break;
                }
                break;
            }

            case SDL_JOYBUTTONUP: {
                if (ev.jbutton.button == 16) fn_pressed = false;
                break;
            }
            case SDL_JOYHATMOTION: {
                if (mode == M_DESKTOP) {
                    if (ev.jhat.value & SDL_HAT_UP) sel = std::max(0, sel - 4);
                    if (ev.jhat.value & SDL_HAT_DOWN) sel += 4;
                    if (ev.jhat.value & SDL_HAT_LEFT) sel = std::max(0, sel - 1);
                    if (ev.jhat.value & SDL_HAT_RIGHT) sel += 1;
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

        if (SDL_LockTexture(R.fb, nullptr, (void **)&R.pix, &R.pitch) != 0) {
            fprintf(stderr, "SDL_LockTexture: %s\n", SDL_GetError());
            break;
        }

        switch (mode) {
        case M_DESKTOP: render_desktop(R, apps, sel, clockbuf, battbuf); break;
        case M_TERM:    render_term(R, term, clockbuf, battbuf); break;
        case M_FILES:   render_files(R, files, clockbuf, battbuf); break;
        case M_SYSINFO: render_sysinfo(R, sysinfo, sysinfo_scroll, clockbuf, battbuf); break;
        case M_PROC:    render_proc(R, procs, clockbuf, battbuf); break;
        case M_CALC:    render_calc(R, calc, clockbuf, battbuf); break;
        case M_EXTERNAL: render_desktop(R, apps, sel, clockbuf, battbuf); break;
        }

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

    if (R.joy) SDL_JoystickClose(R.joy);
    SDL_DestroyTexture(R.fb);
    SDL_DestroyRenderer(R.ren);
    SDL_DestroyWindow(R.win);
    SDL_Quit();
    return 0;
}