// keyboard.h — экранная QWERTY-клавиатура + подсказки команд.
#pragma once
#include "settings.h"

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
static const char *KB_FUNC[7] = { "space", "shift", "del", "tab", "enter", "abc", "exit" };

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
        if (col < 0 || col >= 7) return nullptr;
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
            if (col >= 7) col = 6;
        }
        if (col < 0) col = 0;
    }
    void move(int dr, int dc) { row += dr; col += dc; clamp(); }
};

static void draw_keyboard(Renderer &R, const Kbd &kb, int y0, const Settings &S) {
    int s = S.kb_scale;
    int chw = 5 * s + s;
    fillrect(R, 0, y0, SCREEN_W, SCREEN_H - y0, rgb(20, 20, 30));
    int kw = SCREEN_W / 10;
    int kh = 38;
    for (int r = 0; r < 4; ++r) {
        const char *line = kb.page == 0 ? KB_PAGE0[r] : KB_PAGE1[r];
        int n = (int)strlen(line);
        for (int c = 0; c < n; ++c) {
            int x = c * kw;
            int y = y0 + 4 + r * (kh + 4);
            bool sel = kb.row == r && kb.col == c;
            Uint32 col = sel ? rgb(255, 220, 60) : rgb(55, 55, 75);
            fillrect(R, x + 2, y, kw - 4, kh - 4, col);
            char b[2] = { line[c], 0 };
            if (r < 3 && kb.page == 0 && kb.shift && line[c] >= 'a' && line[c] <= 'z') b[0] = (char)(line[c] - 32);
            drawtext(R, b, x + (kw - textw(b, s)) / 2, y + (kh - 7 * s) / 2, s, sel ? rgb(0,0,0) : rgb(230,230,230));
        }
    }
    int fx[7] = { 4, 94, 184, 274, 364, 454, 544 };
    for (int c = 0; c < 7; ++c) {
        int x = fx[c];
        int y = y0 + 4 + 4 * (kh + 4);
        bool sel = kb.row == 4 && kb.col == c;
        Uint32 col = sel ? rgb(255, 220, 60) : rgb(70, 70, 95);
        fillrect(R, x, y, 86, kh, col);
        const char *t = KB_FUNC[c];
        drawtext(R, t, x + (86 - textw(t, 2)) / 2, y + (kh - 14) / 2, 2, sel ? rgb(0,0,0) : rgb(230,230,230));
    }
}

// Подсказки команд: сканируем /bin /usr/bin один раз
static std::vector<std::string> cmd_suggestions(const std::string &prefix) {
    static std::vector<std::string> all;
    static bool scanned = false;
    if (!scanned) {
        scanned = true;
#ifdef _WIN32
        all.push_back("help"); all.push_back("dir"); all.push_back("cd");
#else
        for (const char *dir : {"/bin", "/usr/bin", "/usr/local/bin"}) {
            DIR *d = opendir(dir);
            if (!d) continue;
            struct dirent *e;
            while ((e = readdir(d))) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                std::string full = std::string(dir) + "/" + n;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && (st.st_mode & S_IXUSR))
                    all.push_back(n);
            }
            closedir(d);
        }
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        // встроенные команды
        all.insert(all.begin(), { "help", "clear", "ls", "pwd", "echo", "cd", "exit" });
#endif
    }
    if (prefix.empty()) return {};
    std::vector<std::string> out;
    for (auto &c : all)
        if (c.compare(0, prefix.size(), prefix) == 0) {
            out.push_back(c);
            if ((int)out.size() >= 9) break;
        }
    return out;
}