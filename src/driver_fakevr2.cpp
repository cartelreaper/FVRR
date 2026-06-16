/*
 * FakeVR OpenVR Driver v2
 * Virtual HMD + two controllers, tracking data from shared memory.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include "openvr_driver.h"
#include "../shared_mem.h"
using namespace vr;

// ── Log ────────────────────────────────────────────────────────────────────
static FILE* g_log = nullptr;
static void log_open() {
    char p[MAX_PATH]; GetTempPathA(MAX_PATH,p); strcat_s(p,"fakevr_driver.log");
    fopen_s(&g_log,p,"w");
}
static void log_msg(const char* fmt,...) {
    if(!g_log) return; va_list v; va_start(v,fmt);
    vfprintf(g_log,fmt,v); fprintf(g_log,"\n"); fflush(g_log); va_end(v);
}

// ── Shared memory ──────────────────────────────────────────────────────────
static HANDLE g_hMap=NULL; static FakeVRData* g_data=NULL;
static void shmem_init() {
    g_hMap=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,FAKEVR_SHMEM_NAME);
    if(!g_hMap) g_hMap=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,FAKEVR_SHMEM_SIZE,FAKEVR_SHMEM_NAME);
    if(g_hMap){ g_data=(FakeVRData*)MapViewOfFile(g_hMap,FILE_MAP_ALL_ACCESS,0,0,FAKEVR_SHMEM_SIZE);
        if(g_data&&g_data->magic!=FAKEVR_MAGIC){
            memset(g_data,0,sizeof(*g_data)); g_data->magic=FAKEVR_MAGIC; g_data->version=1;
            g_data->head.py=1.7f; g_data->head.ow=1.0f;
            g_data->left.pose.px=-0.3f; g_data->left.pose.py=1.2f; g_data->left.pose.pz=-0.4f; g_data->left.pose.ow=1.0f;
            g_data->right.pose.px=0.3f; g_data->right.pose.py=1.2f; g_data->right.pose.pz=-0.4f; g_data->right.pose.ow=1.0f;
        } log_msg("shmem OK");
    }
}

static DriverPose_t make_pose(const FakeVRPose& p) {
    DriverPose_t d={0};
    d.poseIsValid=true; d.deviceIsConnected=true; d.result=TrackingResult_Running_OK;
    d.qRotation={p.ox,p.oy,p.oz,p.ow};
    d.vecPosition[0]=p.px; d.vecPosition[1]=p.py; d.vecPosition[2]=p.pz;
    d.qWorldFromDriverRotation={0,0,0,1}; d.qDriverFromHeadRotation={0,0,0,1};
    return d;
}
static DriverPose_t default_pose() {
    DriverPose_t d={0}; d.poseIsValid=true; d.deviceIsConnected=true;
    d.result=TrackingResult_Running_OK; d.qRotation={0,0,0,1};
    d.qWorldFromDriverRotation={0,0,0,1}; d.qDriverFromHeadRotation={0,0,0,1};
    return d;
}

// ── HMD ────────────────────────────────────────────────────────────────────
class FakeHMD : public ITrackedDeviceServerDriver, public IVRDisplayComponent {
public:
    TrackedDeviceIndex_t m_idx=k_unTrackedDeviceIndexInvalid;
    PropertyContainerHandle_t m_container=k_ulInvalidPropertyContainer;

    EVRInitError Activate(TrackedDeviceIndex_t idx) override {
        m_idx=idx;
        m_container=VRProperties()->TrackedDeviceToPropertyContainer(idx);
        log_msg("HMD Activate idx=%u container=%llu",idx,(unsigned long long)m_container);
        auto p=VRProperties();
        p->SetStringProperty(m_container,Prop_ManufacturerName_String,"FakeVR");
        p->SetStringProperty(m_container,Prop_ModelNumber_String,"FakeVR HMD");
        p->SetStringProperty(m_container,Prop_SerialNumber_String,"FAKEVR_HMD_001");
        p->SetStringProperty(m_container,Prop_TrackingSystemName_String,"fakevr2");
        p->SetFloatProperty(m_container,Prop_UserIpdMeters_Float,0.063f);
        p->SetFloatProperty(m_container,Prop_DisplayFrequency_Float,90.f);
        p->SetFloatProperty(m_container,Prop_SecondsFromVsyncToPhotons_Float,0.011f);
        p->SetBoolProperty(m_container,Prop_IsOnDesktop_Bool,false);
        p->SetBoolProperty(m_container,Prop_HasCamera_Bool,false);
        p->SetUint64Property(m_container,Prop_CurrentUniverseId_Uint64,2);
        return VRInitError_None;
    }
    void Deactivate() override { m_idx=k_unTrackedDeviceIndexInvalid; }
    void EnterStandby() override {}
    void* GetComponent(const char* n) override {
        if(!strcmp(n,IVRDisplayComponent_Version)) return (IVRDisplayComponent*)this;
        return nullptr;
    }
    void DebugRequest(const char*,char* r,uint32_t s) override { if(s) r[0]=0; }
    DriverPose_t GetPose() override { return g_data?make_pose(g_data->head):default_pose(); }

    // IVRDisplayComponent
    void GetWindowBounds(int32_t* x,int32_t* y,uint32_t* w,uint32_t* h) override
        { *x=0;*y=0;*w=1920;*h=1080; }
    bool IsDisplayOnDesktop() override { return false; }
    bool IsDisplayRealDisplay() override { return false; }
    void GetRecommendedRenderTargetSize(uint32_t* w,uint32_t* h) override { *w=1920;*h=1080; }
    void GetEyeOutputViewport(EVREye eye,uint32_t* x,uint32_t* y,uint32_t* w,uint32_t* h) override {
        *y=0;*w=960;*h=1080; *x=(eye==Eye_Left)?0:960;
    }
    void GetProjectionRaw(EVREye,float* l,float* r,float* t,float* b) override
        { *l=-1.f;*r=1.f;*t=-1.f;*b=1.f; }
    DistortionCoordinates_t ComputeDistortion(EVREye,float u,float v) override {
        DistortionCoordinates_t d;
        d.rfRed[0]=u;d.rfRed[1]=v;d.rfGreen[0]=u;d.rfGreen[1]=v;d.rfBlue[0]=u;d.rfBlue[1]=v;
        return d;
    }
    bool ComputeInverseDistortion(HmdVector2_t* r,EVREye,uint32_t,float u,float v) override {
        r->v[0]=u; r->v[1]=v; return true;
    }
};

// ── Controller ─────────────────────────────────────────────────────────────
class FakeController : public ITrackedDeviceServerDriver {
public:
    TrackedDeviceIndex_t m_idx=k_unTrackedDeviceIndexInvalid;
    PropertyContainerHandle_t m_container=k_ulInvalidPropertyContainer;
    bool m_isLeft;
    VRInputComponentHandle_t m_trigger=0,m_grip=0,m_a=0,m_b=0,m_menu=0;
    VRInputComponentHandle_t m_tx=0,m_ty=0,m_haptic=0;

    FakeController(bool left):m_isLeft(left){}

    EVRInitError Activate(TrackedDeviceIndex_t idx) override {
        m_idx=idx;
        m_container=VRProperties()->TrackedDeviceToPropertyContainer(idx);
        log_msg("Controller %s Activate idx=%u",m_isLeft?"Left":"Right",idx);
        auto p=VRProperties();
        p->SetStringProperty(m_container,Prop_ManufacturerName_String,"FakeVR");
        p->SetStringProperty(m_container,Prop_ModelNumber_String,m_isLeft?"FakeVR Left":"FakeVR Right");
        p->SetStringProperty(m_container,Prop_SerialNumber_String,m_isLeft?"FAKEVR_L_001":"FAKEVR_R_001");
        p->SetStringProperty(m_container,Prop_TrackingSystemName_String,"fakevr2");
        p->SetInt32Property(m_container,Prop_ControllerRoleHint_Int32,
            m_isLeft?TrackedControllerRole_LeftHand:TrackedControllerRole_RightHand);

        auto inp=VRDriverInput();
        inp->CreateBooleanComponent(m_container,"/input/trigger/click",&m_trigger);
        inp->CreateBooleanComponent(m_container,"/input/grip/click",&m_grip);
        inp->CreateBooleanComponent(m_container,"/input/application_menu/click",&m_menu);
        inp->CreateBooleanComponent(m_container,"/input/a/click",&m_a);
        inp->CreateBooleanComponent(m_container,"/input/b/click",&m_b);
        inp->CreateScalarComponent(m_container,"/input/trackpad/x",&m_tx,VRScalarType_Absolute,VRScalarUnits_NormalizedTwoSided);
        inp->CreateScalarComponent(m_container,"/input/trackpad/y",&m_ty,VRScalarType_Absolute,VRScalarUnits_NormalizedTwoSided);
        inp->CreateHapticComponent(m_container,"/output/haptic",&m_haptic);
        return VRInitError_None;
    }
    void Deactivate() override { m_idx=k_unTrackedDeviceIndexInvalid; }
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*,char* r,uint32_t s) override { if(s) r[0]=0; }
    DriverPose_t GetPose() override {
        if(g_data){ auto& h=m_isLeft?g_data->left:g_data->right; return make_pose(h.pose); }
        return default_pose();
    }
    void Update() {
        if(!g_data||m_idx==k_unTrackedDeviceIndexInvalid) return;
        auto& h=m_isLeft?g_data->left:g_data->right;
        auto inp=VRDriverInput();
        inp->UpdateBooleanComponent(m_trigger,h.trigger>0.5f,0);
        inp->UpdateBooleanComponent(m_grip,h.grip>0.5f,0);
        inp->UpdateBooleanComponent(m_a,h.a_button!=0,0);
        inp->UpdateBooleanComponent(m_b,h.b_button!=0,0);
        inp->UpdateScalarComponent(m_tx,h.thumbstick_x,0);
        inp->UpdateScalarComponent(m_ty,h.thumbstick_y,0);
        VRServerDriverHost()->TrackedDevicePoseUpdated(m_idx,GetPose(),sizeof(DriverPose_t));
    }
};

// ── Provider ───────────────────────────────────────────────────────────────
static FakeHMD g_hmd;
static FakeController g_left(true),g_right(false);

class FakeVRProvider : public IServerTrackedDeviceProvider {
public:
    EVRInitError Init(IVRDriverContext* ctx) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
        log_open(); log_msg("FakeVR2 Driver Init"); shmem_init();
        VRServerDriverHost()->TrackedDeviceAdded("FAKEVR_HMD_001",TrackedDeviceClass_HMD,&g_hmd);
        VRServerDriverHost()->TrackedDeviceAdded("FAKEVR_L_001",TrackedDeviceClass_Controller,&g_left);
        VRServerDriverHost()->TrackedDeviceAdded("FAKEVR_R_001",TrackedDeviceClass_Controller,&g_right);
        log_msg("Devices added OK"); return VRInitError_None;
    }
    void Cleanup() override {
        log_msg("Cleanup");
        if(g_data){UnmapViewOfFile(g_data);g_data=nullptr;}
        if(g_hMap){CloseHandle(g_hMap);g_hMap=nullptr;}
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    const char* const* GetInterfaceVersions() override { return k_InterfaceVersions; }
    void RunFrame() override {
        if(g_hmd.m_idx!=k_unTrackedDeviceIndexInvalid)
            VRServerDriverHost()->TrackedDevicePoseUpdated(g_hmd.m_idx,g_hmd.GetPose(),sizeof(DriverPose_t));
        g_left.Update(); g_right.Update();
    }
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {} void LeaveStandby() override {}
};
static FakeVRProvider g_provider;

extern "C" __declspec(dllexport)
void* HmdDriverFactory(const char* iface,int* err) {
    log_msg("HmdDriverFactory: %s",iface);
    if(!strcmp(iface,IServerTrackedDeviceProvider_Version)) return &g_provider;
    if(err) *err=VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD r,LPVOID){ if(r==DLL_PROCESS_ATTACH) log_open(); return TRUE; }
