// files.h — FAR-подобный двухпанельный файловый менеджер.
#pragma once
#include <cerrno>
#include "term.h"

struct Panel {
    std::string cwd = "/";
    std::vector<std::pair<std::string, bool>> entries;   // имя, isdir
    int sel = 0;
    bool viewing = false;
    std::string viewname;
    std::vector<std::string> view;
    int vscroll = 0;

    void refresh() {
        entries.clear(); sel = 0;
#ifdef _WIN32
        if (cwd.empty()) cwd = GETCWD(nullptr, 0);
#endif
        DIR *d = opendir(cwd.c_str());
        if (!d) return;
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
                if (stat(path_join(cwd, name).c_str(), &st) == 0) isd = ISDIR(st);
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
    }
    void enter() {
        if (sel < 0 || sel >= (int)entries.size()) return;
        auto &e = entries[sel];
        if (e.second) { cwd = path_join(cwd, e.first); refresh(); }
        else open_preview(e.first);
    }
    void up() {
        if (viewing) { viewing = false; return; }
        if (cwd == "/") return;
        size_t p = cwd.rfind('/');
        cwd = (p == std::string::npos || p == 0) ? "/" : cwd.substr(0, p);
        refresh();
    }
    void open_preview(const std::string &fname) {
        std::string full = path_join(cwd, fname);
        FILE *f = fopen(full.c_str(), "rb");
        if (!f) return;
        char buf[32768];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        view.clear();
        bool binary = n > 0 && memchr(buf, 0, n) != nullptr;
        if (binary) view.push_back("(binary, " + std::to_string((int)n) + " bytes)");
        else {
            std::string acc;
            for (size_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') { view.push_back(acc); acc.clear(); }
                else acc += buf[i];
            }
            if (!acc.empty()) view.push_back(acc);
        }
        viewname = fname; vscroll = 0; viewing = true;
    }
};

struct FileState {
    Panel left, right;
    int focus = 0;       // 0=left, 1=right
    std::string msg;
    bool confirm_delete = false;
    std::string confirm_name;
    // F9-меню действий (как в FAR)
    int menu = -1;       // активный пункт меню или -1
    // ввод имени для rename/mkdir
    bool input_active = false;
    std::string input_label;
    std::string input_val;
    int input_target = 0;   // 0=rename, 1=mkdir
    std::string input_old;
    Kbd kb;

    Panel &cur() { return focus == 0 ? left : right; }
    Panel &oth() { return focus == 0 ? right : left; }

    void refresh() { left.refresh(); right.refresh(); }

    void cmd_copy() {
        Panel &c = cur(), &o = oth();
        if (c.sel < 0 || c.sel >= (int)c.entries.size()) return;
        auto &e = c.entries[c.sel];
        if (e.second) { msg = "copy: папки пока не копируются"; return; }
        std::string src = path_join(c.cwd, e.first);
        std::string dst = path_join(o.cwd, e.first);
        FILE *in = fopen(src.c_str(), "rb");
        if (!in) { msg = "copy: не могу открыть " + src; return; }
        FILE *out = fopen(dst.c_str(), "wb");
        if (!out) { fclose(in); msg = "copy: не могу писать " + dst; return; }
        char buf[8192]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in); fclose(out);
        msg = "скопировано: " + e.first;
        o.refresh();
    }
    void cmd_move() {
        Panel &c = cur(), &o = oth();
        if (c.sel < 0 || c.sel >= (int)c.entries.size()) return;
        auto &e = c.entries[c.sel];
        std::string src = path_join(c.cwd, e.first);
        std::string dst = path_join(o.cwd, e.first);
        if (rename(src.c_str(), dst.c_str()) == 0) {
            msg = "перемещено: " + e.first;
            c.refresh(); o.refresh();
        } else msg = "move: не удалось (" + std::string(strerror(errno)) + ")";
    }
    void cmd_mkdir() {
        if (input_val.empty()) return;
        std::string full = path_join(cur().cwd, input_val);
        if (mkdir(full.c_str(), 0755) == 0) {
            msg = "папка создана: " + input_val;
            cur().refresh();
        } else msg = "mkdir: " + std::string(strerror(errno));
        input_active = false;
    }
    void cmd_rename() {
        if (input_val.empty()) return;
        Panel &c = cur();
        if (c.sel < 0 || c.sel >= (int)c.entries.size()) return;
        std::string src = path_join(c.cwd, c.entries[c.sel].first);
        std::string dst = path_join(c.cwd, input_val);
        if (rename(src.c_str(), dst.c_str()) == 0) {
            msg = "переименовано: " + input_val;
            c.refresh();
        } else msg = "rename: " + std::string(strerror(errno));
        input_active = false;
    }
    void cmd_delete() {
        Panel &c = cur();
        if (c.sel < 0 || c.sel >= (int)c.entries.size()) return;
        confirm_name = c.entries[c.sel].first;
        confirm_delete = true;
    }
    void do_delete() {
        Panel &c = cur();
        std::string full = path_join(c.cwd, confirm_name);
        if (remove(full.c_str()) == 0) {
            msg = "удалено: " + confirm_name;
            c.refresh();
        } else msg = "не удалось удалить";
        confirm_delete = false;
    }
};

static void render_panel(Renderer &R, Panel &p, int x, int w, bool focused) {
    fillrect(R, x, STATUS_H + 2, w, SCREEN_H - STATUS_H - 2, focused ? rgb(30, 30, 50) : rgb(20, 20, 32));
    // путь
    std::string cwd = p.cwd;
    if ((int)cwd.size() > 44) cwd = cwd.substr(cwd.size() - 44);
    drawtext(R, cwd.c_str(), x + 4, STATUS_H + 2, 2, rgb(255, 220, 60));
    if (p.viewing) {
        drawtext(R, p.viewname.c_str(), x + 4, STATUS_H + 22, 2, rgb(120, 220, 120));
        int y = STATUS_H + 44;
        int maxlines = (SCREEN_H - y - 20) / 16;
        for (size_t i = (size_t)p.vscroll; i < p.view.size() && (int)(i - p.vscroll) < maxlines; ++i) {
            std::string s = p.view[i];
            if ((int)s.size() > w / 12 - 1) s = s.substr(0, w / 12 - 1);
            drawtext(R, s.c_str(), x + 4, y + (int)(i - p.vscroll) * 16, 2, rgb(220, 220, 220));
        }
        return;
    }
    int y = STATUS_H + 22;
    int maxlines = (SCREEN_H - y - 24) / 16;
    int start = p.sel - maxlines / 2;
    if (start < 0) start = 0;
    for (int i = start; i < (int)p.entries.size() && i < start + maxlines; ++i) {
        bool sel = (i == p.sel);
        std::string s = p.entries[i].second ? "[d] " : "    ";
        s += p.entries[i].first;
        if ((int)s.size() > w / 12 - 1) s = s.substr(0, w / 12 - 1);
        drawtext(R, s.c_str(), x + 4, y + (i - start) * 16, 2,
                 sel ? rgb(255, 220, 60) : rgb(215, 215, 215));
        if (sel) fillrect(R, x + w - 8, y + (i - start) * 16, 6, 14, rgb(255, 220, 60));
    }
}

static void render_files(Renderer &R, FileState &F, const char *clock, const char *batt, const Settings &S) {
    fillrect(R, 0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 0));
    draw_statusbar(R, clock, batt, "FAR Files");
    if (!F.msg.empty()) drawtext(R, F.msg.c_str(), 8, STATUS_H - 1, 2, rgb(255, 150, 90));

    if (F.input_active) {
        // оверлей ввода имени (rename / mkdir) — как диалог в FAR
        draw_keyboard(R, F.kb, STATUS_H + 64, S);
        fillrect(R, 0, STATUS_H, SCREEN_W, 60, rgb(30, 30, 55));
        drawtext(R, F.input_label.c_str(), 8, STATUS_H + 4, 2, rgb(255, 220, 60));
        std::string v = F.input_val + "_";
        if ((int)v.size() > 55) v = v.substr(0, 55);
        drawtext(R, v.c_str(), 8, STATUS_H + 24, 2, rgb(255, 255, 255));
        return;
    }

    if (F.menu >= 0) {
        // F9-меню действий
        fillrect(R, 180, 80, 280, 160, rgb(40, 40, 70));
        drawtext(R, "  F9: Menu", 190, 88, 2, rgb(255, 220, 60));
        const char *items[] = { "F3 Просмотр", "F5 Копировать", "F6 Переместить",
                                "F7 Создать папку", "F8 Удалить", "F2 Переименовать" };
        int y = 112;
        for (int i = 0; i < 6; ++i) {
            bool sel = (F.menu == i);
            drawtext(R, items[i], 190, y + i * 22, 2, sel ? rgb(255, 220, 60) : rgb(220, 220, 220));
            if (sel) fillrect(R, 460, y + i * 22, 6, 14, rgb(255, 220, 60));
        }
        drawtext(R, "A: ok  B: close  arrows: move", 190, 216, 2, rgb(140, 140, 160));
        return;
    }

    render_panel(R, F.left, 0, SCREEN_W / 2, F.focus == 0);
    render_panel(R, F.right, SCREEN_W / 2, SCREEN_W / 2, F.focus == 1);

    // строка F-клавиш как в FAR
    const char *fkeys[10] = { "F1 Help", "F2 Ren", "F3 View", "F4 Edit", "F5 Copy",
                              "F6 Move", "F7 MkDir", "F8 Del", "F9 Menu", "F10 Quit" };
    fillrect(R, 0, SCREEN_H - 18, SCREEN_W, 18, rgb(0, 0, 0));
    for (int i = 0; i < 10; ++i) {
        bool sel = (i == 4 && F.focus != -999); // подсветка доступных
        drawtext(R, fkeys[i], 4 + i * 62, SCREEN_H - 16, 1, sel ? rgb(255, 220, 60) : rgb(150, 150, 165));
    }

    if (F.confirm_delete) {
        fillrect(R, 150, 200, 340, 60, rgb(70, 25, 25));
        std::string t = "Удалить " + F.confirm_name + "?";
        if ((int)t.size() > 40) t = t.substr(0, 40);
        drawtext(R, t.c_str(), 165, 214, 2, rgb(255, 255, 255));
        drawtext(R, "A: yes  B: no", 165, 236, 2, rgb(255, 200, 80));
    }
}