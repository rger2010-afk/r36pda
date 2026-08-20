// term.h — встроенный терминал с подсказками и встроенными командами.
#pragma once
#include "keyboard.h"

struct TermState {
    std::vector<std::string> lines;
    std::string input;
    Kbd kb;
    std::thread *worker = nullptr;
    std::vector<std::string> pending;
    std::mutex mtx;
    bool running = false;
    bool exit_requested = false;
    std::vector<std::string> suggestions;

    void add_line(const std::string &s) {
        if (lines.size() > 400) lines.erase(lines.begin());
        lines.push_back(s);
    }
    void builtin_cmd(const std::string &cmd) {
        // встроенные команды не требуют /bin
        if (cmd == "help") {
            add_line("Встроенные: help clear ls pwd echo exit");
            add_line("Обычные команды выполняются через /bin/sh.");
            add_line("Кнопки: A - клавиша, B - del, Start - enter,");
            add_line("Select - подсказка, Tab - автодополнение.");
            return;
        }
        if (cmd == "clear") { lines.clear(); return; }
        if (cmd == "pwd") {
#ifdef _WIN32
            add_line(GETCWD(nullptr, 0));
#else
            char b[1024];
            add_line(getcwd(b, sizeof b) ? b : "?");
#endif
            return;
        }
        if (cmd == "echo") { add_line(""); return; }
        if (cmd.rfind("echo ", 0) == 0) { add_line(cmd.substr(5)); return; }
        if (cmd == "ls") {
            std::vector<std::string> out;
            run_cmd_lines("ls -la", out);
            for (auto &s : out) add_line(s);
            return;
        }
        if (cmd == "exit") { exit_requested = true; return; }
        start_shell(cmd);
    }
    void start_shell(const std::string &cmd) {
        if (running) return;
        add_line("$ " + cmd);
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
    void run(const std::string &cmd) {
        std::string c = cmd;
        while (!c.empty() && (c.front() == ' ' || c.front() == '\t')) c.erase(c.begin());
        if (c.empty()) return;
        // встроенные: только если нет '/' и нет пробела с аргументами в начале
        if (c.find('/') == std::string::npos) {
            std::string first = c.substr(0, c.find(' '));
            builtin_cmd(c);
            return;
        }
        start_shell(c);
    }
    void pump() {
        if (worker) {
            std::vector<std::string> got;
            { std::lock_guard<std::mutex> l(mtx); got.swap(pending); }
            for (auto &s : got) add_line(s);
            if (!running) { worker->join(); delete worker; worker = nullptr; }
        }
    }
    void update_suggestions() {
        // подсказка по последнему токену
        std::string last;
        size_t sp = input.find_last_of(" \t");
        last = (sp == std::string::npos) ? input : input.substr(sp + 1);
        suggestions = cmd_suggestions(last);
    }
    void complete() {
        if (suggestions.empty()) return;
        size_t sp = input.find_last_of(" \t");
        std::string pre = (sp == std::string::npos) ? "" : input.substr(0, sp + 1);
        input = pre + suggestions[0];
        update_suggestions();
    }
};