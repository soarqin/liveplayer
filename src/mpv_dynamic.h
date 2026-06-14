#ifndef MPV_DYNAMIC_H
#define MPV_DYNAMIC_H

#include <windows.h>
#include <stdint.h>
#include <mpv/client.h>

typedef mpv_handle *(*MpvCreateFn)(void);
typedef int (*MpvInitializeFn)(mpv_handle *ctx);
typedef void (*MpvTerminateDestroyFn)(mpv_handle *ctx);
typedef int (*MpvSetOptionStringFn)(mpv_handle *ctx, const char *name, const char *data);
typedef int (*MpvCommandAsyncFn)(mpv_handle *ctx, uint64_t reply_userdata, const char **args);
typedef void (*MpvSetWakeupCallbackFn)(mpv_handle *ctx, void (*cb)(void *d), void *d);
typedef mpv_event *(*MpvWaitEventFn)(mpv_handle *ctx, double timeout);
typedef const char *(*MpvErrorStringFn)(int error);
typedef int (*MpvGetPropertyFn)(mpv_handle *ctx, const char *name, mpv_format format, void *data);

typedef struct {
    MpvCreateFn create;
    MpvInitializeFn initialize;
    MpvTerminateDestroyFn terminate_destroy;
    MpvSetOptionStringFn set_option_string;
    MpvCommandAsyncFn command_async;
    MpvSetWakeupCallbackFn set_wakeup_callback;
    MpvWaitEventFn wait_event;
    MpvErrorStringFn error_string;
    MpvGetPropertyFn get_property;
} MpvApi;

extern MpvApi g_mpv;

BOOL MpvLoad(void);
void MpvUnload(void);

#endif
