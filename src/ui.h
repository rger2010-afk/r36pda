// ui.h — базовый рендер и элементы интерфейса r36pda.
#pragma once
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
#define STATUS_H 24

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

// 5x7 шрифт, масштаб s
static void drawtext(Renderer &R, const char *str, int x, int y, int s, Uint32 c) {
    const unsigned char *p = (const unsigned char *)str;
    int cx = x;
    while (*p) {
        int len = 0;
        int cp = utf8_decode(p, &len);
        const Glyph *g = font_glyph(cp);
        if (g) {
            for (int r = 0; r < 7; ++r)
                for (int b = 0; b < 5; ++b)
                    if ((g->r[r] >> (4 - b)) & 1)
                        for (int dy = 0; dy < s; ++dy)
                            for (int dx = 0; dx < s; ++dx)
                                putpx(R, cx + b * s + dx, y + r * s + dy, c);
            cx += 5 * s + s;
        } else cx += 5 * s + s;
        p += len;
    }
}

static int textw(const char *str, int s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) { int l = 0; utf8_decode(p, &l); w += 5 * s + s; p += l; }
    return w;
}

static void draw_statusbar(Renderer &R, const char *clock, const char *batt, const char *title) {
    fillrect(R, 0, 0, SCREEN_W, STATUS_H, rgb(26, 26, 42));
    drawtext(R, clock, 6, (STATUS_H - 14) / 2, 2, rgb(255, 255, 255));
    if (title) {
        int tw = textw(title, 2);
        drawtext(R, title, (SCREEN_W - tw) / 2, (STATUS_H - 14) / 2, 2, rgb(200, 200, 215));
    }
    std::string bt = std::string("BAT ") + batt;
    int bw = textw(bt.c_str(), 2);
    drawtext(R, bt.c_str(), SCREEN_W - 46 - bw, (STATUS_H - 14) / 2, 2, rgb(120, 255, 120));
    fillrect(R, 0, STATUS_H - 1, SCREEN_W, 1, rgb(70, 70, 100));
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

static std::string path_join(const std::string &a, const std::string &b) {
    if (a.empty() || a == "/") return a + b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}