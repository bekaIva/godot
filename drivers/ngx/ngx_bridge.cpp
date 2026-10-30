/**************************************************************************/
/*  ngx_bridge.cpp                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// Every NGX call, for Vulkan and Direct3D 12. Built against NVIDIA's DLSS SDK
// (its include/ folder) and Vulkan's headers only, without Godot's, so that
// MSVC can build it on its own as patina_ngx.dll (ngx_bridge.h):
//   cl /O2 /MT /EHsc /LD /DNGX_BRIDGE_BUILD_DLL /I<SDK>/include
//      /I<godot>/thirdparty/vulkan/include ngx_bridge.cpp
//      /link <SDK>/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib /OUT:patina_ngx.dll

#include "ngx_bridge.h"

// Only Vulkan's types: the engine hands over its own vkGetInstanceProcAddr.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace {

enum API {
	API_NONE,
	API_VULKAN,
	API_D3D12,
};

struct State {
	API api = API_NONE;
	void *device = nullptr; // VkDevice or ID3D12Device *.
	NVSDK_NGX_Parameter *parameters = nullptr;
	// NGX keeps pointers to these.
	NVSDK_NGX_FeatureCommonInfo common_info = {};
	NVSDK_NGX_FeatureDiscoveryInfo discovery = {};
};

State state;

void setup(const NGXBridgeSetup *p_setup) {
	state.common_info = {};
	state.common_info.PathListInfo.Path = p_setup->library_paths;
	state.common_info.PathListInfo.Length = p_setup->library_path_count;
	state.common_info.LoggingInfo.MinimumLoggingLevel = p_setup->logging ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;

	state.discovery = {};
	state.discovery.SDKVersion = NVSDK_NGX_Version_API;
	state.discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
	state.discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
	state.discovery.Identifier.v.ProjectDesc.ProjectId = p_setup->project_id;
	state.discovery.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
	state.discovery.Identifier.v.ProjectDesc.EngineVersion = p_setup->engine_version;
	state.discovery.ApplicationDataPath = p_setup->data_path;
	state.discovery.FeatureInfo = &state.common_info;
}

uint32_t get_capability_parameters() {
	NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
	if (state.api == API_VULKAN) {
		result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&state.parameters);
	}
#ifdef _WIN32
	if (state.api == API_D3D12) {
		result = NVSDK_NGX_D3D12_GetCapabilityParameters(&state.parameters);
	}
#endif
	if (NVSDK_NGX_FAILED(result)) {
		state.parameters = nullptr;
		ngx_bridge_shutdown();
	}
	return result;
}

// DLSS's quality modes differ in the range of render sizes they take: the
// first mode whose range holds the render size, best quality first.
NVSDK_NGX_PerfQuality_Value dlss_quality(const NGXBridgeDLSS *p_params) {
	if (p_params->render_width >= p_params->output_width) {
		return NVSDK_NGX_PerfQuality_Value_DLAA;
	}
	static const NVSDK_NGX_PerfQuality_Value modes[] = {
		NVSDK_NGX_PerfQuality_Value_MaxQuality,
		NVSDK_NGX_PerfQuality_Value_Balanced,
		NVSDK_NGX_PerfQuality_Value_MaxPerf,
		NVSDK_NGX_PerfQuality_Value_UltraPerformance,
	};
	for (NVSDK_NGX_PerfQuality_Value mode : modes) {
		unsigned int optimal_width = 0, optimal_height = 0, max_width = 0, max_height = 0, min_width = 0, min_height = 0;
		float sharpness = 0.0;
		NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(state.parameters, p_params->output_width, p_params->output_height, mode,
				&optimal_width, &optimal_height, &max_width, &max_height, &min_width, &min_height, &sharpness);
		if (!NVSDK_NGX_FAILED(result) && p_params->render_width >= min_width && p_params->render_width <= max_width && p_params->render_height >= min_height && p_params->render_height <= max_height) {
			return mode;
		}
	}
	return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

NVSDK_NGX_Resource_VK vulkan_resource(const NGXBridgeImage &p_image, bool p_read_write) {
	VkImageSubresourceRange range = {};
	range.aspectMask = p_image.aspect;
	range.levelCount = 1;
	range.baseArrayLayer = p_image.base_layer;
	range.layerCount = 1;
	return NVSDK_NGX_Create_ImageView_Resource_VK((VkImageView)p_image.view, (VkImage)p_image.resource, range, (VkFormat)p_image.format, p_image.width, p_image.height, p_read_write);
}

} // namespace

uint32_t ngx_bridge_get_version(void) {
	return NGX_BRIDGE_VERSION;
}

uint32_t ngx_bridge_vulkan_instance_extensions(const NGXBridgeSetup *p_setup, NGXBridgeAddExtension p_add, void *p_userdata) {
	setup(p_setup);
	uint32_t count = 0;
	VkExtensionProperties *properties = nullptr;
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&state.discovery, &count, &properties);
	if (!NVSDK_NGX_FAILED(result)) {
		for (uint32_t i = 0; i < count; i++) {
			p_add(p_userdata, properties[i].extensionName);
		}
	}
	return result;
}

uint32_t ngx_bridge_vulkan_device_extensions(const NGXBridgeSetup *p_setup, void *p_instance, void *p_physical_device, NGXBridgeAddExtension p_add, void *p_userdata) {
	setup(p_setup);
	uint32_t count = 0;
	VkExtensionProperties *extensions = nullptr;
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements((VkInstance)p_instance, (VkPhysicalDevice)p_physical_device, &state.discovery, &count, &extensions);
	if (!NVSDK_NGX_FAILED(result)) {
		for (uint32_t i = 0; i < count; i++) {
			p_add(p_userdata, extensions[i].extensionName);
		}
	}
	return result;
}

uint32_t ngx_bridge_vulkan_init(const NGXBridgeSetup *p_setup, void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr) {
	if (state.api != API_NONE) {
		return NVSDK_NGX_Result_FAIL_FeatureAlreadyExists;
	}
	setup(p_setup);
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(p_setup->project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, p_setup->engine_version, p_setup->data_path,
			(VkInstance)p_instance, (VkPhysicalDevice)p_physical_device, (VkDevice)p_device,
			(PFN_vkGetInstanceProcAddr)p_get_instance_proc_addr, (PFN_vkGetDeviceProcAddr)p_get_device_proc_addr, &state.common_info);
	if (NVSDK_NGX_FAILED(result)) {
		return result;
	}
	state.api = API_VULKAN;
	state.device = p_device;
	return get_capability_parameters();
}

uint32_t ngx_bridge_d3d12_init(const NGXBridgeSetup *p_setup, void *p_device) {
#ifdef _WIN32
	if (state.api != API_NONE) {
		return NVSDK_NGX_Result_FAIL_FeatureAlreadyExists;
	}
	setup(p_setup);
	NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(p_setup->project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, p_setup->engine_version, p_setup->data_path,
			(ID3D12Device *)p_device, &state.common_info);
	if (NVSDK_NGX_FAILED(result)) {
		return result;
	}
	state.api = API_D3D12;
	state.device = p_device;
	return get_capability_parameters();
#else
	return NVSDK_NGX_Result_FAIL_PlatformError;
#endif
}

uint32_t ngx_bridge_dlss_capability(int *r_available, int *r_needs_updated_driver) {
	*r_available = 0;
	*r_needs_updated_driver = 0;
	if (state.parameters == nullptr) {
		return NVSDK_NGX_Result_FAIL_NotInitialized;
	}
	NVSDK_NGX_Parameter_GetI(state.parameters, NVSDK_NGX_Parameter_SuperSampling_Available, r_available);
	NVSDK_NGX_Parameter_GetI(state.parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, r_needs_updated_driver);
	return NVSDK_NGX_Result_Success;
}

uint32_t ngx_bridge_dlss_create(void *p_command_buffer, const NGXBridgeDLSS *p_params, void **r_feature, int *r_quality) {
	*r_feature = nullptr;
	if (state.parameters == nullptr) {
		return NVSDK_NGX_Result_FAIL_NotInitialized;
	}
	NVSDK_NGX_DLSS_Create_Params create = {};
	create.Feature.InWidth = p_params->render_width;
	create.Feature.InHeight = p_params->render_height;
	create.Feature.InTargetWidth = p_params->output_width;
	create.Feature.InTargetHeight = p_params->output_height;
	create.Feature.InPerfQualityValue = dlss_quality(p_params);
	// Godot's internal colour is linear HDR, its depth reversed, its motion
	// vectors at render resolution without the jitter. DLSS measures the
	// exposure itself.
	create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
			NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
	*r_quality = (int)create.Feature.InPerfQualityValue;

	NVSDK_NGX_Handle *feature = nullptr;
	NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
	if (state.api == API_VULKAN) {
		result = NGX_VULKAN_CREATE_DLSS_EXT1((VkDevice)state.device, (VkCommandBuffer)p_command_buffer, 1, 1, &feature, state.parameters, &create);
	}
#ifdef _WIN32
	if (state.api == API_D3D12) {
		result = NGX_D3D12_CREATE_DLSS_EXT((ID3D12GraphicsCommandList *)p_command_buffer, 1, 1, &feature, state.parameters, &create);
	}
#endif
	if (!NVSDK_NGX_FAILED(result)) {
		*r_feature = feature;
	}
	return result;
}

uint32_t ngx_bridge_dlss_evaluate(void *p_command_buffer, void *p_feature, const NGXBridgeDLSS *p_params) {
	NVSDK_NGX_Handle *feature = (NVSDK_NGX_Handle *)p_feature;
	if (state.api == API_VULKAN) {
		NVSDK_NGX_Resource_VK color = vulkan_resource(p_params->color, false);
		NVSDK_NGX_Resource_VK depth = vulkan_resource(p_params->depth, false);
		NVSDK_NGX_Resource_VK motion = vulkan_resource(p_params->motion, false);
		NVSDK_NGX_Resource_VK output = vulkan_resource(p_params->output, true);

		NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
		eval.Feature.pInColor = &color;
		eval.Feature.pInOutput = &output;
		eval.pInDepth = &depth;
		eval.pInMotionVectors = &motion;
		eval.InJitterOffsetX = p_params->jitter_x;
		eval.InJitterOffsetY = p_params->jitter_y;
		eval.InRenderSubrectDimensions.Width = p_params->render_width;
		eval.InRenderSubrectDimensions.Height = p_params->render_height;
		eval.InReset = p_params->reset;
		eval.InMVScaleX = p_params->motion_scale_x;
		eval.InMVScaleY = p_params->motion_scale_y;
		eval.InFrameTimeDeltaInMsec = p_params->frame_time_ms;
		return NGX_VULKAN_EVALUATE_DLSS_EXT((VkCommandBuffer)p_command_buffer, feature, state.parameters, &eval);
	}
#ifdef _WIN32
	if (state.api == API_D3D12) {
		NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
		eval.Feature.pInColor = (ID3D12Resource *)p_params->color.resource;
		eval.Feature.pInOutput = (ID3D12Resource *)p_params->output.resource;
		eval.pInDepth = (ID3D12Resource *)p_params->depth.resource;
		eval.pInMotionVectors = (ID3D12Resource *)p_params->motion.resource;
		eval.InJitterOffsetX = p_params->jitter_x;
		eval.InJitterOffsetY = p_params->jitter_y;
		eval.InRenderSubrectDimensions.Width = p_params->render_width;
		eval.InRenderSubrectDimensions.Height = p_params->render_height;
		eval.InReset = p_params->reset;
		eval.InMVScaleX = p_params->motion_scale_x;
		eval.InMVScaleY = p_params->motion_scale_y;
		eval.InFrameTimeDeltaInMsec = p_params->frame_time_ms;
		return NGX_D3D12_EVALUATE_DLSS_EXT((ID3D12GraphicsCommandList *)p_command_buffer, feature, state.parameters, &eval);
	}
#endif
	return NVSDK_NGX_Result_FAIL_NotInitialized;
}

void ngx_bridge_dlss_release(void *p_feature) {
	if (state.api == API_VULKAN) {
		NVSDK_NGX_VULKAN_ReleaseFeature((NVSDK_NGX_Handle *)p_feature);
	}
#ifdef _WIN32
	if (state.api == API_D3D12) {
		NVSDK_NGX_D3D12_ReleaseFeature((NVSDK_NGX_Handle *)p_feature);
	}
#endif
}

void ngx_bridge_shutdown(void) {
	if (state.api == API_VULKAN) {
		if (state.parameters) {
			NVSDK_NGX_VULKAN_DestroyParameters(state.parameters);
		}
		NVSDK_NGX_VULKAN_Shutdown1((VkDevice)state.device);
	}
#ifdef _WIN32
	if (state.api == API_D3D12) {
		if (state.parameters) {
			NVSDK_NGX_D3D12_DestroyParameters(state.parameters);
		}
		NVSDK_NGX_D3D12_Shutdown1((ID3D12Device *)state.device);
	}
#endif
	state.parameters = nullptr;
	state.device = nullptr;
	state.api = API_NONE;
}
