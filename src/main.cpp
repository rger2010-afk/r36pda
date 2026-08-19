// r36pda — PDA-десктоп для R36S (ArkOS) и для теста на ПК.
// C++17 + SDL2. Без внешних библиотек: встроенный шрифт font.h.
//
// Сборка на консоли (ArkOS):
//   sudo apt-get install --reinstall g++ libsdl2-dev
//   make            # см. Makefile
//   ./r36pda
//
// Сборка на ПК (Windows + MinGW + SDL2): см. Makefile.win

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include "font.h"

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
  #include <direct.h>
  #define CHDIR _chdir
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <dirent.h>
  #define CHDIR chdir
#endif

// Разрешение R36S
#define SCREEN_W 640
#define SCREEN_H 480

#define STATUS_H 28
#define MARGIN 24
#define ICON_SLOT 152
#define LABEL_H 26

struct App {
    std::string name;
    std::string command;   // что запускать (через /bin/sh -c или cmd /c)
    Uint8 color[3];
};

// ----------------------------- конфиг -----------------------------
// Формат файла: имя | команда | R,G,B  (по одной записи на строку, # — комментарий)
static std::vector<App> load_apps(const std::string &path) {
    std::vector<App> out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "нет конфига %s — использую встроенные\n", path.c_str()); return out; }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        std::string s = line;
        // убрать перевод строки
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        // split по '|'
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
        if (!a.name.empty() && !a.command.empty()) out.push_back(a);
    }
    fclose(f);
    return out;
}

static std::vector<App> default_apps() {
    std::vector<App> out;
    App a;
    a.name = "Terminal";   a.command = "xterm || st || bash"; a.color[0]=80; a.color[1]=200; a.color[2]=120;
    out.push_back(a);
    a.name = "Games";      a.command = "bash -c 'pkill emulationstation; emulationstation'"; a.color[0]=240; a.color[1]=150; a.color[2]=60;
    out.push_back(a);
    a.name = "Arduino";    a.command = "bash -c 'arduino-cli --help | less'"; a.color[0]=200; a.color[1]=80; a.color[2]=80;
    out.push_back(a);
    a.name = "Files";      a.command = "bash -c 'ls -la'"; a.color[0]=120; a.color[1]=120; a.color[2]=220;
    out.push_back(a);
    return out;
}

// ----------------------------- запуск -----------------------------
// Запускает команду и ждёт завершения. Лаунчер уходит в фон (SDL_HIDE).
static void launch_and_wait(const std::string &cmd) {
#ifdef _WIN32
    // показать окно консоли для команды
    system(cmd.c_str());
#else
    pid_t pid = fork();
    if (pid == 0) {
        // ребёнок: запустить sh -c, отдав ему управление терминалом
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
        _exit(127);
    } else if (pid > 0) {
        int st;
        waitpid(pid, &st, 0);
    }
#endif
}

// ----------------------------- рендер -----------------------------
struct Renderer {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *fb = nullptr;      // текстурный буфер 640x480 (пиксели)
    Uint32 *pix = nullptr;
    int pitch = 0;
    SDL_Joystick *joy = nullptr;
};

// вписать пиксель (x,y) цвета c в буфер
static void putpx(Renderer &R, int x, int y, Uint32 c) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    R.pix[y * (R.pitch / 4) + x] = c;
}

static void fillrect(Renderer &R, int x, int y, int w, int h, Uint32 c) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            putpx(R, xx, yy, c);
}

// нарисовать строку шрифтом 5x7 с масштабом s, цвет c, позиция (x,y) — левый верх
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
                    if ((g->r[r] >> b) & 1) {
                        for (int dy = 0; dy < s; ++dy)
                            for (int dx = 0; dx < s; ++dx)
                                putpx(R, cx + b * s + dx, y + r * s + dy, c);
                    }
                }
            }
            cx += 5 * s + s;   // символ + 1 пиксель пробела (в масштабе)
        } else {
            cx += 5 * s + s;
        }
        p += len;
    }
}

// ширина строки при данном масштабе
static int textw(const char *str, int s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        int len = 0;
        int cp = utf8_decode(p, &len);
        if (font_glyph(cp)) w += 5 * s + s;
        else w += 5 * s + s;
        p += len;
    }
    return w;
}

static Uint32 rgb(Uint8 r, Uint8 g, Uint8 b) {
    return ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

// рисование «иконки» — цветной квадрат с первой буквой имени
static void drawicon(Renderer &R, const App &a, int x, int y, int sz, bool sel) {
    // фон иконки
    Uint32 base = rgb(a.color[0], a.color[1], a.color[2]);
    if (sel) base = rgb(255, 255, 255);
    fillrect(R, x, y, sz, sz, base);
    // рамка
    Uint32 border = sel ? rgb(255, 220, 60) : rgb(40, 40, 40);
    for (int i = 0; i < sz; ++i) {
        putpx(R, x, y + i, border);
        putpx(R, x + sz - 1, y + i, border);
        putpx(R, x + i, y, border);
        putpx(R, x + i, y + sz - 1, border);
    }
    // буква внутри
    char buf[8];
    int len = 0;
    int cp = utf8_decode((const unsigned char *)a.name.c_str(), &len);
    if (cp > 0) {
        // скопировать первый utf8-символ
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

// подпись под иконкой (перенос не делаем, режем)
static void drawlabel(Renderer &R, const std::string &name, int x, int y, int w, int s, bool sel) {
    Uint32 c = sel ? rgb(255, 220, 60) : rgb(230, 230, 230);
    std::string t = name;
    int avail = w / (5 * s + s);
    if ((int)t.size() > avail) t = t.substr(0, avail);
    drawtext(R, t.c_str(), x, y, s, c);
}

// ----------------------------- статус-бар -----------------------------
static void draw_statusbar(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, STATUS_H, rgb(30, 30, 45));
    // часы
    drawtext(R, clock, 8, (STATUS_H - 7 * 2) / 2, 2, rgb(255, 255, 255));
    // батарея справа
    std::string bt = std::string("BAT ") + batt;
    int bw = textw(bt.c_str(), 2);
    drawtext(R, bt.c_str(), SCREEN_W - 8 - bw, (STATUS_H - 7 * 2) / 2, 2, rgb(120, 255, 120));
    // полоска под статус-баром
    fillrect(R, 0, STATUS_H - 2, SCREEN_W, 2, rgb(60, 60, 90));
}

static void read_battery(char *out, size_t n) {
#ifdef _WIN32
    snprintf(out, n, "--");
    (void)0;
#else
    // попробовать /sys/class/power_supply/*/capacity
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
                        // убрать перевод строки
                        for (char *q = buf; *q; ++q) if (*q == '\n' || *q == '\r') { *q = 0; break; }
                        snprintf(out, n, "%s%%", buf);
                        fclose(f);
                        closedir(d);
                        return;
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

// ----------------------------- главное -----------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Renderer R;

    // окно: на R36S — полноэкранный framebuffer, на ПК — обычное окно
    Uint32 flags = 0;
    const char *fs = getenv("R36PDA_FULLSCREEN");
    if (fs && fs[0] == '1') flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    R.win = SDL_CreateWindow("r36pda", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W, SCREEN_H, flags);
    if (!R.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R.ren) R.ren = SDL_CreateRenderer(R.win, -1, SDL_RENDERER_SOFTWARE);
    if (!R.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    // пиксельный буфер 640x480
    R.fb = SDL_CreateTexture(R.ren, SDL_PIXELFORMAT_RGB888,
                             SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!R.fb) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 1; }

    // джойстик (на R36S это встроенный геймпад; на ПК может не быть — ок)
    if (SDL_NumJoysticks() > 0) {
        R.joy = SDL_JoystickOpen(0);
    }

    // конфиг: ищем в нескольких местах, затем встроенный
    std::vector<App> apps;
    apps = load_apps("config/apps.cfg");
    if (apps.empty()) apps = load_apps("apps.cfg");
    if (apps.empty()) apps = default_apps();

    int cols = (SCREEN_W - 2 * MARGIN) / ICON_SLOT;
    if (cols < 1) cols = 1;

    int sel = 0;
    bool running = true;
    bool esc_pressed = false;   // Fn+Start = выход (см. ниже)

    // раскладка иконок
    int grid_left = MARGIN;
    int grid_top = STATUS_H + MARGIN / 2;

    char clockbuf[16], battbuf[16];
    read_clock(clockbuf, sizeof clockbuf);
    read_battery(battbuf, sizeof battbuf);

    while (running) {
        // обработка событий
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN: {
                if (ev.key.repeat) break;
                switch (ev.key.keysym.sym) {
                case SDLK_UP:    case SDLK_w:    sel -= cols; break;
                case SDLK_DOWN:  case SDLK_s:    sel += cols; break;
                case SDLK_LEFT:  case SDLK_a:    sel -= 1;    break;
                case SDLK_RIGHT: case SDLK_d:    sel += 1;    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                case SDLK_KP_ENTER:
                    if (!apps.empty()) {
                        SDL_HideWindow(R.win);
                        launch_and_wait(apps[sel % apps.size()].command);
                        SDL_ShowWindow(R.win);
                        SDL_RaiseWindow(R.win);
                    }
                    break;
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_F1:
                    // выйти в эмулятор (на R36S запустить emulationstation)
                    SDL_HideWindow(R.win);
                    launch_and_wait("pkill emulationstation; emulationstation");
                    SDL_ShowWindow(R.win);
                    break;
                }
                if (sel < 0) sel = 0;
                break;
            }
            case SDL_JOYBUTTONDOWN: {
                // Маппинг R36S: 0=B,1=A,8..11=dpad,12=Select,13=Start,16=Fn
                int b = ev.jbutton.button;
                if (b == 8)      sel -= cols;              // D-Up
                else if (b == 9) sel += cols;              // D-Down
                else if (b == 10) sel -= 1;                // D-Left
                else if (b == 11) sel += 1;                // D-Right
                else if (b == 16) esc_pressed = true;      // Fn зажат
                else if (b == 13 && esc_pressed) {         // Fn+Start = выход
                    running = false;
                    esc_pressed = false;
                }
                else if ((b == 1 || b == 0) && !esc_pressed) {  // A или B = запуск
                    if (!apps.empty()) {
                        SDL_HideWindow(R.win);
                        launch_and_wait(apps[sel % apps.size()].command);
                        SDL_ShowWindow(R.win);
                        SDL_RaiseWindow(R.win);
                    }
                }
                if (sel < 0) sel = 0;
                break;
            }
            case SDL_JOYBUTTONUP: {
                if (ev.jbutton.button == 16) esc_pressed = false;
                break;
            }
            case SDL_JOYHATMOTION: {
                if (ev.jhat.value & SDL_HAT_UP)    sel -= cols;
                if (ev.jhat.value & SDL_HAT_DOWN)  sel += cols;
                if (ev.jhat.value & SDL_HAT_LEFT)  sel -= 1;
                if (ev.jhat.value & SDL_HAT_RIGHT) sel += 1;
                if (sel < 0) sel = 0;
                break;
            }
            case SDL_JOYAXISMOTION: {
                // аналоговые стики: ось 0/1 — левый, 2/3 — правый
                const int THRESH = 10000;
                if (abs(ev.jaxis.value) > THRESH) {
                    if (ev.jaxis.axis == 0 && ev.jaxis.value > 0) sel += 1;
                    else if (ev.jaxis.axis == 0 && ev.jaxis.value < 0) sel -= 1;
                    else if (ev.jaxis.axis == 1 && ev.jaxis.value > 0) sel += cols;
                    else if (ev.jaxis.axis == 1 && ev.jaxis.value < 0) sel -= cols;
                    if (sel < 0) sel = 0;
                }
                break;
            }
            }
        }

        // ограничить sel
        int total = (int)apps.size();
        if (total > 0) {
            if (sel < 0) sel = 0;
            if (sel >= total) sel = total - 1;
        }

        // --- рендер в пиксельный буфер ---
        if (SDL_LockTexture(R.fb, nullptr, (void **)&R.pix, &R.pitch) != 0) {
            fprintf(stderr, "SDL_LockTexture: %s\n", SDL_GetError());
            break;
        }
        memset(R.pix, 0, (size_t)R.pitch * SCREEN_H);   // фон — чёрный

        // фон-градиент сверху вниз
        for (int y = STATUS_H; y < SCREEN_H; ++y) {
            Uint8 shade = (Uint8)(18 + (y * 20) / SCREEN_H);
            Uint32 c = rgb(shade, shade, shade + 6);
            for (int x = 0; x < SCREEN_W; ++x) R.pix[y * (R.pitch / 4) + x] = c;
        }

        // статус-бар
        draw_statusbar(R, clockbuf, battbuf);

        // иконки
        int ncols = cols;
        for (size_t i = 0; i < apps.size(); ++i) {
            int r = (int)(i / ncols);
            int c = (int)(i % ncols);
            int x = grid_left + c * ICON_SLOT + (ICON_SLOT - 96) / 2;
            int y = grid_top + r * (ICON_SLOT + LABEL_H);
            if (y + ICON_SLOT > SCREEN_H - 8) break;
            drawicon(R, apps[i], x, y, 96, (int)i == sel);
            drawlabel(R, apps[i].name, grid_left + c * ICON_SLOT,
                      y + 96 + 2, ICON_SLOT, 2, (int)i == sel);
        }

        // подсказка внизу
        const char *hint = "A/B: run   arrows: move   F1: exit   Esc: quit";
        drawtext(R, hint, (SCREEN_W - textw(hint, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 130));

        SDL_UnlockTexture(R.fb);
        SDL_RenderCopy(R.ren, R.fb, nullptr, nullptr);
        SDL_RenderPresent(R.ren);

        // обновлять часы раз в ~1 сек
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