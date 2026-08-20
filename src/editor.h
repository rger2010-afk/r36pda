// editor.h — простой текстовый редактор для Python-приложений.
#pragma once
#include "files.h"

struct EditState {
    std::vector<std::string> lines;
    int row = 0, col = 0;        // позиция курсора
    Kbd kb;                      // клавиатура для ввода
    std::string path = "myapps/app.py";
    std::string msg;
    bool dirty = false;

    void reset(const std::string &p) {
        path = p; lines.clear(); lines.push_back(""); row = 0; col = 0;
        kb = Kbd(); msg = ""; dirty = false;
        load();
    }
    void load() {
        lines.clear();
        FILE *f = fopen(path.c_str(), "r");
        if (!f) { lines.push_back(""); return; }
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            lines.push_back(s);
        }
        fclose(f);
        if (lines.empty()) lines.push_back("");
        if (row >= (int)lines.size()) row = (int)lines.size() - 1;
        if (col > (int)lines[row].size()) col = (int)lines[row].size();
        dirty = false;
    }
    void save() {
        FILE *f = fopen(path.c_str(), "w");
        if (!f) { msg = "не могу писать " + path; return; }
        for (auto &l : lines) fprintf(f, "%s\n", l.c_str());
        fclose(f);
        dirty = false;
        msg = "сохранено: " + path;
    }
    void type(char ch) {
        if (ch == '\n') {
            std::string tail = lines[row].substr(col);
            lines[row] = lines[row].substr(0, col);
            lines.insert(lines.begin() + row + 1, tail);
            ++row; col = 0;
        } else {
            if (col > (int)lines[row].size()) col = (int)lines[row].size();
            lines[row].insert(lines[row].begin() + col, ch);
            ++col;
        }
        dirty = true;
    }
    void backspace() {
        if (col > 0) {
            lines[row].erase(lines[row].begin() + col - 1);
            --col;
        } else if (row > 0) {
            col = (int)lines[row - 1].size();
            lines[row - 1] += lines[row];
            lines.erase(lines.begin() + row);
            --row;
        }
        dirty = true;
    }
    void mv(int dr, int dc) {
        int nr = row + dr, nc = col + dc;
        if (nr < 0) nr = 0;
        if (nr >= (int)lines.size()) nr = (int)lines.size() - 1;
        if (nc < 0) nc = 0;
        if (nc > (int)lines[nr].size()) nc = (int)lines[nr].size();
        row = nr; col = nc;
    }
    void run_python() {
        save();
        std::string cmd = "cd myapps && python3 app.py 2>&1";
        std::vector<std::string> out;
        run_cmd_lines(cmd, out);
        msg = out.empty() ? "python: ok (без вывода)" : out[0];
        if ((int)msg.size() > 52) msg = msg.substr(0, 52);
    }
};

static int ed_col(EditState &E, int i) { return (i == E.row) ? E.col : 0; }

static void render_editor(Renderer &R, EditState &E, const char *clock, const char *batt, const Settings &S) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "Python editor");

    // текстовое поле
    int kb_h = 4 * 42 + 34;
    int area_bottom = SCREEN_H - kb_h - 2;
    if (!E.msg.empty()) drawtext(R, E.msg.c_str(), 8, STATUS_H + 2, 2, rgb(255, 180, 90));

    int line_h = 16;
    int maxlines = (area_bottom - STATUS_H - 24) / line_h;
    int start = E.row - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)E.lines.size() && i < start + maxlines; ++i) {
        bool sel = (i == E.row);
        std::string s = E.lines[i];
        if ((int)s.size() > 58) s = s.substr(0, 58);
        drawtext(R, s.c_str(), 8, STATUS_H + 22 + (i - start) * line_h, 2,
                 sel ? rgb(255, 220, 60) : rgb(220, 220, 220));
        if (sel) {
            int cc = std::min(ed_col(E, i), 58);
            int tw = textw(E.lines[i].substr(0, cc).c_str(), 2);
            fillrect(R, 8 + tw, STATUS_H + 22 + (i - start) * line_h, 2, 14, rgb(255, 255, 255));
        }
    }
    draw_keyboard(R, E.kb, SCREEN_H - kb_h, S);
    const char *h = "A:key B:del Start:run Select:save Tab:newline";
    drawtext(R, h, (SCREEN_W - textw(h, 2)) / 2, area_bottom - 12, 2, rgb(120, 120, 135));
}