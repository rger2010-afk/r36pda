// app.h — общие типы приложений, состояния и вспомогательные функции.
#pragma once
#include "settings.h"

enum AppMode { M_DESKTOP, M_TERM, M_FILES, M_SYSINFO, M_PROC, M_CALC, M_SETTINGS, M_SCRIPT, M_EXTERNAL, M_EDIT, M_NOTES, M_CONTACTS, M_CALENDAR, M_TODO, M_PAINT, M_READER };

struct App {
    std::string name;
    AppMode mode = M_DESKTOP;
    std::string command;   // для M_EXTERNAL / M_SCRIPT
    std::string script;
    Uint8 color[3] = {80, 180, 230};
};

// процессы
struct ProcState {
    std::vector<std::pair<int, std::string>> procs;
    int sel = 0;
    int confirm = -1;
    std::string msg;

    void refresh() {
        procs.clear(); sel = 0;
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

// калькулятор
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

static void calc_press(CalcState &C, char ch) {
    if (ch >= '0' && ch <= '9') C.digit(ch - '0');
    else if (ch == 'C') C.clear();
    else if (ch == '=') C.equals();
    else if (ch == '+') C.apply_op(1);
    else if (ch == '-') C.apply_op(2);
    else if (ch == '*') C.apply_op(3);
    else if (ch == '/') C.apply_op(4);
}

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