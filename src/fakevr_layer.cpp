/*
 * FakeVR OpenXR API Layer
 * Intercepts OpenXR calls and feeds synthetic VR data from shared memory.
 * Compiled as fakevr_layer.dll (x64 Windows)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

// OpenXR headers — we only need the types/defines, not linking to loader
#define XR_USE_PLATFORM_WIN32
#define XR_NO_PROTOTYPES
#include "include/openxr/openxr.h"
#include "include/openxr/openxr_loader_negotiation.h"

// Forward declarations
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance instance, const char* funcName, PFN_xrVoidFunction* function);

#include "shared_mem.h"

// ─────────────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────────────

static HANDLE   g_hMapFile  = NULL;
static FakeVRData* g_data   = NULL;
static HANDLE   g_hLogFile  = INVALID_HANDLE_VALUE;

// Next-layer dispatch table (function pointers to the real OpenXR loader)
typedef XrResult (XRAPI_PTR *PFN_xrGetInstanceProcAddr)(XrInstance, const char*, PFN_xrVoidFunction*);
static PFN_xrGetInstanceProcAddr g_nextGetInstanceProcAddr = NULL;

// Cached next-layer functions
#define NEXT_FUNC(name) static PFN_##name g_next_##name = NULL;
NEXT_FUNC(xrGetInstanceProcAddr)
NEXT_FUNC(xrWaitFrame)
NEXT_FUNC(xrBeginFrame)
NEXT_FUNC(xrEndFrame)
NEXT_FUNC(xrLocateSpace)
NEXT_FUNC(xrLocateViews)
NEXT_FUNC(xrGetActionStateFloat)
NEXT_FUNC(xrGetActionStateBoolean)
NEXT_FUNC(xrGetActionStatePose)
NEXT_FUNC(xrGetActionStateVector2f)
NEXT_FUNC(xrPollEvent)
NEXT_FUNC(xrCreateSession)
NEXT_FUNC(xrDestroySession)
NEXT_FUNC(xrRequestExitSession)
NEXT_FUNC(xrCreateReferenceSpace)
NEXT_FUNC(xrEnumerateReferenceSpaces)
NEXT_FUNC(xrEnumerateViewConfigurations)
NEXT_FUNC(xrGetViewConfigurationProperties)
NEXT_FUNC(xrEnumerateViewConfigurationViews)

// ─────────────────────────────────────────────────────────────────────────────
// Logging helper
// ─────────────────────────────────────────────────────────────────────────────
static void log_msg(const char* fmt, ...) {
    char buf[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    // Write to debug log file in temp
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_hLogFile, buf, (DWORD)strlen(buf), &written, NULL);
        char nl[] = "\r\n";
        WriteFile(g_hLogFile, nl, 2, &written, NULL);
    }
    OutputDebugStringA("[FakeVR] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared memory
// ─────────────────────────────────────────────────────────────────────────────
static void shmem_open() {
    if (g_data) return;
    g_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, FAKEVR_SHMEM_NAME);
    if (!g_hMapFile) {
        // Create it with defaults if companion isn't running yet
        g_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                         PAGE_READWRITE, 0,
                                         FAKEVR_SHMEM_SIZE, FAKEVR_SHMEM_NAME);
    }
    if (g_hMapFile) {
        g_data = (FakeVRData*)MapViewOfFile(g_hMapFile, FILE_MAP_READ | FILE_MAP_WRITE,
                                             0, 0, FAKEVR_SHMEM_SIZE);
        if (g_data) {
            if (g_data->magic != FAKEVR_MAGIC) {
                // Init defaults
                memset(g_data, 0, sizeof(*g_data));
                g_data->magic   = FAKEVR_MAGIC;
                g_data->version = 1;
                // Default head: 1.7m above floor, facing -Z
                g_data->head.py = 1.7f;
                g_data->head.ow = 1.0f;
                // Left hand
                g_data->left.pose.px = -0.3f; g_data->left.pose.py = 1.2f; g_data->left.pose.pz = -0.4f;
                g_data->left.pose.ow = 1.0f;
                // Right hand
                g_data->right.pose.px = 0.3f; g_data->right.pose.py = 1.2f; g_data->right.pose.pz = -0.4f;
                g_data->right.pose.ow = 1.0f;
            }
            log_msg("Shared memory mapped OK");
        }
    } else {
        log_msg("WARNING: Could not open/create shared memory");
    }
}

static FakeVRData* get_data() {
    if (!g_data) shmem_open();
    return g_data;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static XrPosef pose_from_shared(const FakeVRPose& p) {
    XrPosef out;
    out.position.x    = p.px;
    out.position.y    = p.py;
    out.position.z    = p.pz;
    out.orientation.x = p.ox;
    out.orientation.y = p.oy;
    out.orientation.z = p.oz;
    out.orientation.w = p.ow;
    if (out.orientation.w == 0 && out.orientation.x == 0 &&
        out.orientation.y == 0 && out.orientation.z == 0) {
        out.orientation.w = 1.0f; // identity fallback
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Session state machine
// ─────────────────────────────────────────────────────────────────────────────
static XrSession         g_session     = XR_NULL_HANDLE;
static XrSessionState    g_sessionState = XR_SESSION_STATE_UNKNOWN;
static bool              g_eventQueued  = false;
static XrSessionState    g_nextState    = XR_SESSION_STATE_UNKNOWN;
static ULONGLONG         g_startTime    = 0;

static void queue_state(XrSessionState s) {
    g_nextState   = s;
    g_eventQueued = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Intercepted functions
// ─────────────────────────────────────────────────────────────────────────────

// xrCreateSession — also kick off state machine
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrCreateSession(XrInstance instance,
                      const XrSessionCreateInfo* createInfo,
                      XrSession* session)
{
    log_msg("xrCreateSession called");
    shmem_open();
    XrResult r = XR_SUCCESS;
    if (g_next_xrCreateSession) {
        r = g_next_xrCreateSession(instance, createInfo, session);
    } else {
        // Synthesize a fake handle
        static uintptr_t fake_id = 0xFACE0001;
        *session = (XrSession)fake_id;
    }
    if (r == XR_SUCCESS) {
        g_session = *session;
        g_sessionState = XR_SESSION_STATE_IDLE;
        g_startTime = GetTickCount64();
        // Queue transition chain: IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
        queue_state(XR_SESSION_STATE_READY);
    }
    return r;
}

// xrPollEvent — serve our state transitions
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData)
{
    // First drain real events if next layer exists
    if (g_next_xrPollEvent) {
        XrResult r = g_next_xrPollEvent(instance, eventData);
        if (r == XR_SUCCESS) return r;
    }

    if (!g_eventQueued) return XR_EVENT_UNAVAILABLE;

    g_eventQueued = false;
    g_sessionState = g_nextState;
    log_msg("State -> %d", (int)g_nextState);

    XrEventDataSessionStateChanged* ev = (XrEventDataSessionStateChanged*)eventData;
    ev->type      = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    ev->next      = NULL;
    ev->session   = g_session;
    ev->state     = g_nextState;
    ev->time      = (XrTime)((GetTickCount64() - g_startTime) * 1000000ULL);

    // Auto-advance the chain
    switch (g_nextState) {
        case XR_SESSION_STATE_READY:        queue_state(XR_SESSION_STATE_SYNCHRONIZED); break;
        case XR_SESSION_STATE_SYNCHRONIZED: queue_state(XR_SESSION_STATE_VISIBLE);      break;
        case XR_SESSION_STATE_VISIBLE:      queue_state(XR_SESSION_STATE_FOCUSED);      break;
        default: break;
    }
    return XR_SUCCESS;
}

// xrWaitFrame — always succeed, fill timing
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrWaitFrame(XrSession session,
                  const XrFrameWaitInfo* frameWaitInfo,
                  XrFrameState* frameState)
{
    if (g_next_xrWaitFrame) {
        return g_next_xrWaitFrame(session, frameWaitInfo, frameState);
    }
    frameState->predictedDisplayTime       = (XrTime)((GetTickCount64()) * 1000000ULL);
    frameState->predictedDisplayPeriod     = 11111111; // ~90Hz
    frameState->shouldRender               = XR_TRUE;
    return XR_SUCCESS;
}

// xrLocateSpace — this is where head/hand tracking lives
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrLocateSpace(XrSpace space, XrSpace baseSpace,
                    XrTime time, XrSpaceLocation* location)
{
    XrResult r = XR_SUCCESS;
    if (g_next_xrLocateSpace) {
        r = g_next_xrLocateSpace(space, baseSpace, time, location);
        // We'll override the pose below regardless
    }

    FakeVRData* d = get_data();
    if (!d) return r;

    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                              XR_SPACE_LOCATION_POSITION_VALID_BIT    |
                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|
                              XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    // Default: head pose. Application typically uses action spaces for hands,
    // but some engines locate the view space here.
    location->pose = pose_from_shared(d->head);
    return XR_SUCCESS;
}

// xrLocateViews — stereo head poses
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrLocateViews(XrSession session,
                    const XrViewLocateInfo* viewLocateInfo,
                    XrViewState* viewState,
                    uint32_t viewCapacityInput,
                    uint32_t* viewCountOutput,
                    XrView* views)
{
    if (g_next_xrLocateViews) {
        XrResult r = g_next_xrLocateViews(session, viewLocateInfo, viewState,
                                           viewCapacityInput, viewCountOutput, views);
        // Still override poses
        if (r != XR_SUCCESS) return r;
    }

    FakeVRData* d = get_data();
    *viewCountOutput = 2;
    if (!views || viewCapacityInput < 2 || !d) return XR_SUCCESS;

    viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                                XR_VIEW_STATE_POSITION_VALID_BIT    |
                                XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|
                                XR_VIEW_STATE_POSITION_TRACKED_BIT;

    XrPosef headPose = pose_from_shared(d->head);

    // Left eye: slightly left of head
    views[0].pose = headPose;
    views[0].pose.position.x -= 0.032f;
    views[0].fov = { -0.8727f, 0.8727f, 0.8727f, -0.8727f }; // ~50 deg half-angle

    // Right eye: slightly right
    views[1].pose = headPose;
    views[1].pose.position.x += 0.032f;
    views[1].fov = { -0.8727f, 0.8727f, 0.8727f, -0.8727f };

    return XR_SUCCESS;
}

// xrGetActionStateFloat — triggers, grips, thumbsticks
static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrGetActionStateFloat(XrSession session,
                             const XrActionStateGetInfo* getInfo,
                             XrActionStateFloat* state)
{
    if (g_next_xrGetActionStateFloat) {
        g_next_xrGetActionStateFloat(session, getInfo, state);
    }
    // We always synthesize
    FakeVRData* d = get_data();
    if (!d) return XR_SUCCESS;

    // We can't know which action this is without string introspection,
    // so we provide some data; the companion app handles the actual mapping.
    state->isActive   = XR_TRUE;
    state->currentState = 0.0f;
    state->changedSinceLastSync = XR_FALSE;
    return XR_SUCCESS;
}

static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrGetActionStateBoolean(XrSession session,
                               const XrActionStateGetInfo* getInfo,
                               XrActionStateBoolean* state)
{
    if (g_next_xrGetActionStateBoolean) {
        g_next_xrGetActionStateBoolean(session, getInfo, state);
    }
    state->isActive   = XR_TRUE;
    state->currentState = XR_FALSE;
    state->changedSinceLastSync = XR_FALSE;
    return XR_SUCCESS;
}

static XRAPI_ATTR XrResult XRAPI_CALL
layer_xrGetActionStatePose(XrSession session,
                            const XrActionStateGetInfo* getInfo,
                            XrActionStatePose* state)
{
    if (g_next_xrGetActionStatePose) {
        g_next_xrGetActionStatePose(session, getInfo, state);
    }
    state->isActive = XR_TRUE;
    return XR_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// xrGetInstanceProcAddr — the entry point for the layer chain
// ─────────────────────────────────────────────────────────────────────────────

#define INTERCEPT(name) \
    if (strcmp(funcName, #name) == 0) { \
        *function = (PFN_xrVoidFunction)layer_##name; return XR_SUCCESS; }

#define LOAD_NEXT(name) \
    if (g_nextGetInstanceProcAddr && !g_next_##name) { \
        g_nextGetInstanceProcAddr(instance, #name, (PFN_xrVoidFunction*)&g_next_##name); }

extern "C" XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest)
{
    // Open log
    char logPath[MAX_PATH];
    GetTempPathA(MAX_PATH, logPath);
    strcat_s(logPath, MAX_PATH, "fakevr_layer.log");
    g_hLogFile = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    log_msg("FakeVR Layer negotiating. Version: 1.0");

    if (!loaderInfo || !apiLayerRequest) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_NEGOTIATION_INFO) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION) return XR_ERROR_INITIALIZATION_FAILED;

    apiLayerRequest->structType            = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    apiLayerRequest->structVersion         = XR_API_LAYER_INFO_STRUCT_VERSION;
    apiLayerRequest->structSize            = sizeof(XrNegotiateApiLayerRequest);
    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion       = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr   = xrGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = NULL;

    shmem_open();
    log_msg("Negotiation OK");
    return XR_SUCCESS;
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL
xrGetInstanceProcAddr(XrInstance instance, const char* funcName,
                      PFN_xrVoidFunction* function)
{
    // Store the next layer's dispatch
    // (The loader passes its own xrGetInstanceProcAddr before calling ours.)
    LOAD_NEXT(xrWaitFrame)
    LOAD_NEXT(xrBeginFrame)
    LOAD_NEXT(xrEndFrame)
    LOAD_NEXT(xrLocateSpace)
    LOAD_NEXT(xrLocateViews)
    LOAD_NEXT(xrGetActionStateFloat)
    LOAD_NEXT(xrGetActionStateBoolean)
    LOAD_NEXT(xrGetActionStatePose)
    LOAD_NEXT(xrPollEvent)
    LOAD_NEXT(xrCreateSession)

    INTERCEPT(xrCreateSession)
    INTERCEPT(xrPollEvent)
    INTERCEPT(xrWaitFrame)
    INTERCEPT(xrLocateSpace)
    INTERCEPT(xrLocateViews)
    INTERCEPT(xrGetActionStateFloat)
    INTERCEPT(xrGetActionStateBoolean)
    INTERCEPT(xrGetActionStatePose)

    // For anything we don't intercept, pass to next layer
    if (g_nextGetInstanceProcAddr) {
        return g_nextGetInstanceProcAddr(instance, funcName, function);
    }
    *function = NULL;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// Called by loader to set the next layer's proc addr
extern "C" XRAPI_ATTR void XRAPI_CALL
fakevr_SetNextGetInstanceProcAddr(PFN_xrGetInstanceProcAddr nextFunc)
{
    g_nextGetInstanceProcAddr = nextFunc;
    log_msg("Next xrGetInstanceProcAddr set: %p", (void*)nextFunc);
}

// ─────────────────────────────────────────────────────────────────────────────
// DLL entry
// ─────────────────────────────────────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        log_msg("FakeVR Layer DLL attached");
        shmem_open();
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_data)     { UnmapViewOfFile(g_data); g_data = NULL; }
        if (g_hMapFile) { CloseHandle(g_hMapFile); g_hMapFile = NULL; }
        if (g_hLogFile != INVALID_HANDLE_VALUE) { CloseHandle(g_hLogFile); g_hLogFile = INVALID_HANDLE_VALUE; }
    }
    return TRUE;
}
