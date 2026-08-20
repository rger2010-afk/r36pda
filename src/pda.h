// pda.h — классические КПК-приложения: Записки, Контакты, Календарь,
// Задачи, Рисование, Читалка. Плюс общий диалог ввода через клавиатуру.
#pragma once
#include "editor.h"
#include <map>
#include <functional>

// ============================= общий диалог ввода =============================
static bool inp_on = false;
static int  inp_target = 0;
static std::string inp_label, inp_val;
static Kbd inp_kb;
static std::function<void()> inp_done;

static void inp_open(const std::string &label, int target, std::function<void()> cb) {
    inp_on = true; inp_label = label; inp_val = "";
    inp_kb = Kbd(); inp_target = target; inp_done = cb;
}

// обработка кнопок диалога; возвращает true, если событие поглощено
static bool inp_handle(int act) {
    if (!inp_on) return false;
    if (act == BTN_UP) inp_kb.move(-1, 0);
    else if (act == BTN_DOWN) inp_kb.move(1, 0);
    else if (act == BTN_LEFT) inp_kb.move(0, -1);
    else if (act == BTN_RIGHT) inp_kb.move(0, 1);
    else if (act == BTN_A || act == BTN_B) {
        const char *k = inp_kb.cell_char();
        if (k) {
            std::string key = k;
            if (key == "space") inp_val += ' ';
            else if (key == "del") { if (!inp_val.empty()) inp_val.pop_back(); }
            else if (key == "enter") { auto cb = inp_done; inp_on = false; if (cb) cb(); }
            else if (key == "abc") inp_kb.page = 1 - inp_kb.page;
            else if (key == "shift") inp_kb.shift = !inp_kb.shift;
            else if (key == "exit") { inp_on = false; }
            else inp_val += key;
        }
    }
    return true;
}

static void render_input(Renderer &R, const Settings &S) {
    int kb_h = 4 * 42 + 34;
    int y0 = SCREEN_H - kb_h;
    std::string line = inp_label + ": " + inp_val;
    if ((int)line.size() > 52) line = line.substr(0, 52);
    fillrect(R, 0, STATUS_H, SCREEN_W, 50, rgb(30, 40, 60));
    drawtext(R, line.c_str(), 8, STATUS_H + 10, 2, rgb(255, 220, 60));
    fillrect(R, 0, y0, SCREEN_W, kb_h, rgb(20, 20, 30));
    draw_keyboard(R, inp_kb, y0, S);
}

// ============================= Записки (Notes) =============================
struct NotesState {
    std::vector<std::string> files;   // полные пути notes/*.txt
    int sel = 0;
    void clamp() {
        if (sel < 0) sel = 0;
        if (!files.empty() && sel >= (int)files.size()) sel = (int)files.size() - 1;
    }
    void refresh() {
        files.clear();
        DIR *d = opendir("notes");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".txt")
                    files.push_back("notes/" + n);
            }
            closedir(d);
        }
        std::sort(files.begin(), files.end());
        clamp();
    }
    std::string name(int i) const {
        std::string s = files[i];
        size_t p = s.rfind('/');
        return (p == std::string::npos) ? s : s.substr(p + 1);
    }
    std::string new_path() const {
        int n = 1;
        while (true) {
            std::string p = "notes/note" + std::to_string(n) + ".txt";
            FILE *f = fopen(p.c_str(), "rb");
            if (!f) return p;
            fclose(f); ++n;
        }
    }
    void del_sel() {
        if (sel < 0 || sel >= (int)files.size()) return;
        remove(files[sel].c_str());
        files.erase(files.begin() + sel);
        clamp();
    }
};
static NotesState notes;

static void render_notes(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Записки");
    int maxlines = (SCREEN_H - STATUS_H - 40) / 18;
    int start = notes.sel - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)notes.files.size() && i < start + maxlines; ++i) {
        bool s = (i == notes.sel);
        std::string n = notes.name(i);
        if ((int)n.size() > 50) n = n.substr(0, 50);
        drawtext(R, n.c_str(), 8, STATUS_H + 8 + (i - start) * 18, 2,
                 s ? rgb(255, 220, 60) : rgb(220, 220, 220));
    }
    const char *h = "A: открыть  Select: новая  L/R: удалить  B: назад";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
}

// ============================= Контакты (Contacts) =============================
struct Contact { std::string f[4]; };
static const char *CONTACT_FIELD[4] = { "Имя", "Телефон", "Email", "Адрес" };

struct ContactsState {
    std::vector<Contact> cs;
    int sel = 0, field = 0;
    bool viewing = false;
    void load() {
        cs.clear();
        FILE *f = fopen("contacts.txt", "r");
        if (!f) return;
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            Contact c;
            for (int i = 0; i < 4; ++i) {
                size_t p = s.find('|');
                if (p == std::string::npos) { c.f[i] = s; s.clear(); }
                else { c.f[i] = s.substr(0, p); s = s.substr(p + 1); }
            }
            cs.push_back(c);
        }
        fclose(f);
    }
    void save() {
        FILE *f = fopen("contacts.txt", "w");
        if (!f) return;
        for (auto &c : cs)
            fprintf(f, "%s|%s|%s|%s\n", c.f[0].c_str(), c.f[1].c_str(),
                    c.f[2].c_str(), c.f[3].c_str());
        fclose(f);
    }
    void clamp() {
        if (sel < 0) sel = 0;
        if (!cs.empty() && sel >= (int)cs.size()) sel = (int)cs.size() - 1;
        if (field < 0) field = 0;
        if (field > 3) field = 3;
    }
};
static ContactsState contacts;

static void render_contacts(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Контакты");
    if (contacts.viewing && !contacts.cs.empty()) {
        Contact &c = contacts.cs[contacts.sel];
        int y = STATUS_H + 12;
        for (int i = 0; i < 4; ++i) {
            bool s = (i == contacts.field);
            std::string line = std::string(CONTACT_FIELD[i]) + ": " + c.f[i];
            if ((int)line.size() > 56) line = line.substr(0, 56);
            drawtext(R, line.c_str(), 8, y + i * 18, 2,
                     s ? rgb(255, 220, 60) : rgb(220, 220, 220));
        }
        const char *h = "A: изменить  B: назад";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
    } else {
        int maxlines = (SCREEN_H - STATUS_H - 40) / 18;
        int start = contacts.sel - maxlines / 2;
        if (start < 0) start = 0;
        for (int i = start; i < (int)contacts.cs.size() && i < start + maxlines; ++i) {
            bool s = (i == contacts.sel);
            std::string n = contacts.cs[i].f[0];
            if (n.empty()) n = "(без имени)";
            if ((int)n.size() > 50) n = n.substr(0, 50);
            drawtext(R, n.c_str(), 8, STATUS_H + 8 + (i - start) * 18, 2,
                     s ? rgb(255, 220, 60) : rgb(220, 220, 220));
        }
        const char *h = "A: открыть  Select: новый  L/R: удалить  B: назад";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
    }
}

// ============================= Календарь (Calendar) =============================
struct CalendarState {
    int year = 2026, mon = 0;    // mon 0..11
    int day_sel = 1;
    std::map<std::string, std::vector<std::string>> ev;
    static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
    static int dim(int y, int m) {
        static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (m == 1 && leap(y)) return 29;
        return d[m];
    }
    static int wday0(int y, int m) {  // день недели 1-го числа (0=Вс)
        struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = 1;
        mktime(&t);
        return t.tm_wday;
    }
    std::string key(int y, int m, int d) const {
        char b[16];
        snprintf(b, sizeof b, "%04d-%02d-%02d", y, m + 1, d);
        return b;
    }
    void load() {
        ev.clear();
        FILE *f = fopen("calendar.txt", "r");
        if (!f) return;
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            size_t p = s.find('|');
            if (p == std::string::npos) continue;
            std::string k = s.substr(0, p), v = s.substr(p + 1);
            if (k.size() == 10) ev[k].push_back(v);
        }
        fclose(f);
    }
    void save() {
        FILE *f = fopen("calendar.txt", "w");
        if (!f) return;
        for (auto &kv : ev)
            for (auto &v : kv.second)
                fprintf(f, "%s|%s\n", kv.first.c_str(), v.c_str());
        fclose(f);
    }
    void refresh() {
        time_t t = time(nullptr);
        struct tm *lt = localtime(&t);
        year = lt->tm_year + 1900; mon = lt->tm_mon; day_sel = lt->tm_mday;
        load();
    }
    void month_shift(int d) {
        mon += d;
        if (mon < 0) { mon = 11; --year; }
        if (mon > 11) { mon = 0; ++year; }
        if (day_sel > dim(year, mon)) day_sel = dim(year, mon);
    }
    void clamp_day() { if (day_sel < 1) day_sel = 1; if (day_sel > dim(year, mon)) day_sel = dim(year, mon); }
};
static CalendarState cal;

static void render_calendar(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Календарь");
    char title[32];
    snprintf(title, sizeof title, "%04d-%02d", cal.year, cal.mon + 1);
    drawtext(R, title, (SCREEN_W - textw(title, 2)) / 2, STATUS_H + 6, 2, rgb(200, 200, 220));
    int gy = STATUS_H + 30;
    static const char *wd[7] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };
    int cw = SCREEN_W / 7;
    for (int c = 0; c < 7; ++c)
        drawtext(R, wd[c], c * cw + (cw - textw(wd[c], 2)) / 2, gy, 2, rgb(140, 140, 160));
    int gy0 = gy + 22, ch = 52;
    int off = (wday0(cal.year, cal.mon) + 6) % 7;   // понедельник первым
    int days = cal.dim(cal.year, cal.mon);
    for (int i = 0; i < 42; ++i) {
        int r = i / 7, c = i % 7;
        int day = i - off + 1;
        if (day < 1 || day > days) continue;
        bool s = (day == cal.day_sel);
        int x = c * cw, y = gy0 + r * ch;
        if (s) fillrect(R, x + 2, y + 2, cw - 4, ch - 4, rgb(70, 70, 100));
        else fillrect(R, x + 2, y + 2, cw - 4, ch - 4, rgb(25, 25, 40));
        char b[4];
        snprintf(b, sizeof b, "%d", day);
        drawtext(R, b, x + 4, y + 4, 2, s ? rgb(255, 220, 60) : rgb(200, 200, 210));
        auto it = cal.ev.find(cal.key(cal.year, cal.mon, day));
        if (it != cal.ev.end() && !it->second.empty())
            drawtext(R, (it->second.size() > 9 ? "9+" : std::to_string(it->second.size())).c_str(),
                     x + cw - 14, y + 4, 2, rgb(120, 220, 120));
    }
    const char *h = "A: событие  Select/Start: месяц  B: назад";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
}

// ============================= Задачи (Todo) =============================
struct TodoState {
    std::vector<std::pair<bool, std::string>> items;
    int sel = 0;
    void load() {
        items.clear();
        FILE *f = fopen("todo.txt", "r");
        if (!f) return;
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (s.empty()) continue;
            bool done = (s[0] == '1');
            items.push_back({ done, s.size() > 2 ? s.substr(2) : "" });
        }
        fclose(f);
    }
    void save() {
        FILE *f = fopen("todo.txt", "w");
        if (!f) return;
        for (auto &it : items)
            fprintf(f, "%d|%s\n", it.first ? 1 : 0, it.second.c_str());
        fclose(f);
    }
    void clamp() {
        if (sel < 0) sel = 0;
        if (!items.empty() && sel >= (int)items.size()) sel = (int)items.size() - 1;
    }
};
static TodoState todo;

static void render_todo(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Задачи");
    int maxlines = (SCREEN_H - STATUS_H - 40) / 18;
    int start = todo.sel - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)todo.items.size() && i < start + maxlines; ++i) {
        bool s = (i == todo.sel);
        std::string box = todo.items[i].first ? "[x]" : "[ ]";
        std::string t = todo.items[i].second;
        if ((int)t.size() > 48) t = t.substr(0, 48);
        std::string line = box + " " + t;
        drawtext(R, line.c_str(), 8, STATUS_H + 8 + (i - start) * 18, 2,
                 s ? rgb(255, 220, 60) : (todo.items[i].first ? rgb(130, 160, 130) : rgb(220, 220, 220)));
    }
    const char *h = "A: отметить  Select: новая  L/R: удалить  B: назад";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
}

// ============================= Рисование (Paint) =============================
static const Uint32 PAINT_PAL[8] = {
    rgb(255, 255, 255), rgb(255, 60, 60), rgb(60, 200, 60), rgb(60, 140, 255),
    rgb(255, 200, 40), rgb(255, 80, 220), rgb(60, 220, 220), rgb(0, 0, 0),
};

struct PaintState {
    std::vector<Uint32> px;
    int color_idx = 0;
    bool pen_down = false;
    int lx = -1, ly = -1;
    void init() { px.assign((size_t)SCREEN_W * SCREEN_H, 0); pen_down = false; lx = ly = -1; load(); }
    void clear() { std::fill(px.begin(), px.end(), (Uint32)0); }
    void put(int x, int y, Uint32 c) {
        if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
        px[(size_t)y * SCREEN_W + x] = c;
    }
    void thick(int x, int y, Uint32 c) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                put(x + dx, y + dy, c);
    }
    void line(int x0, int y0, int x1, int y1, Uint32 c) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            thick(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void save() {
        FILE *f = fopen("paint.dat", "wb");
        if (!f) return;
        fwrite(px.data(), 4, px.size(), f);
        fclose(f);
    }
    void load() {
        FILE *f = fopen("paint.dat", "rb");
        if (!f) return;
        fread(px.data(), 4, px.size(), f);
        fclose(f);
    }
};
static PaintState paint;

static void render_paint(Renderer &R, const char *clock, const char *batt) {
    memcpy(R.pix, paint.px.data(), (size_t)SCREEN_W * SCREEN_H * 4);
    draw_statusbar(R, clock, batt, "Рисование");
    int y = STATUS_H + 6;
    for (int i = 0; i < 8; ++i) {
        int x = 8 + i * 34;
        bool s = (i == paint.color_idx);
        fillrect(R, x, y, 28, 14, PAINT_PAL[i]);
        if (s) { fillrect(R, x - 1, y - 1, 30, 16, rgb(255, 220, 60)); fillrect(R, x, y, 28, 14, PAINT_PAL[i]); }
    }
    const char *st = paint.pen_down ? "ВЕДЁТ ПЕРО" : "перо поднято";
    drawtext(R, st, 320, y + 2, 2, paint.pen_down ? rgb(255, 80, 80) : rgb(140, 140, 160));
    const char *h = "A: перо  L/R: цвет  Select: стереть  Start: сохранить  B: назад";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
}

// ============================= Читалка (Reader) =============================
struct ReaderState {
    std::vector<std::string> books;
    std::vector<std::string> content;
    int sel = 0, pos = 0;
    bool reading = false;
    int cur = -1;
    void refresh() {
        books.clear();
        DIR *d = opendir("books");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".txt")
                    books.push_back("books/" + n);
            }
            closedir(d);
        }
        std::sort(books.begin(), books.end());
        if (sel < 0) sel = 0;
        if (!books.empty() && sel >= (int)books.size()) sel = (int)books.size() - 1;
    }
    void open(int i) {
        content.clear(); pos = 0; cur = i; reading = true;
        FILE *f = fopen(books[i].c_str(), "r");
        if (!f) return;
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            while ((int)s.size() > 72) { content.push_back(s.substr(0, 72)); s = s.substr(72); }
            content.push_back(s);
        }
        fclose(f);
    }
    void close() { reading = false; }
};
static ReaderState reader;

static void render_reader(Renderer &R, const char *clock, const char *batt) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    if (reader.reading) {
        std::string t = reader.cur >= 0 ? reader.books[reader.cur] : "";
        draw_statusbar(R, clock, batt, t.c_str());
        int maxlines = (SCREEN_H - STATUS_H - 28) / 16;
        if (reader.pos > (int)reader.content.size() - maxlines) reader.pos = (int)reader.content.size() - maxlines;
        if (reader.pos < 0) reader.pos = 0;
        for (int i = reader.pos; i < (int)reader.content.size() && i < reader.pos + maxlines; ++i) {
            std::string s = reader.content[i];
            if ((int)s.size() > 76) s = s.substr(0, 76);
            drawtext(R, s.c_str(), 8, STATUS_H + 6 + (i - reader.pos) * 16, 2, rgb(225, 225, 225));
        }
        const char *h = "L/R: страницы  Select: список  B: назад";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 12, 2, rgb(120, 120, 135));
    } else {
        draw_statusbar(R, clock, batt, "Читалка");
        int maxlines = (SCREEN_H - STATUS_H - 40) / 18;
        int start = reader.sel - maxlines / 2;
        if (start < 0) start = 0;
        for (int i = start; i < (int)reader.books.size() && i < start + maxlines; ++i) {
            bool s = (i == reader.sel);
            std::string n = reader.books[i];
            size_t p = n.rfind('/');
            if (p != std::string::npos) n = n.substr(p + 1);
            if ((int)n.size() > 50) n = n.substr(0, 50);
            drawtext(R, n.c_str(), 8, STATUS_H + 8 + (i - start) * 18, 2,
                     s ? rgb(255, 220, 60) : rgb(220, 220, 220));
        }
        const char *h = "A: открыть  B: назад";
        drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, SCREEN_H - 14, 2, rgb(120, 120, 135));
    }
}