// Loader negotiation structures for OpenXR API Layers
// Based on OpenXR Spec 1.0 loader interface specification
#pragma once
#include "openxr.h"

#define XR_CURRENT_LOADER_API_LAYER_VERSION 1
#define XR_API_LAYER_INFO_STRUCT_VERSION    1

typedef enum XrLoaderInterfaceStructs {
    XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED         = 0,
    XR_LOADER_INTERFACE_STRUCT_LOADER_INFO          = 1,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST    = 2,
    XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST      = 3,
    XR_LOADER_INTERFACE_STRUCT_NEGOTIATION_INFO     = 4,
} XrLoaderInterfaceStructs;

typedef struct XrNegotiateLoaderInfo {
    XrLoaderInterfaceStructs  structType;
    uint32_t                  structVersion;
    size_t                    structSize;
    uint32_t                  minInterfaceVersion;
    uint32_t                  maxInterfaceVersion;
    XrVersion                 minApiVersion;
    XrVersion                 maxApiVersion;
} XrNegotiateLoaderInfo;

typedef XrResult (XRAPI_PTR *PFN_xrCreateApiLayerInstance)(
    const XrInstanceCreateInfo*, const void*, XrInstance*);

typedef struct XrNegotiateApiLayerRequest {
    XrLoaderInterfaceStructs  structType;
    uint32_t                  structVersion;
    size_t                    structSize;
    uint32_t                  layerInterfaceVersion;
    XrVersion                 layerApiVersion;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr;
    PFN_xrCreateApiLayerInstance createApiLayerInstance;
} XrNegotiateApiLayerRequest;
