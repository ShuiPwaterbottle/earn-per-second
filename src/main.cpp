// ============================================================================
// EarnPerSecond — 每秒工资计算器
// ----------------------------------------------------------------------------
// 一个简单的 Win32 GUI 应用：
//   1. 设置月薪 / 日薪 / 时薪以及工作时间，自动算出每秒工资；
//   2. 点击「开始上班」后每秒刷新已赚到的钱（高精度计时）；
//   3. 随时可以暂停或下班结算，结算时自动汇总本次收入与累计收入。
//
// 技术要点：
//   - 纯 Win32 API，无第三方依赖，单个 .exe；
//   - 高精度计时使用 QueryPerformanceCounter，1 秒定时器负责刷新界面；
//   - 源码以 UTF-8 保存，运行时转换为 UTF-16 调用 Win32 API；
//   - 设置与累计收入持久化到 salary.ini（优先 exe 目录，其次 %APPDATA%）。
//
// 编译（MinGW-w64）：
//   g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ \
//       src/main.cpp -o EarnPerSecond.exe -luser32 -lgdi32 -lshell32
// 编译（MSVC，x64 Native Tools 命令提示符）：
//   cl /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE src\main.cpp ^
//      /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib
// ============================================================================

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// ---------------------------------------------------------------------------
// 文本编码辅助：窄字符串一律按 UTF-8 处理
// ---------------------------------------------------------------------------
static std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// ---------------------------------------------------------------------------
// 控件 ID
// ---------------------------------------------------------------------------
enum {
    IDC_RADIO_MONTHLY = 1001,
    IDC_RADIO_DAILY,
    IDC_RADIO_HOURLY,
    IDC_EDIT_AMOUNT,
    IDC_EDIT_HOURS,
    IDC_EDIT_DAYS,
    IDC_STATIC_RATE,
    IDC_BTN_SAVE,
    IDC_STATIC_MONEY,
    IDC_STATIC_RATE2,
    IDC_STATIC_TIME,
    IDC_STATIC_TOTAL,
    IDC_BTN_START,
    IDC_BTN_PAUSE,
    IDC_BTN_STOP,
    IDC_BTN_RESET
};

enum { TIMER_REFRESH = 1 };

// ---------------------------------------------------------------------------
// 应用状态
// ---------------------------------------------------------------------------
struct Config {
    int    type        = 0;      // 0=月薪, 1=日薪, 2=时薪
    double amount      = 8000.0; // 金额（元）
    double hours       = 8.0;    // 每天工作小时数
    double days        = 5.0;    // 每周工作天数
    double accumulated = 0.0;    // 累计收入（元）
};

enum WorkState { ST_IDLE = 0, ST_WORKING, ST_PAUSED };

static Config    g_cfg;
static WorkState g_state = ST_IDLE;
static double    g_rate  = 0.0;        // 本次上班快照的每秒工资
static double    g_lastEarned  = 0.0;  // 上一次结算的收入（下班后仍显示）
static double    g_lastElapsed = 0.0;  // 上一次结算的时长
static LARGE_INTEGER g_freq, g_sessionStart, g_pauseStart;
static double    g_pausedTotal = 0.0;  // 累计暂停秒数
static HWND      g_hwnd = nullptr;
static HFONT     g_fontBig = nullptr;

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------
static double nowSec() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)g_freq.QuadPart;
}

static double sessionElapsedSec() {
    if (g_state == ST_IDLE) return 0.0;
    double start = (double)g_sessionStart.QuadPart / (double)g_freq.QuadPart;
    double e = nowSec() - start - g_pausedTotal;
    return e < 0.0 ? 0.0 : e;
}

// 根据设置计算每秒工资
static double calcRate(const Config& c) {
    if (c.amount <= 0.0) return 0.0;
    if (c.type == 0) { // 月薪：每月工作秒数 = 每天小时 * 每周天数 * (52/12) * 3600
        double secs = c.hours * c.days * (52.0 / 12.0) * 3600.0;
        return secs > 0.0 ? c.amount / secs : 0.0;
    }
    if (c.type == 1) { // 日薪
        double secs = c.hours * 3600.0;
        return secs > 0.0 ? c.amount / secs : 0.0;
    }
    return c.amount / 3600.0; // 时薪
}

static std::string fmtMoney(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

static std::string fmtRate(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

static std::string fmtTime(double sec) {
    long long s = (long long)(sec + 0.5);
    long long h = s / 3600, m = (s % 3600) / 60, ss = s % 60;
    char buf[64];
    snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, ss);
    return buf;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) b--;
    // 去掉 UTF-8 BOM
    if (b - a >= 3 && (unsigned char)s[a] == 0xEF && (unsigned char)s[a+1] == 0xBB && (unsigned char)s[a+2] == 0xBF) a += 3;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------------------
// 配置读写（Win32 文件 API，宽字符路径，内容为 UTF-8）
// ---------------------------------------------------------------------------
static std::wstring configPathW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L'\\');
    std::wstring dir = (pos == std::wstring::npos) ? L"" : p.substr(0, pos + 1);

    std::wstring cand = dir + L"salary.ini";
    HANDLE h = CreateFileW(cand.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return cand;
    }
    // 回退到 %APPDATA%\EarnPerSecond
    wchar_t ap[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, ap))) {
        std::wstring dir2 = std::wstring(ap) + L"\\EarnPerSecond";
        CreateDirectoryW(dir2.c_str(), nullptr);
        return dir2 + L"\\salary.ini";
    }
    return cand;
}

static std::string readAllTextW(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string out;
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) out.append(buf, rd);
    CloseHandle(h);
    return out;
}

static bool writeAllTextW(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == text.size();
}

static void loadConfig() {
    std::string data = readAllTextW(configPathW());
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        std::string line = trim(data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = (nl == std::string::npos) ? data.size() : nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (k == "type")            g_cfg.type        = (int)atof(v.c_str());
        else if (k == "amount")     g_cfg.amount      = atof(v.c_str());
        else if (k == "hours")      g_cfg.hours       = atof(v.c_str());
        else if (k == "days")       g_cfg.days        = atof(v.c_str());
        else if (k == "accumulated") g_cfg.accumulated = atof(v.c_str());
    }
    if (g_cfg.amount <= 0) g_cfg.amount = 8000.0;
    if (g_cfg.hours  <= 0) g_cfg.hours  = 8.0;
    if (g_cfg.days   <= 0) g_cfg.days   = 5.0;
}

static void saveConfig() {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "[earn-per-second]\n"
             "type=%d\namount=%.6f\nhours=%.6f\ndays=%.6f\naccumulated=%.6f\n",
             g_cfg.type, g_cfg.amount, g_cfg.hours, g_cfg.days, g_cfg.accumulated);
    writeAllTextW(configPathW(), buf);
}

// ---------------------------------------------------------------------------
// 控件辅助
// ---------------------------------------------------------------------------
static void setText(HWND h, const std::string& s) { SetWindowTextW(h, wide(s).c_str()); }
static std::string getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return "";
    std::wstring w(n + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(n);
    return utf8(w);
}

static HWND mk(HWND parent, const wchar_t* cls, const std::string& text, DWORD style,
               int id, int x, int y, int w, int h, HFONT font) {
    HWND hc = CreateWindowExW(0, cls, wide(text).c_str(),
                              style | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              x, y, w, h, parent, (HMENU)(INT_PTR)id,
                              GetModuleHandleW(nullptr), nullptr);
    if (hc && font) SendMessageW(hc, WM_SETFONT, (WPARAM)font, TRUE);
    return hc;
}

// 读取设置区控件 -> g_cfg，并做基本校验
static bool readSettingsFromControls() {
    g_cfg.type = SendMessageW(GetDlgItem(g_hwnd, IDC_RADIO_DAILY), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 :
                 (SendMessageW(GetDlgItem(g_hwnd, IDC_RADIO_HOURLY), BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 0);
    g_cfg.amount = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_AMOUNT)).c_str());
    g_cfg.hours  = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_HOURS)).c_str());
    g_cfg.days   = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_DAYS)).c_str());
    return g_cfg.amount > 0.0 && g_cfg.hours > 0.0 && g_cfg.days > 0.0;
}

// 刷新“每秒工资”预览（跟随控件实时变化）
static void updateRatePreview() {
    Config c = g_cfg;
    c.type = SendMessageW(GetDlgItem(g_hwnd, IDC_RADIO_DAILY), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 :
             (SendMessageW(GetDlgItem(g_hwnd, IDC_RADIO_HOURLY), BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 0);
    c.amount = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_AMOUNT)).c_str());
    c.hours  = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_HOURS)).c_str());
    c.days   = atof(getText(GetDlgItem(g_hwnd, IDC_EDIT_DAYS)).c_str());
    double r = calcRate(c);
    setText(GetDlgItem(g_hwnd, IDC_STATIC_RATE),
            "每秒工资 ≈ ¥" + fmtRate(r) + "　｜　每小时 ≈ ¥" + fmtMoney(r * 3600.0));
}

// 刷新工作区显示（金额 / 时长 / 累计 / 按钮状态）
static void updateWorkUI() {
    bool idle = (g_state == ST_IDLE);
    double earned = idle ? g_lastEarned : g_rate * sessionElapsedSec();

    setText(GetDlgItem(g_hwnd, IDC_STATIC_MONEY), "¥" + fmtMoney(earned));
    setText(GetDlgItem(g_hwnd, IDC_STATIC_RATE2), "每秒 ¥" + fmtRate(g_rate));
    if (idle) {
        setText(GetDlgItem(g_hwnd, IDC_STATIC_TIME),
                g_lastElapsed > 0.0
                    ? "已下班 · 本次工作 " + fmtTime(g_lastElapsed) + "，收入 ¥" + fmtMoney(g_lastEarned)
                    : "已工作 00:00:00");
    } else {
        setText(GetDlgItem(g_hwnd, IDC_STATIC_TIME),
                (g_state == ST_PAUSED ? "已暂停 · 累计工作 " : "已工作 ")
                + fmtTime(sessionElapsedSec()));
    }
    setText(GetDlgItem(g_hwnd, IDC_STATIC_TOTAL), "累计收入：¥" + fmtMoney(g_cfg.accumulated));

    bool working = (g_state == ST_WORKING), paused = (g_state == ST_PAUSED);
    EnableWindow(GetDlgItem(g_hwnd, IDC_BTN_START), !working);   // 空闲/暂停时可用
    EnableWindow(GetDlgItem(g_hwnd, IDC_BTN_PAUSE), working);    // 仅工作时可暂停
    EnableWindow(GetDlgItem(g_hwnd, IDC_BTN_STOP),  working || paused);
    setText(GetDlgItem(g_hwnd, IDC_BTN_START), paused ? "继续上班" : "开始上班");

    if (working || paused) {
        SetWindowTextW(g_hwnd, wide("已赚 ¥" + fmtMoney(earned) + " · 每秒工资").c_str());
    } else {
        SetWindowTextW(g_hwnd, L"每秒工资 · Earn Per Second");
    }
}

// ---------------------------------------------------------------------------
// 会话控制
// ---------------------------------------------------------------------------
static void startSession() {
    if (!readSettingsFromControls()) {
        MessageBoxW(g_hwnd, L"请先填写正确的工资设置：\n金额、每天工作小时、每周工作天数都要大于 0。",
                    L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    g_rate = calcRate(g_cfg);
    if (g_rate <= 0.0) {
        MessageBoxW(g_hwnd, L"每秒工资为 0，请检查设置。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    g_state = ST_WORKING;
    g_pausedTotal = 0.0;
    g_lastEarned = 0.0;
    g_lastElapsed = 0.0;
    QueryPerformanceCounter(&g_sessionStart);
    SetTimer(g_hwnd, TIMER_REFRESH, 1000, nullptr);
    updateWorkUI();
}

static void pauseSession() {
    if (g_state != ST_WORKING) return;
    g_state = ST_PAUSED;
    QueryPerformanceCounter(&g_pauseStart);
    KillTimer(g_hwnd, TIMER_REFRESH);
    updateWorkUI();
}

static void resumeSession() {
    if (g_state != ST_PAUSED) return;
    g_pausedTotal += nowSec() - (double)g_pauseStart.QuadPart / (double)g_freq.QuadPart;
    g_state = ST_WORKING;
    SetTimer(g_hwnd, TIMER_REFRESH, 1000, nullptr);
    updateWorkUI();
}

static void stopSession() {
    if (g_state == ST_IDLE) return;
    double elapsed = sessionElapsedSec();
    double earned  = g_rate * elapsed;
    g_lastEarned  = earned;
    g_lastElapsed = elapsed;
    g_cfg.accumulated += earned;
    g_state = ST_IDLE;
    g_pausedTotal = 0.0;
    KillTimer(g_hwnd, TIMER_REFRESH);
    saveConfig();

    std::string msg = "本次工作：" + fmtTime(elapsed) + "\n"
                    + "本次收入：¥" + fmtMoney(earned) + "\n"
                    + "累计收入：¥" + fmtMoney(g_cfg.accumulated) + "\n\n"
                    + "辛苦啦，下班快乐！";
    MessageBoxW(g_hwnd, wide(msg).c_str(), L"下班结算", MB_OK | MB_ICONINFORMATION);
    updateWorkUI();
}

static void resetAccumulated() {
    if (g_cfg.accumulated <= 0.0) return;
    int r = MessageBoxW(g_hwnd, L"确定要把累计收入清零吗？此操作不可撤销。",
                        L"重置累计收入", MB_YESNO | MB_ICONQUESTION);
    if (r == IDYES) {
        g_cfg.accumulated = 0.0;
        saveConfig();
        updateWorkUI();
    }
}

// ---------------------------------------------------------------------------
// 界面创建
// ---------------------------------------------------------------------------
static void createControls(HWND hwnd) {
    HFONT fontCtrl = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_fontBig = CreateFontW(-38, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HFONT fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

    // ---- 工资设置 ----
    mk(hwnd, L"BUTTON", "工资设置", BS_GROUPBOX, 0, 10, 10, 470, 150, fontSmall);
    mk(hwnd, L"BUTTON", "月薪", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, IDC_RADIO_MONTHLY, 30, 32, 60, 22, fontCtrl);
    mk(hwnd, L"BUTTON", "日薪", BS_AUTORADIOBUTTON | WS_TABSTOP, IDC_RADIO_DAILY, 95, 32, 60, 22, fontCtrl);
    mk(hwnd, L"BUTTON", "时薪", BS_AUTORADIOBUTTON | WS_TABSTOP, IDC_RADIO_HOURLY, 160, 32, 60, 22, fontCtrl);
    mk(hwnd, L"STATIC", "金额（元）：", SS_LEFT, 0, 30, 64, 90, 20, fontCtrl);
    mk(hwnd, L"EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_EDIT_AMOUNT, 100, 62, 130, 23, fontCtrl);
    mk(hwnd, L"STATIC", "每天工作(小时)：", SS_LEFT, 0, 30, 94, 105, 20, fontCtrl);
    mk(hwnd, L"EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_EDIT_HOURS, 135, 92, 50, 23, fontCtrl);
    mk(hwnd, L"STATIC", "每周工作(天)：", SS_LEFT, 0, 200, 94, 95, 20, fontCtrl);
    mk(hwnd, L"EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_EDIT_DAYS, 295, 92, 50, 23, fontCtrl);
    mk(hwnd, L"STATIC", "", SS_LEFT, IDC_STATIC_RATE, 30, 124, 280, 20, fontCtrl);
    mk(hwnd, L"BUTTON", "保存设置", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_SAVE, 345, 120, 100, 26, fontCtrl);

    // ---- 上班计时 ----
    mk(hwnd, L"BUTTON", "上班计时", BS_GROUPBOX, 0, 10, 170, 470, 250, fontSmall);
    mk(hwnd, L"STATIC", "¥0.00", SS_CENTER | SS_CENTERIMAGE, IDC_STATIC_MONEY, 25, 195, 420, 62, g_fontBig);
    mk(hwnd, L"STATIC", "", SS_CENTER, IDC_STATIC_RATE2, 25, 262, 420, 18, fontCtrl);
    mk(hwnd, L"STATIC", "", SS_CENTER, IDC_STATIC_TIME, 25, 288, 420, 18, fontCtrl);
    mk(hwnd, L"BUTTON", "开始上班", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_START, 60, 330, 100, 34, fontCtrl);
    mk(hwnd, L"BUTTON", "暂停", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_PAUSE, 185, 330, 100, 34, fontCtrl);
    mk(hwnd, L"BUTTON", "下班结算", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_STOP, 310, 330, 100, 34, fontCtrl);
    mk(hwnd, L"STATIC", "", SS_LEFT, IDC_STATIC_TOTAL, 30, 384, 300, 20, fontCtrl);
    mk(hwnd, L"BUTTON", "重置累计", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_RESET, 345, 380, 100, 26, fontCtrl);

    // ---- 使用说明 ----
    mk(hwnd, L"BUTTON", "使用说明", BS_GROUPBOX, 0, 10, 430, 470, 130, fontSmall);
    mk(hwnd, L"STATIC", "① 填写工资设置（月薪/日薪/时薪 + 工作时间），点击「保存设置」", SS_LEFT, 0, 30, 452, 22, 18, fontSmall);
    mk(hwnd, L"STATIC", "② 点击「开始上班」，每秒自动刷新已赚到的钱", SS_LEFT, 0, 30, 474, 22, 18, fontSmall);
    mk(hwnd, L"STATIC", "③ 「暂停」可随时暂停；「下班结算」汇总本次收入并计入累计", SS_LEFT, 0, 30, 496, 22, 18, fontSmall);
    mk(hwnd, L"STATIC", "④ 设置与累计收入自动保存到 salary.ini，下次启动自动恢复", SS_LEFT, 0, 30, 518, 22, 18, fontSmall);

    // ---- 底部信息 ----
    mk(hwnd, L"STATIC", "EarnPerSecond v1.0.0 · MIT License · 开源项目",
       SS_CENTER, 0, 10, 582, 470, 18, fontSmall);
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        createControls(hwnd);
        // 载入配置并同步到控件
        loadConfig();
        SendMessageW(GetDlgItem(hwnd, IDC_RADIO_MONTHLY), BM_SETCHECK,
                     g_cfg.type == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_RADIO_DAILY), BM_SETCHECK,
                     g_cfg.type == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_RADIO_HOURLY), BM_SETCHECK,
                     g_cfg.type == 2 ? BST_CHECKED : BST_UNCHECKED, 0);
        setText(GetDlgItem(hwnd, IDC_EDIT_AMOUNT), fmtMoney(g_cfg.amount));
        setText(GetDlgItem(hwnd, IDC_EDIT_HOURS), fmtMoney(g_cfg.hours));
        setText(GetDlgItem(hwnd, IDC_EDIT_DAYS), fmtMoney(g_cfg.days));
        updateRatePreview();
        updateWorkUI();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case IDC_RADIO_MONTHLY:
        case IDC_RADIO_DAILY:
        case IDC_RADIO_HOURLY:
            updateRatePreview();
            break;
        case IDC_EDIT_AMOUNT:
        case IDC_EDIT_HOURS:
        case IDC_EDIT_DAYS:
            if (HIWORD(wp) == EN_CHANGE) updateRatePreview();
            break;
        case IDC_BTN_SAVE:
            if (!readSettingsFromControls()) {
                MessageBoxW(hwnd, L"设置无效：金额、每天工作小时、每周工作天数都要大于 0。",
                            L"提示", MB_OK | MB_ICONWARNING);
            } else {
                saveConfig();
                MessageBoxW(hwnd, L"设置已保存到 salary.ini。", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            break;
        case IDC_BTN_START:
            if (g_state == ST_PAUSED) resumeSession();
            else startSession();
            break;
        case IDC_BTN_PAUSE:
            pauseSession();
            break;
        case IDC_BTN_STOP:
            stopSession();
            break;
        case IDC_BTN_RESET:
            resetAccumulated();
            break;
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_REFRESH && g_state == ST_WORKING) updateWorkUI();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 506;
        mmi->ptMinTrackSize.y = 649;
        mmi->ptMaxTrackSize.x = 506;
        mmi->ptMaxTrackSize.y = 649;
        return 0;
    }
    case WM_DESTROY:
        if (g_state != ST_IDLE) stopSession(); // 关窗时自动结算
        saveConfig();
        if (g_fontBig) DeleteObject(g_fontBig);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    QueryPerformanceFrequency(&g_freq);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EarnPerSecondWnd";
    wc.hIconSm       = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, 490, 610};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExW(0, L"EarnPerSecondWnd", L"每秒工资 · Earn Per Second",
                                style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
