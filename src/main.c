#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "mpv_dynamic.h"

#include "resource.h"

#define ID_LISTVIEW     100
#define ID_BTN_ADD      101
#define ID_BTN_DEL      102
#define ID_BTN_PLAY     103
#define ID_BTN_UP       104
#define ID_BTN_DOWN     105

#define WM_MPV_EVENT    (WM_USER + 1)

typedef struct {
    wchar_t *name;
    wchar_t *url;
} FavoriteItem;

typedef struct {
    mpv_handle *mpv;
    HWND hWnd;
    HWND hVideo;
    BOOL sizeAdjusted;
    BOOL isFullscreen;
    LONG_PTR savedStyle;
    WINDOWPLACEMENT savedPlacement;
    char errLog[8192];
    size_t errLogLen;
    BOOL handlingEvents;
} PlaybackContext;

typedef struct {
    HWND *items;
    size_t count;
    size_t capacity;
} HwndList;

typedef struct {
    wchar_t *name;
    size_t nameLen;
    wchar_t *url;
    size_t urlLen;
} AddDlgData;

/* 收藏列表数组（与 ListView 行一一对应） */
static FavoriteItem *g_favorites = NULL;
static int g_favCount = 0;
static int g_favCapacity = 0;

static struct {
    HWND hWnd;
    HWND hList;
    wchar_t configPath[MAX_PATH];
} g_main = { 0 };

static HINSTANCE g_hInst = NULL;
static HwndList g_playbackWindows = { 0 };

static HICON AppIconLoad(void)
{
    return LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
}

static char *WideToUtf8(const wchar_t *wstr)
{
    if (!wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *str = (char *)malloc(len);
    if (!str) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    return str;
}

static void TrimWhitespace(wchar_t *str)
{
    size_t len = wcslen(str);
    while (len > 0 && (str[len - 1] == L'\n' || str[len - 1] == L'\r' ||
                       str[len - 1] == L' ' || str[len - 1] == L'\t')) {
        str[--len] = L'\0';
    }
    wchar_t *start = str;
    while (*start == L' ' || *start == L'\t') start++;
    if (start != str) {
        memmove(str, start, (wcslen(start) + 1) * sizeof(wchar_t));
    }
}

static void TrimLineEnding(wchar_t *str)
{
    size_t len = wcslen(str);
    while (len > 0 && (str[len - 1] == L'\n' || str[len - 1] == L'\r')) {
        str[--len] = L'\0';
    }
}

static void GetConfigPath(wchar_t *out, size_t size)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        wcsncpy(out, L"favorites.txt", size);
        out[size - 1] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    wcsncpy(out, path, size - 1);
    out[size - 1] = L'\0';
    size_t remain = size - wcslen(out) - 1;
    wcsncat(out, L"favorites.txt", remain);
}

static void GetExeDirectory(wchar_t *out, size_t size)
{
    if (size == 0) return;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        out[0] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    wcsncpy(out, path, size - 1);
    out[size - 1] = L'\0';
}

/* ── Cookie 合并 ─────────────────────────────────────────── */

typedef struct {
    wchar_t *line;    /* 原始行内容（不含换行） */
    wchar_t *domain;  /* 第 0 字段，用于去重键 */
    wchar_t *path;    /* 第 2 字段，用于去重键 */
    wchar_t *name;    /* 第 5 字段，用于去重键 */
} CookieEntry;

static BOOL CookieEnsureCapacity(CookieEntry **entries, size_t *count, size_t *capacity)
{
    if (*count < *capacity) return TRUE;
    size_t newCap = *capacity ? *capacity * 2 : 16;
    CookieEntry *p = (CookieEntry *)realloc(*entries, newCap * sizeof(CookieEntry));
    if (!p) return FALSE;
    *entries = p;
    *capacity = newCap;
    return TRUE;
}

/* 读取 Netscape cookie 文件，按 (domain, path, name) 去重。
   后读取到的重复键会覆盖先读取到的键。注释行与格式错误的行保留原样。 */
static void ReadCookieFile(const wchar_t *path, CookieEntry **entries, size_t *count, size_t *capacity)
{
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return;

    wchar_t buf[4096];
    while (fgetws(buf, 4096, fp)) {
        TrimLineEnding(buf);
        if (buf[0] == L'\0') continue;

        wchar_t *lineCopy = _wcsdup(buf);
        if (!lineCopy) continue;

        /* 注释行直接保留 */
        if (buf[0] == L'#') {
            if (!CookieEnsureCapacity(entries, count, capacity)) { free(lineCopy); continue; }
            size_t idx = *count;
            (*entries)[idx].line = lineCopy;
            (*entries)[idx].domain = NULL;
            (*entries)[idx].path = NULL;
            (*entries)[idx].name = NULL;
            (*count)++;
            continue;
        }

        /* 解析制表符分隔字段：domain flag path secure expires name value */
        wchar_t parseBuf[4096];
        wcsncpy(parseBuf, buf, 4095);
        parseBuf[4095] = L'\0';

        wchar_t *fields[7] = { 0 };
        int nFields = 0;
        wchar_t *saveptr = NULL;
        wchar_t *token = wcstok(parseBuf, L"\t", &saveptr);
        while (token && nFields < 7) {
            fields[nFields++] = token;
            token = wcstok(NULL, L"\t", &saveptr);
        }

        /* 字段不足时保留原行但不去重 */
        if (nFields < 7) {
            if (!CookieEnsureCapacity(entries, count, capacity)) { free(lineCopy); continue; }
            size_t idx = *count;
            (*entries)[idx].line = lineCopy;
            (*entries)[idx].domain = NULL;
            (*entries)[idx].path = NULL;
            (*entries)[idx].name = NULL;
            (*count)++;
            continue;
        }

        /* 查找是否已存在相同 (domain, path, name) */
        int existing = -1;
        for (size_t i = 0; i < *count; i++) {
            CookieEntry *e = &(*entries)[i];
            if (e->domain && e->path && e->name &&
                wcscmp(e->domain, fields[0]) == 0 &&
                wcscmp(e->path, fields[2]) == 0 &&
                wcscmp(e->name, fields[5]) == 0) {
                existing = (int)i;
                break;
            }
        }

        if (existing >= 0) {
            free((*entries)[existing].line);
            (*entries)[existing].line = lineCopy;
        } else {
            if (!CookieEnsureCapacity(entries, count, capacity)) { free(lineCopy); continue; }
            size_t idx = *count;
            (*entries)[idx].line = lineCopy;
            (*entries)[idx].domain = _wcsdup(fields[0]);
            (*entries)[idx].path = _wcsdup(fields[2]);
            (*entries)[idx].name = _wcsdup(fields[5]);
            (*count)++;
        }
    }
    fclose(fp);
}

/* 启动时先把已有的 cookies_aio.txt 作为基础，再合并所有 *_cookies.txt。
   后读取到的重复 (domain, path, name) 覆盖先读取到的。合并完成后删除源文件。 */
static void MergeCookieFiles(void)
{
    wchar_t exeDir[MAX_PATH];
    GetExeDirectory(exeDir, MAX_PATH);
    if (exeDir[0] == L'\0') return;

    wchar_t destPath[MAX_PATH];
    if (_snwprintf(destPath, MAX_PATH, L"%scookies_aio.txt", exeDir) < 0) return;

    CookieEntry *entries = NULL;
    size_t count = 0, capacity = 0;

    /* 1. 先读取已有的 cookies_aio.txt 作为基础 */
    ReadCookieFile(destPath, &entries, &count, &capacity);

    /* 2. 收集 *_cookies.txt 源文件路径 */
    wchar_t **srcPaths = NULL;
    size_t srcCount = 0, srcCapacity = 0;

    wchar_t pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, L"%s*_cookies.txt", exeDir) >= 0) {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(pattern, &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                wchar_t srcPath[MAX_PATH];
                if (_snwprintf(srcPath, MAX_PATH, L"%s%s", exeDir, findData.cFileName) < 0) continue;
                if (_wcsicmp(srcPath, destPath) == 0) continue;

                if (srcCount == srcCapacity) {
                    size_t newCap = srcCapacity ? srcCapacity * 2 : 4;
                    wchar_t **p = (wchar_t **)realloc(srcPaths, newCap * sizeof(wchar_t *));
                    if (!p) continue;
                    srcPaths = p;
                    srcCapacity = newCap;
                }
                wchar_t *dup = _wcsdup(srcPath);
                if (dup) srcPaths[srcCount++] = dup;
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    if (srcCount == 0) {
        /* 没有新文件需要合并，保留现有 cookies_aio.txt 不动 */
        for (size_t i = 0; i < count; i++) {
            free(entries[i].line);
            free(entries[i].domain);
            free(entries[i].path);
            free(entries[i].name);
        }
        free(entries);
        free(srcPaths);
        return;
    }

    /* 3. 按任意顺序读取源文件（后读取的重复键覆盖前者） */
    for (size_t i = 0; i < srcCount; i++) {
        ReadCookieFile(srcPaths[i], &entries, &count, &capacity);
    }

    /* 4. 写回 cookies_aio.txt（yt-dlp 要求无 BOM 且使用本机换行符，Windows 为 \r\n） */
    FILE *dest = _wfopen(destPath, L"wb");
    if (!dest) {
        for (size_t i = 0; i < srcCount; i++) free(srcPaths[i]);
        free(srcPaths);
        for (size_t i = 0; i < count; i++) {
            free(entries[i].line);
            free(entries[i].domain);
            free(entries[i].path);
            free(entries[i].name);
        }
        free(entries);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        char *utf8 = WideToUtf8(entries[i].line);
        if (!utf8) continue;
        fwrite(utf8, 1, strlen(utf8), dest);
        fwrite("\r\n", 1, 2, dest);
        free(utf8);
    }
    fclose(dest);

    /* 5. 写入成功后才删除源文件 */
    for (size_t i = 0; i < srcCount; i++) {
        DeleteFileW(srcPaths[i]);
        free(srcPaths[i]);
    }
    free(srcPaths);

    for (size_t i = 0; i < count; i++) {
        free(entries[i].line);
        free(entries[i].domain);
        free(entries[i].path);
        free(entries[i].name);
    }
    free(entries);
}

static void EnsureDefaultMpvConfig(void)
{
    wchar_t exeDir[MAX_PATH];
    GetExeDirectory(exeDir, MAX_PATH);
    if (exeDir[0] == L'\0') return;

    wchar_t configPath[MAX_PATH];
    if (_snwprintf(configPath, MAX_PATH, L"%smpv.conf", exeDir) < 0) return;

    DWORD attr = GetFileAttributesW(configPath);
    if (attr != INVALID_FILE_ATTRIBUTES) return;

    FILE *fp = _wfopen(configPath, L"w");
    if (!fp) return;

    fwprintf(fp, L"# LivePlayer default mpv config\n");
    fwprintf(fp, L"# You can modify or delete this file; it will not be overwritten.\n");
    fwprintf(fp, L"vo=gpu\n");
    fwprintf(fp, L"hwdec=no\n");
    fwprintf(fp, L"keep-open=no\n");
    fwprintf(fp, L"ytdl=yes\n");
    fwprintf(fp, L"ytdl-format=best\n");
    fwprintf(fp, L"# Keyboard shortcuts (space/arrows/etc.) are handled by mpv itself.\n");
    fwprintf(fp, L"input-default-bindings=yes\n");
    fwprintf(fp, L"input-vo-keyboard=yes\n");
    fclose(fp);
}

/* 现代 UI 字体：Segoe UI 9pt（Vista+），回退到系统默认字体。
   字体句柄在进程生命周期内保持有效，程序退出时由 OS 回收。 */
static HFONT g_hFont = NULL;

static void CreateModernFont(void)
{
    /* 使用 SystemParametersInfo 获取系统 UI 字体（跟随用户设置） */
    NONCLIENTMETRICSW ncm = { 0 };
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    /* 回退：手动创建 Segoe UI 9pt */
    if (!g_hFont) {
        g_hFont = CreateFontW(
            -MulDiv(9, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
            L"Segoe UI");
    }
}

static void SetModernFont(HWND hWnd)
{
    if (g_hFont) {
        SendMessage(hWnd, WM_SETFONT, (WPARAM)g_hFont, MAKELPARAM(TRUE, 0));
    }
}

/* ── 收藏数组管理 ─────────────────────────────────────────── */

static BOOL FavArrayEnsure(void)
{
    if (g_favCount < g_favCapacity) return TRUE;
    int newCap = g_favCapacity ? g_favCapacity * 2 : 8;
    FavoriteItem *p = (FavoriteItem *)realloc(g_favorites, (size_t)newCap * sizeof(FavoriteItem));
    if (!p) return FALSE;
    g_favorites = p;
    g_favCapacity = newCap;
    return TRUE;
}

/* ── ListView 辅助 ────────────────────────────────────────── */

/* 向 ListView 末尾插入一行（仅 UI，不修改 g_favorites）。 */
static void LvAppendRow(int idx, const wchar_t *name, const wchar_t *url)
{
    LVITEMW lvi = { 0 };
    lvi.mask    = LVIF_TEXT;
    lvi.iItem   = idx;
    lvi.iSubItem = 0;
    lvi.pszText = (LPWSTR)name;
    ListView_InsertItem(g_main.hList, &lvi);
    ListView_SetItemText(g_main.hList, idx, 1, (LPWSTR)url);
}

/* 用 g_favorites[idx] 刷新 ListView 第 idx 行的两列文本。 */
static void LvRefreshRow(int idx)
{
    if (idx < 0 || idx >= g_favCount) return;
    ListView_SetItemText(g_main.hList, idx, 0, g_favorites[idx].name);
    ListView_SetItemText(g_main.hList, idx, 1, g_favorites[idx].url);
}

static int LvGetSelected(void)
{
    return ListView_GetNextItem(g_main.hList, -1, LVNI_SELECTED);
}

static void LvSetSelected(int idx)
{
    if (idx < 0 || idx >= g_favCount) return;
    ListView_SetItemState(g_main.hList, idx,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_main.hList, idx, FALSE);
}

/* ── 收藏列表管理（数组 + ListView 同步） ─────────────────── */

static void FavoriteAdd(const wchar_t *name, const wchar_t *url)
{
    if (!FavArrayEnsure()) return;
    wchar_t *n = _wcsdup(name);
    wchar_t *u = _wcsdup(url);
    if (!n || !u) { free(n); free(u); return; }
    int idx = g_favCount;
    g_favorites[idx].name = n;
    g_favorites[idx].url  = u;
    g_favCount++;
    LvAppendRow(idx, name, url);
}

static void FavoriteDelete(int idx)
{
    if (idx < 0 || idx >= g_favCount) return;
    free(g_favorites[idx].name);
    free(g_favorites[idx].url);
    /* 数组前移 */
    memmove(&g_favorites[idx], &g_favorites[idx + 1],
            (size_t)(g_favCount - idx - 1) * sizeof(FavoriteItem));
    g_favCount--;
    ListView_DeleteItem(g_main.hList, idx);
}

static void FavoriteClear(void)
{
    for (int i = 0; i < g_favCount; i++) {
        free(g_favorites[i].name);
        free(g_favorites[i].url);
    }
    g_favCount = 0;
    if (g_main.hList) ListView_DeleteAllItems(g_main.hList);
}

static const FavoriteItem *FavoriteGet(int idx)
{
    if (idx < 0 || idx >= g_favCount) return NULL;
    return &g_favorites[idx];
}

/* 交换数组中两个相邻项，并刷新 ListView 对应行文本。 */
static void FavoriteSwap(int a, int b)
{
    if (a < 0 || b < 0 || a >= g_favCount || b >= g_favCount) return;
    FavoriteItem tmp = g_favorites[a];
    g_favorites[a]   = g_favorites[b];
    g_favorites[b]   = tmp;
    LvRefreshRow(a);
    LvRefreshRow(b);
}

static void LoadFavorites(void)
{
    FILE *fp = _wfopen(g_main.configPath, L"r, ccs=UTF-8");
    if (!fp) return;

    wchar_t name[4096];
    wchar_t url[4096];
    while (fgetws(name, 4096, fp) && fgetws(url, 4096, fp)) {
        TrimWhitespace(name);
        TrimWhitespace(url);
        if (name[0] != L'\0' && url[0] != L'\0') {
            FavoriteAdd(name, url);
        }
    }
    fclose(fp);
}

static void SaveFavorites(void)
{
    FILE *fp = _wfopen(g_main.configPath, L"w, ccs=UTF-8");
    if (!fp) {
        MessageBoxW(g_main.hWnd, L"无法保存收藏列表。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    for (int i = 0; i < g_favCount; i++) {
        fwprintf(fp, L"%s\n%s\n", g_favorites[i].name, g_favorites[i].url);
    }
    fclose(fp);
}

/* ── 播放窗口管理 ─────────────────────────────────────────── */

static void PlaybackWindowAdd(HWND hWnd)
{
    if (g_playbackWindows.count == g_playbackWindows.capacity) {
        size_t newCap = g_playbackWindows.capacity ? g_playbackWindows.capacity * 2 : 4;
        HWND *newItems = (HWND *)realloc(g_playbackWindows.items, newCap * sizeof(HWND));
        if (!newItems) return;
        g_playbackWindows.items = newItems;
        g_playbackWindows.capacity = newCap;
    }
    g_playbackWindows.items[g_playbackWindows.count++] = hWnd;
}

static void PlaybackWindowRemove(HWND hWnd)
{
    for (size_t i = 0; i < g_playbackWindows.count; i++) {
        if (g_playbackWindows.items[i] == hWnd) {
            g_playbackWindows.items[i] = g_playbackWindows.items[--g_playbackWindows.count];
            return;
        }
    }
}

static void PlaybackWindowCloseAll(void)
{
    if (g_playbackWindows.count == 0) return;
    size_t n = g_playbackWindows.count;
    HWND *copy = (HWND *)malloc(n * sizeof(HWND));
    if (!copy) return;
    memcpy(copy, g_playbackWindows.items, n * sizeof(HWND));
    for (size_t i = 0; i < n; i++) {
        DestroyWindow(copy[i]);
    }
    free(copy);
}

/* ── mpv 相关 ─────────────────────────────────────────────── */

static void ShowMpvError(HWND hWnd, int err)
{
    const char *err_str = g_mpv.error_string(err);
    wchar_t msg[512];
    msg[0] = L'\0';
    if (err_str) {
        MultiByteToWideChar(CP_UTF8, 0, err_str, -1, msg, 512);
    } else {
        wcsncpy(msg, L"未知错误", 512);
    }
    MessageBoxW(hWnd, msg, L"解析/播放错误", MB_OK | MB_ICONERROR);
}

static void ShowMpvErrorMessage(HWND hWnd, const char *utf8)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return;
    wchar_t *wmsg = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wmsg) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wmsg, wlen);
    MessageBoxW(hWnd, wmsg, L"播放错误", MB_OK | MB_ICONERROR);
    free(wmsg);
}

static void AppendErrLog(PlaybackContext *ctx, const char *text)
{
    if (!text || !*text) return;
    size_t avail = sizeof(ctx->errLog) - 1 - ctx->errLogLen;
    if (avail == 0) return;
    size_t tlen = strlen(text);
    if (tlen > avail) tlen = avail;
    memcpy(ctx->errLog + ctx->errLogLen, text, tlen);
    ctx->errLogLen += tlen;
    ctx->errLog[ctx->errLogLen] = '\0';
}

static void SetPlaybackFullscreen(PlaybackContext *ctx, BOOL fullscreen)
{
    if (!ctx || !ctx->hWnd || fullscreen == ctx->isFullscreen) return;
    HWND hWnd = ctx->hWnd;
    if (fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        ctx->savedStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
        ctx->savedPlacement.length = sizeof(ctx->savedPlacement);
        GetWindowPlacement(hWnd, &ctx->savedPlacement);
        SetWindowLongPtr(hWnd, GWL_STYLE, ctx->savedStyle & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPos(hWnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        ctx->isFullscreen = TRUE;
    } else {
        SetWindowLongPtr(hWnd, GWL_STYLE, ctx->savedStyle);
        SetWindowPlacement(hWnd, &ctx->savedPlacement);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        ctx->isFullscreen = FALSE;
    }
}

static void OnMpvEvent(PlaybackContext *ctx)
{
    if (!ctx || !ctx->mpv) return;
    /* MessageBox 等模态对话框会运行内部消息循环，可能再次触发 WM_MPV_EVENT。
       若嵌套处理导致窗口被销毁（如 idle=no 时播放错误同时收到 SHUTDOWN），
       外层循环会在对话框返回后继续访问已释放的 ctx。使用标志防止重入。 */
    if (ctx->handlingEvents) return;
    ctx->handlingEvents = TRUE;

    while (1) {
        mpv_event *e = g_mpv.wait_event(ctx->mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;

        if (e->event_id == MPV_EVENT_SHUTDOWN) {
            ctx->handlingEvents = FALSE;
            DestroyWindow(ctx->hWnd);
            return;
        }

        if (e->event_id == MPV_EVENT_LOG_MESSAGE) {
            mpv_event_log_message *lm = (mpv_event_log_message *)e->data;
            if (lm) AppendErrLog(ctx, lm->text);
            continue;
        }

        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *pc = (mpv_event_property *)e->data;
            if (pc && pc->name && pc->data && pc->format == MPV_FORMAT_FLAG &&
                strcmp(pc->name, "fullscreen") == 0) {
                SetPlaybackFullscreen(ctx, (*(int *)pc->data) ? TRUE : FALSE);
            }
            continue;
        }

        if (e->event_id == MPV_EVENT_VIDEO_RECONFIG) {
            int64_t dwidth = 0;
            int64_t dheight = 0;
            int ok = g_mpv.get_property(ctx->mpv, "dwidth", MPV_FORMAT_INT64, &dwidth) >= 0 &&
                     g_mpv.get_property(ctx->mpv, "dheight", MPV_FORMAT_INT64, &dheight) >= 0;
            if (!ctx->sizeAdjusted && !ctx->isFullscreen && ok && dwidth > 0 && dheight > 0 &&
                dwidth <= 8192 && dheight <= 8192 &&
                ctx->hWnd && !IsIconic(ctx->hWnd) && !IsZoomed(ctx->hWnd)) {
                RECT rc = { 0, 0, (LONG)dwidth, (LONG)dheight };
                DWORD style = (DWORD)GetWindowLongPtr(ctx->hWnd, GWL_STYLE);
                DWORD exStyle = (DWORD)GetWindowLongPtr(ctx->hWnd, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, style, FALSE, exStyle);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                SetWindowPos(ctx->hWnd, NULL, 0, 0, w, h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                ctx->sizeAdjusted = TRUE;
            }
        }

        if (e->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file *ef = (mpv_event_end_file *)e->data;
            if (ef && (ef->reason == MPV_END_FILE_REASON_ERROR || ef->error < 0)) {
                if (ctx->errLogLen > 0) {
                    ShowMpvErrorMessage(ctx->hWnd, ctx->errLog);
                } else if (ef->error < 0) {
                    ShowMpvError(ctx->hWnd, ef->error);
                } else {
                    MessageBoxW(ctx->hWnd, L"视频播放结束或解析失败。",
                                L"解析/播放错误", MB_OK | MB_ICONERROR);
                }
                ctx->errLogLen = 0;
                ctx->errLog[0] = '\0';
            }
        }
    }
    ctx->handlingEvents = FALSE;
}

static void MpvWakeupCb(void *d)
{
    HWND hWnd = (HWND)d;
    if (hWnd && IsWindow(hWnd)) {
        PostMessageW(hWnd, WM_MPV_EVENT, 0, 0);
    }
}

static BOOL InitPlaybackMpv(PlaybackContext *ctx, HWND hVideo, const wchar_t *urlW)
{
    ctx->mpv = g_mpv.create();
    if (!ctx->mpv) return FALSE;

    g_mpv.request_log_messages(ctx->mpv, "error");

    char widStr[32];
    snprintf(widStr, sizeof(widStr), "%lld", (long long)(intptr_t)hVideo);
    g_mpv.set_option_string(ctx->mpv, "wid", widStr);

    g_mpv.set_option_string(ctx->mpv, "input-default-bindings", "yes");
    g_mpv.set_option_string(ctx->mpv, "input-vo-keyboard", "yes");
    g_mpv.set_option_string(ctx->mpv, "config", "yes");

    wchar_t exeDir[MAX_PATH];
    GetExeDirectory(exeDir, MAX_PATH);
    if (exeDir[0] != L'\0') {
        char *configDir = WideToUtf8(exeDir);
        if (configDir) {
            g_mpv.set_option_string(ctx->mpv, "config-dir", configDir);
            free(configDir);
        }
    }

    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exePath, L'\\');
        if (slash) slash[1] = L'\0';
        size_t remain = MAX_PATH - wcslen(exePath) - 1;
        wcsncat(exePath, L"yt-dlp.exe", remain);
        char *ytdlPath = WideToUtf8(exePath);
        if (ytdlPath) {
            g_mpv.set_option_string(ctx->mpv, "ytdl-path", ytdlPath);
            free(ytdlPath);
        }
    }

    /* 如果启动时合并生成了 cookies_aio.txt，则传给 mpv 用于 yt-dlp。 */
    if (exeDir[0] != L'\0') {
        wchar_t cookiePath[MAX_PATH];
        if (_snwprintf(cookiePath, MAX_PATH, L"%scookies_aio.txt", exeDir) >= 0) {
            DWORD attr = GetFileAttributesW(cookiePath);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                char *cookieDir = WideToUtf8(exeDir);
                if (cookieDir) {
                    char optVal[MAX_PATH * 4];
                    snprintf(optVal, sizeof(optVal), "cookies=%scookies_aio.txt", cookieDir);
                    g_mpv.set_option_string(ctx->mpv, "ytdl-raw-options", optVal);
                    free(cookieDir);
                }
            }
        }
    }

    g_mpv.set_wakeup_callback(ctx->mpv, MpvWakeupCb, ctx->hWnd);

    if (g_mpv.initialize(ctx->mpv) < 0) {
        g_mpv.terminate_destroy(ctx->mpv);
        ctx->mpv = NULL;
        return FALSE;
    }

    g_mpv.observe_property(ctx->mpv, 0, "fullscreen", MPV_FORMAT_FLAG);

    char *url = WideToUtf8(urlW);
    if (!url) {
        g_mpv.terminate_destroy(ctx->mpv);
        ctx->mpv = NULL;
        return FALSE;
    }
    const char *cmd[] = { "loadfile", url, NULL };
    g_mpv.command_async(ctx->mpv, 0, cmd);
    free(url);

    return TRUE;
}

/* ── 播放窗口 ─────────────────────────────────────────────── */

static void VkToMpvKeyName(UINT vk, BOOL shift, BOOL ctrl, BOOL alt, char *out, size_t outSize)
{
    out[0] = '\0';
    if (outSize == 0) return;

    char key[32] = {0};
    if (vk >= '0' && vk <= '9') {
        snprintf(key, sizeof(key), "%c", (char)vk);
    } else if (vk >= 'A' && vk <= 'Z') {
        if (shift && !ctrl && !alt) {
            snprintf(key, sizeof(key), "%c", (char)vk);
        } else {
            snprintf(key, sizeof(key), "%c", (char)(vk + 32));
        }
    } else {
        switch (vk) {
        case VK_SPACE:  strncpy(key, "SPACE", sizeof(key) - 1); break;
        case VK_RETURN: strncpy(key, "ENTER", sizeof(key) - 1); break;
        case VK_ESCAPE: strncpy(key, "ESC", sizeof(key) - 1); break;
        case VK_TAB:    strncpy(key, "TAB", sizeof(key) - 1); break;
        case VK_BACK:   strncpy(key, "BS", sizeof(key) - 1); break;
        case VK_DELETE: strncpy(key, "DEL", sizeof(key) - 1); break;
        case VK_LEFT:   strncpy(key, "LEFT", sizeof(key) - 1); break;
        case VK_RIGHT:  strncpy(key, "RIGHT", sizeof(key) - 1); break;
        case VK_UP:     strncpy(key, "UP", sizeof(key) - 1); break;
        case VK_DOWN:   strncpy(key, "DOWN", sizeof(key) - 1); break;
        case VK_HOME:   strncpy(key, "HOME", sizeof(key) - 1); break;
        case VK_END:    strncpy(key, "END", sizeof(key) - 1); break;
        case VK_PRIOR:  strncpy(key, "PGUP", sizeof(key) - 1); break;
        case VK_NEXT:   strncpy(key, "PGDWN", sizeof(key) - 1); break;
        default:
            if (vk >= VK_F1 && vk <= VK_F12) {
                snprintf(key, sizeof(key), "F%d", (int)(vk - VK_F1 + 1));
            }
            break;
        }
    }

    if (key[0] == '\0') return;

    char mods[32] = {0};
    if (ctrl)  strncat(mods, "ctrl+", sizeof(mods) - 1);
    if (alt)   strncat(mods, "alt+", sizeof(mods) - 1);
    if (shift) strncat(mods, "shift+", sizeof(mods) - 1);

    snprintf(out, outSize, "%s%s", mods, key);
}

static LRESULT CALLBACK PlaybackWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        const wchar_t *url = (const wchar_t *)cs->lpCreateParams;

        PlaybackContext *ctx = (PlaybackContext *)calloc(1, sizeof(PlaybackContext));
        if (!ctx) return -1;
        ctx->hWnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

        ctx->hVideo = CreateWindowW(L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
            0, 0, cs->cx, cs->cy, hWnd, NULL, g_hInst, NULL);

        if (!InitPlaybackMpv(ctx, ctx->hVideo, url)) {
            MessageBoxW(hWnd, L"初始化播放器失败。", L"错误", MB_OK | MB_ICONERROR);
        }

        PlaybackWindowAdd(hWnd);
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (ctx && ctx->mpv) {
            UINT vk = (UINT)wParam;
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
            char keyName[64];
            VkToMpvKeyName(vk, shift, ctrl, alt, keyName, sizeof(keyName));
            if (keyName[0] != '\0') {
                const char *cmd[] = { "keypress", keyName, NULL };
                g_mpv.command_async(ctx->mpv, 0, cmd);
            }
        }
        if (message == WM_SYSKEYDOWN) {
            return DefWindowProcW(hWnd, message, wParam, lParam);
        }
        break;
    }
    case WM_SIZE:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (ctx && ctx->hVideo) {
            MoveWindow(ctx->hVideo, 0, 0, w, h, TRUE);
        }
        break;
    }
    case WM_MPV_EVENT:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        OnMpvEvent(ctx);
        break;
    }
    case WM_DESTROY:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (ctx) {
            if (ctx->mpv) {
                g_mpv.set_wakeup_callback(ctx->mpv, NULL, NULL);
                g_mpv.terminate_destroy(ctx->mpv);
            }
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
            free(ctx);
        }
        PlaybackWindowRemove(hWnd);
        break;
    }
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void OpenPlaybackWindow(const wchar_t *name, const wchar_t *url)
{
    wchar_t title[512];
    _snwprintf(title, 512, L"LivePlayer - %s", name);

    HWND hWnd = CreateWindowW(L"LivePlayerVideoClass", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 960, 600,
        NULL, NULL, g_hInst, (LPVOID)url);

    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }
}

/* ── 主窗口 ───────────────────────────────────────────────── */

static void PlaySelected(void)
{
    int idx = LvGetSelected();
    if (idx < 0) return;
    const FavoriteItem *item = FavoriteGet(idx);
    if (!item) return;
    OpenPlaybackWindow(item->name, item->url);
}

static INT_PTR CALLBACK AddFavoriteDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static AddDlgData *data = NULL;
    switch (message) {
    case WM_INITDIALOG:
        data = (AddDlgData *)lParam;
        SetDlgItemTextW(hDlg, IDC_EDIT_NAME, L"");
        SetDlgItemTextW(hDlg, IDC_EDIT_URL, L"");
        {
            HICON hIcon = AppIconLoad();
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (data) {
                GetDlgItemTextW(hDlg, IDC_EDIT_NAME, data->name, (int)data->nameLen);
                GetDlgItemTextW(hDlg, IDC_EDIT_URL, data->url, (int)data->urlLen);
            }
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void OnAddFavorite(void)
{
    wchar_t name[4096] = { 0 };
    wchar_t url[4096] = { 0 };
    AddDlgData data = { name, 4096, url, 4096 };

    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_DIALOG_ADD), g_main.hWnd,
                        AddFavoriteDlgProc, (LPARAM)&data) != IDOK) {
        return;
    }

    TrimWhitespace(name);
    TrimWhitespace(url);

    if (name[0] == L'\0' || url[0] == L'\0') {
        MessageBoxW(g_main.hWnd, L"名称和 URL 都不能为空。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    FavoriteAdd(name, url);
    SaveFavorites();
    /* 选中新添加的行 */
    LvSetSelected(g_favCount - 1);
}

static void OnDelFavorite(void)
{
    int idx = LvGetSelected();
    if (idx < 0) return;
    if (MessageBoxW(g_main.hWnd, L"确定要删除选中的收藏吗？", L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    FavoriteDelete(idx);
    SaveFavorites();
    /* 删除后选中同位置（或最后一项） */
    if (g_favCount > 0) {
        LvSetSelected(idx < g_favCount ? idx : g_favCount - 1);
    }
}

static void OnMoveUp(void)
{
    int idx = LvGetSelected();
    if (idx <= 0) return;
    FavoriteSwap(idx, idx - 1);
    LvSetSelected(idx - 1);
    SaveFavorites();
}

static void OnMoveDown(void)
{
    int idx = LvGetSelected();
    if (idx < 0 || idx >= g_favCount - 1) return;
    FavoriteSwap(idx, idx + 1);
    LvSetSelected(idx + 1);
    SaveFavorites();
}

/* 初始化 ListView：添加两列并设置扩展样式。 */
static void InitListView(HWND hList, int totalWidth)
{
    ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int colName = totalWidth * 30 / 100;   /* 名称列 30% */
    int colUrl  = totalWidth - colName;    /* URL 列其余 */

    LVCOLUMNW col = { 0 };
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.iSubItem = 0;
    col.cx       = colName;
    col.pszText  = L"名称";
    ListView_InsertColumn(hList, 0, &col);

    col.iSubItem = 1;
    col.cx       = colUrl;
    col.pszText  = L"播放地址";
    ListView_InsertColumn(hList, 1, &col);
}

/* 工具栏高度常量：按钮垂直居中于 44px 区域，下方 1px 分隔线 */
#define TOOLBAR_H   44
#define TOOLBAR_SEP 1   /* 分隔线高度 */
#define LIST_TOP    (TOOLBAR_H + TOOLBAR_SEP + 6)

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
    {
        g_main.hWnd = hWnd;
        GetConfigPath(g_main.configPath, MAX_PATH);

        /* 按钮垂直居中于工具栏区域 */
        int btnH = 28;
        int btnY = (TOOLBAR_H - btnH) / 2;

        int x = 12;
        CreateWindowW(L"BUTTON", L"添加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, btnY, 64, btnH, hWnd, (HMENU)ID_BTN_ADD, g_hInst, NULL);
        x += 72;
        CreateWindowW(L"BUTTON", L"删除",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, btnY, 64, btnH, hWnd, (HMENU)ID_BTN_DEL, g_hInst, NULL);
        x += 72;
        CreateWindowW(L"BUTTON", L"播放",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, btnY, 64, btnH, hWnd, (HMENU)ID_BTN_PLAY, g_hInst, NULL);
        x += 80;   /* 播放后留稍大间距，视觉分组 */
        CreateWindowW(L"BUTTON", L"↑",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, btnY, 38, btnH, hWnd, (HMENU)ID_BTN_UP, g_hInst, NULL);
        x += 46;
        CreateWindowW(L"BUTTON", L"↓",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, btnY, 38, btnH, hWnd, (HMENU)ID_BTN_DOWN, g_hInst, NULL);

        /* ListView：紧贴分隔线下方，四边留 8px 边距 */
        g_main.hList = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            8, LIST_TOP, 644, 340, hWnd, (HMENU)ID_LISTVIEW, g_hInst, NULL);

        /* 设置现代字体 */
        HWND hChild = GetWindow(hWnd, GW_CHILD);
        while (hChild) {
            SetModernFont(hChild);
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }

        InitListView(g_main.hList, 644);
        LoadFavorites();
        break;
    }
    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (g_main.hList) {
            int lw = w - 16;
            int lh = h - LIST_TOP - 8;
            if (lw < 0) lw = 0;
            if (lh < 0) lh = 0;
            MoveWindow(g_main.hList, 8, LIST_TOP, lw, lh, TRUE);
            /* 调整列宽：名称 30%，URL 其余 */
            int inner = lw - GetSystemMetrics(SM_CXVSCROLL) - 2;
            if (inner > 0) {
                int colName = inner * 30 / 100;
                ListView_SetColumnWidth(g_main.hList, 0, colName);
                ListView_SetColumnWidth(g_main.hList, 1, inner - colName);
            }
        }
        /* 重绘工具栏区域（分隔线） */
        RECT rcBar = { 0, 0, LOWORD(lParam), TOOLBAR_H + TOOLBAR_SEP };
        InvalidateRect(hWnd, &rcBar, FALSE);
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        /* 工具栏背景：使用系统按钮面色（与 Visual Styles 一致） */
        RECT rcBar = { 0, 0, rcClient.right, TOOLBAR_H };
        HBRUSH hbrBar = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rcBar, hbrBar);

        /* 分隔线：单像素，颜色取系统 3D 阴影色 */
        RECT rcSep = { 0, TOOLBAR_H, rcClient.right, TOOLBAR_H + TOOLBAR_SEP };
        HBRUSH hbrSep = GetSysColorBrush(COLOR_BTNSHADOW);
        FillRect(hdc, &rcSep, hbrSep);

        EndPaint(hWnd, &ps);
        break;
    }
    case WM_ERASEBKGND:
    {
        /* 用 COLOR_WINDOW 填充 ListView 下方区域，避免闪烁 */
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        RECT rcBelow = { 0, TOOLBAR_H + TOOLBAR_SEP, rc.right, rc.bottom };
        FillRect(hdc, &rcBelow, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_BTN_ADD) {
            OnAddFavorite();
        } else if (id == ID_BTN_DEL) {
            OnDelFavorite();
        } else if (id == ID_BTN_PLAY) {
            PlaySelected();
        } else if (id == ID_BTN_UP) {
            OnMoveUp();
        } else if (id == ID_BTN_DOWN) {
            OnMoveDown();
        }
        break;
    }
    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lParam;
        if (nmhdr->idFrom == ID_LISTVIEW && nmhdr->code == NM_DBLCLK) {
            PlaySelected();
        }
        break;
    }
    case WM_DESTROY:
        FavoriteClear();
        free(g_favorites);
        g_favorites = NULL;
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
        PlaybackWindowCloseAll();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hInst = hInstance;

    if (!MpvLoad()) {
        MessageBoxW(NULL, L"无法加载 libmpv-2.dll，请确认 mpv 已安装并在 PATH 中。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    EnsureDefaultMpvConfig();
    MergeCookieFiles();

    /* 现代字体（在创建任何窗口之前初始化） */
    CreateModernFont();

    INITCOMMONCONTROLSEX iccex = { sizeof(iccex), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&iccex);

    /* 主窗口：背景设为 NULL，由 WM_ERASEBKGND / WM_PAINT 自行绘制，
       避免工具栏区域与列表区域颜色不一致的闪烁。 */
    WNDCLASSEXW wcexMain = { 0 };
    wcexMain.cbSize = sizeof(wcexMain);
    wcexMain.style = CS_HREDRAW | CS_VREDRAW;
    wcexMain.lpfnWndProc = MainWndProc;
    wcexMain.hInstance = hInstance;
    wcexMain.hIcon = AppIconLoad();
    wcexMain.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcexMain.hbrBackground = NULL;   /* 自绘背景 */
    wcexMain.lpszClassName = L"LivePlayerMainClass";
    wcexMain.hIconSm = AppIconLoad();

    if (!RegisterClassExW(&wcexMain)) {
        MessageBoxW(NULL, L"注册主窗口类失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wcexVideo = { 0 };
    wcexVideo.cbSize = sizeof(wcexVideo);
    wcexVideo.style = CS_HREDRAW | CS_VREDRAW;
    wcexVideo.lpfnWndProc = PlaybackWndProc;
    wcexVideo.hInstance = hInstance;
    wcexVideo.hIcon = AppIconLoad();
    wcexVideo.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcexVideo.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcexVideo.lpszClassName = L"LivePlayerVideoClass";
    wcexVideo.hIconSm = AppIconLoad();

    if (!RegisterClassExW(&wcexVideo)) {
        MessageBoxW(NULL, L"注册播放窗口类失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(L"LivePlayerMainClass", L"LivePlayer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 680, 480,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"创建窗口失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    MpvUnload();
    free(g_playbackWindows.items);
    return (int)msg.wParam;
}
