#pragma once
#include <stdint.h>

#define FAKEVR_SHMEM_NAME  "Local\\FakeVR_SharedMemory"
#define FAKEVR_SHMEM_SIZE  sizeof(FakeVRData)

struct FakeVRPose {
    float px, py, pz;        // position
    float ox, oy, oz, ow;    // orientation quaternion
};

struct FakeVRHand {
    FakeVRPose pose;
    float trigger;   // 0..1
    float grip;      // 0..1
    float thumbstick_x;
    float thumbstick_y;
    int   menu_button;   // bool
    int   a_button;
    int   b_button;
};

struct FakeVRData {
    uint32_t    magic;          // 0xFAE00001
    uint32_t    version;
    FakeVRPose  head;
    FakeVRHand  left;
    FakeVRHand  right;
    float       walk_x;         // locomotion from arrows
    float       walk_z;
    uint32_t    session_active; // companion sets 1 when running
    uint32_t    reserved[16];
};

#define FAKEVR_MAGIC 0xFAE00001
