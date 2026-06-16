/*
 * FakeVR Companion App
 * Reads mouse + keyboard, writes VR poses to shared memory.
 *
 * Controls:
 *   Tab           - toggle mouse capture
 *   F1            - control mode: HEAD (mouse = look)
 *   F2            - control mode: LEFT HAND
 *   F3            - control mode: RIGHT HAND
 *   Mouse move    - rotate selected object (yaw/pitch)
 *   Arrow keys    - locomotion (walk_x / walk_z fed to game)
 *   W/A/S/D       - move selected hand forward/back/left/right
 *   Q/E           - move hand up/down
 *   LMB           - trigger on selected hand (or both if head mode)
 *   RMB           - grip on selected hand
 *   Space         - A button
 *   Ctrl          - B button
 *   ESC           - exit
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "shared_mem.h"

// ── Math helpers ────────────────────────────────────────────────────────────
struct Vec3 { float x,y,z; };
struct Quat { float x,y,z,w; };

static Quat quat_mul(Quat a, Quat b) {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}
static Quat quat_from_yaw_pitch(float yaw, float pitch) {
    float cy = cosf(yaw*0.5f),   sy = sinf(yaw*0.5f);
    float cp = cosf(pitch*0.5f), sp = sinf(pitch*0.5f);
    Quat qy  = {0, sy, 0, cy};
    Quat qp  = {sp, 0, 0, cp};
    return quat_mul(qy, qp);
}
static Quat quat_norm(Quat q) {
    float n = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n < 1e-6f) return {0,0,0,1};
    return {q.x/n, q.y/n, q.z/n, q.w/n};
}
// Rotate vector by quaternion
static Vec3 quat_rotate(Quat q, Vec3 v) {
    float qx=q.x,qy=q.y,qz=q.z,qw=q.w;
    float ix =  qw*v.x + qy*v.z - qz*v.y;
    float iy =  qw*v.y + qz*v.x - qx*v.z;
    float iz =  qw*v.z + qx*v.y - qy*v.x;
    float iw = -qx*v.x - qy*v.y - qz*v.z;
    return { ix*qw+iw*(-qx)+iy*(-qz)-iz*(-qy),
             iy*qw+iw*(-qy)+iz*(-qx)-ix*(-qz),
             iz*qw+iw*(-qz)+ix*(-qy)-iy*(-qx) };
}

// ── App state ────────────────────────────────────────────────────────────────
static HWND      g_hwnd      = NULL;
static HANDLE    g_hMapFile  = NULL;
static FakeVRData* g_data    = NULL;
static bool      g_captured  = false;
static int       g_mode      = 0; // 0=head, 1=left, 2=right

// Euler angles for each object
static float g_headYaw   = 0, g_headPitch   = 0;
static float g_leftYaw   = 0, g_leftPitch   = 0;
static float g_rightYaw  = 0, g_rightPitch  = 0;

// Positions (world space)
static float g_headX=0,  g_headY=1.7f, g_headZ=0;
static float g_leftX=-0.3f,  g_leftY=1.2f,  g_leftZ=-0.4f;
static float g_rightX=0.3f, g_rightY=1.2f, g_rightZ=-0.4f;

static float g_walkX=0, g_walkZ=0;

static const float MOUSE_SENS  = 0.002f;
static const float HAND_SPEED  = 0.02f;
static const float WALK_SPEED  = 0.5f;  // m/s equivalent scale factor

// ── Shared memory ─────────────────────────────────────────────────────────
static bool shmem_init() {
    g_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, FAKEVR_SHMEM_NAME);
    if (!g_hMapFile) {
        g_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                         PAGE_READWRITE, 0,
                                         FAKEVR_SHMEM_SIZE, FAKEVR_SHMEM_NAME);
    }
    if (!g_hMapFile) return false;
    g_data = (FakeVRData*)MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, FAKEVR_SHMEM_SIZE);
    if (!g_data) return false;
    memset(g_data, 0, sizeof(*g_data));
    g_data->magic   = FAKEVR_MAGIC;
    g_data->version = 1;
    g_data->session_active = 1;
    return true;
}

static void shmem_write() {
    if (!g_data) return;

    auto write_pose = [](FakeVRPose& p, float x, float y, float z, Quat q) {
        p.px=x; p.py=y; p.pz=z;
        p.ox=q.x; p.oy=q.y; p.oz=q.z; p.ow=q.w;
    };

    Quat qh = quat_from_yaw_pitch(g_headYaw,  g_headPitch);
    Quat ql = quat_from_yaw_pitch(g_leftYaw,  g_leftPitch);
    Quat qr = quat_from_yaw_pitch(g_rightYaw, g_rightPitch);

    write_pose(g_data->head,       g_headX, g_headY, g_headZ, qh);
    write_pose(g_data->left.pose,  g_leftX, g_leftY, g_leftZ, ql);
    write_pose(g_data->right.pose, g_rightX,g_rightY,g_rightZ,qr);
    g_data->walk_x = g_walkX;
    g_data->walk_z = g_walkZ;
}

// ── Mouse capture ─────────────────────────────────────────────────────────
static void set_capture(bool cap) {
    g_captured = cap;
    if (cap) {
        SetCapture(g_hwnd);
        RECT rc; GetClientRect(g_hwnd, &rc);
        POINT centre = { (rc.right-rc.left)/2, (rc.bottom-rc.top)/2 };
        ClientToScreen(g_hwnd, &centre);
        SetCursorPos(centre.x, centre.y);
        ShowCursor(FALSE);
    } else {
        ReleaseCapture();
        ShowCursor(TRUE);
    }
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// ── Status text ──────────────────────────────────────────────────────────
static const wchar_t* mode_name(int m) {
    switch (m) { case 0: return L"HEAD"; case 1: return L"LEFT HAND"; case 2: return L"RIGHT HAND"; }
    return L"?";
}

// Forward declaration
static void on_timer();

// ── Window procedure ─────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == 1) { on_timer(); return 0; }
        return 0;

    case WM_DESTROY:
        if (g_data) g_data->session_active = 0;
        set_capture(false);
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN: {
        int vk = (int)wParam;
        if (vk == VK_ESCAPE)    { DestroyWindow(hwnd); return 0; }
        if (vk == VK_TAB)       { set_capture(!g_captured); return 0; }
        if (vk == VK_F1)        { g_mode = 0; InvalidateRect(hwnd,NULL,TRUE); return 0; }
        if (vk == VK_F2)        { g_mode = 1; InvalidateRect(hwnd,NULL,TRUE); return 0; }
        if (vk == VK_F3)        { g_mode = 2; InvalidateRect(hwnd,NULL,TRUE); return 0; }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_captured) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        int cx = (rc.right-rc.left)/2, cy = (rc.bottom-rc.top)/2;
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        int dx = mx - cx, dy = my - cy;
        if (dx == 0 && dy == 0) return 0;

        float dyaw   = -dx * MOUSE_SENS;
        float dpitch = -dy * MOUSE_SENS;

        auto clamp_pitch = [](float p) -> float {
            if (p >  1.5f) return  1.5f;
            if (p < -1.5f) return -1.5f;
            return p;
        };

        switch (g_mode) {
            case 0: g_headYaw   += dyaw; g_headPitch   = clamp_pitch(g_headPitch   + dpitch); break;
            case 1: g_leftYaw   += dyaw; g_leftPitch   = clamp_pitch(g_leftPitch   + dpitch); break;
            case 2: g_rightYaw  += dyaw; g_rightPitch  = clamp_pitch(g_rightPitch  + dpitch); break;
        }

        POINT centre = { cx, cy };
        ClientToScreen(hwnd, &centre);
        SetCursorPos(centre.x, centre.y);
        shmem_write();
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (g_data) {
            if (g_mode == 0 || g_mode == 1) g_data->left.trigger  = 1.0f;
            if (g_mode == 0 || g_mode == 2) g_data->right.trigger = 1.0f;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_data) { g_data->left.trigger = 0; g_data->right.trigger = 0; }
        return 0;
    }
    case WM_RBUTTONDOWN: {
        if (g_data) {
            if (g_mode == 0 || g_mode == 1) g_data->left.grip  = 1.0f;
            if (g_mode == 0 || g_mode == 2) g_data->right.grip = 1.0f;
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        if (g_data) { g_data->left.grip = 0; g_data->right.grip = 0; }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(20,20,30));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetTextColor(hdc, RGB(0, 220, 120));
        SetBkColor(hdc, RGB(20,20,30));

        wchar_t buf[512];
        int y = 10;
        auto line = [&](const wchar_t* txt) {
            TextOut(hdc, 10, y, txt, (int)wcslen(txt));
            y += 20;
        };

        line(L"═══ FakeVR Companion ═══");
        y += 5;
        swprintf(buf, 512, L"Mode: %s  [F1=Head F2=Left F3=Right]", mode_name(g_mode));
        line(buf);
        swprintf(buf, 512, L"Mouse Capture: %s  [F4 to toggle]", g_captured ? L"ON" : L"OFF");
        line(buf);
        y += 5;
        line(L"Head  - Mouse rotate when F1 active");
        line(L"Hands - WASD=move  QE=up/down");
        line(L"Walk  - Arrow keys");
        line(L"LMB=trigger  RMB=grip  Space=A  Ctrl=B  F4=capture mouse");
        y += 5;
        if (g_data) {
            swprintf(buf, 512, L"Head:  (%.2f, %.2f, %.2f)  yaw=%.1f°",
                     g_headX, g_headY, g_headZ, g_headYaw*57.3f);
            line(buf);
            swprintf(buf, 512, L"Left:  (%.2f, %.2f, %.2f)",
                     g_leftX, g_leftY, g_leftZ);
            line(buf);
            swprintf(buf, 512, L"Right: (%.2f, %.2f, %.2f)",
                     g_rightX, g_rightY, g_rightZ);
            line(buf);
            swprintf(buf, 512, L"Trigger L=%.2f R=%.2f  Grip L=%.2f R=%.2f",
                     g_data->left.trigger, g_data->right.trigger,
                     g_data->left.grip, g_data->right.grip);
            line(buf);
        }
        y += 5;
        line(L"ESC = Exit");

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_HOTKEY:
        if (wParam == 1) { set_capture(!g_captured); return 0; }
        return 0;

    case WM_ACTIVATE:
        // Don't release on focus loss - user switches to Roblox
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Timer tick — keyboard polling ────────────────────────────────────────
static void on_timer() {
    float hs = HAND_SPEED;
    float ws = WALK_SPEED * 0.016f; // ~60fps tick

    auto key = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

    // Walk (always active)
    g_walkX = 0; g_walkZ = 0;
    if (key(VK_LEFT))  g_walkX -= ws;
    if (key(VK_RIGHT)) g_walkX += ws;
    if (key(VK_UP))    g_walkZ -= ws;
    if (key(VK_DOWN))  g_walkZ += ws;

    // Hand movement
    float* hx = nullptr, *hy = nullptr, *hz = nullptr;
    switch (g_mode) {
        case 1: hx=&g_leftX;  hy=&g_leftY;  hz=&g_leftZ;  break;
        case 2: hx=&g_rightX; hy=&g_rightY; hz=&g_rightZ; break;
        default: break;
    }
    if (hx) {
        Quat q = quat_from_yaw_pitch(g_headYaw, 0);
        Vec3 fwd  = quat_rotate(q, {0,0,-1});
        Vec3 rgt  = quat_rotate(q, {1,0,0});
        if (key('W')) { *hx+=fwd.x*hs; *hy+=fwd.y*hs; *hz+=fwd.z*hs; }
        if (key('S')) { *hx-=fwd.x*hs; *hy-=fwd.y*hs; *hz-=fwd.z*hs; }
        if (key('A')) { *hx-=rgt.x*hs; *hy-=rgt.y*hs; *hz-=rgt.z*hs; }
        if (key('D')) { *hx+=rgt.x*hs; *hy+=rgt.y*hs; *hz+=rgt.z*hs; }
        if (key('Q')) *hy -= hs;
        if (key('E')) *hy += hs;
    }

    // Buttons
    if (g_data) {
        g_data->left.a_button  = key(VK_SPACE)   ? 1 : 0;
        g_data->left.b_button  = key(VK_CONTROL) ? 1 : 0;
        g_data->right.a_button = key(VK_SPACE)   ? 1 : 0;
        g_data->right.b_button = key(VK_CONTROL) ? 1 : 0;
    }

    shmem_write();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

// ── WinMain ──────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    if (!shmem_init()) {
        MessageBoxW(NULL, L"Failed to create shared memory!", L"FakeVR", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FakeVRCompanion";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_TOPMOST,
                              L"FakeVRCompanion", L"FakeVR Companion",
                              WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX,
                              100, 100, 480, 380,
                              NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Register F4 as global hotkey (works even when Roblox is focused)
    RegisterHotKey(g_hwnd, 1, 0, VK_F4);

    // 16ms timer for input polling
    SetTimer(g_hwnd, 1, 16, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_data)    { UnmapViewOfFile(g_data); }
    if (g_hMapFile) CloseHandle(g_hMapFile);
    return 0;
}
