#include "mpv_dynamic.h"

#include <stdio.h>

MpvApi g_mpv = { 0 };
static HMODULE g_hMpv = NULL;

#define MPV_LOAD(member, ty, symbol) do { \
    g_mpv.member = (ty)GetProcAddress(g_hMpv, symbol); \
    if (!g_mpv.member) { \
        fprintf(stderr, "GetProcAddress failed: %s\n", symbol); \
        return FALSE; \
    } \
} while (0)

BOOL MpvLoad(void)
{
    if (g_hMpv) return TRUE;

    g_hMpv = LoadLibraryW(L"libmpv-2.dll");
    if (!g_hMpv) {
        fprintf(stderr, "Failed to load libmpv-2.dll (error %lu)\n", GetLastError());
        return FALSE;
    }

    MPV_LOAD(create, MpvCreateFn, "mpv_create");
    MPV_LOAD(initialize, MpvInitializeFn, "mpv_initialize");
    MPV_LOAD(terminate_destroy, MpvTerminateDestroyFn, "mpv_terminate_destroy");
    MPV_LOAD(set_option_string, MpvSetOptionStringFn, "mpv_set_option_string");
    MPV_LOAD(command_async, MpvCommandAsyncFn, "mpv_command_async");
    MPV_LOAD(set_wakeup_callback, MpvSetWakeupCallbackFn, "mpv_set_wakeup_callback");
    MPV_LOAD(wait_event, MpvWaitEventFn, "mpv_wait_event");
    MPV_LOAD(error_string, MpvErrorStringFn, "mpv_error_string");
    MPV_LOAD(get_property, MpvGetPropertyFn, "mpv_get_property");
    MPV_LOAD(request_log_messages, MpvRequestLogMessagesFn, "mpv_request_log_messages");
    MPV_LOAD(observe_property, MpvObservePropertyFn, "mpv_observe_property");

    return TRUE;
}

void MpvUnload(void)
{
    if (g_hMpv) {
        FreeLibrary(g_hMpv);
        g_hMpv = NULL;
    }
    ZeroMemory(&g_mpv, sizeof(g_mpv));
}
