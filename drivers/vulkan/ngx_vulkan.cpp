/**************************************************************************/
/*  ngx_vulkan.cpp                                                        */
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

#include "ngx_vulkan.h"

#ifdef DLSS_ENABLED

#include "core/os/os.h"
#include "core/version.h"
#include "drivers/vulkan/rendering_device_driver_vulkan.h"

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

// NGX asks engines without an application ID from NVIDIA for a GUID-like
// project ID of their own.
static constexpr const char *NGX_PROJECT_ID = "0f61f1e1-8264-484d-8def-b73e8b69e894";
static constexpr uint32_t NVIDIA_VENDOR_ID = 0x10de;

// NGX takes wide strings: UTF-32 on Linux, UTF-16 on Windows. They must
// outlive every NGX call, so they are kept here.
struct NGXStrings {
	String data_path;
	String library_path;
#ifdef WINDOWS_ENABLED
	Char16String data_path_wide;
	Char16String library_path_wide;
#endif
	const wchar_t *library_paths[1] = {};
	NVSDK_NGX_FeatureCommonInfo common_info = {};
	NVSDK_NGX_FeatureDiscoveryInfo discovery = {};
	bool ready = false;
};

static NGXStrings &_ngx_strings() {
	static NGXStrings strings;
	if (!strings.ready) {
		// Logs and caches go to the user data folder, the runtime library is
		// looked for beside the executable.
		strings.data_path = OS::get_singleton()->get_user_data_dir();
		strings.library_path = OS::get_singleton()->get_executable_path().get_base_dir();
#ifdef WINDOWS_ENABLED
		strings.data_path_wide = strings.data_path.utf16();
		strings.library_path_wide = strings.library_path.utf16();
		const wchar_t *data_path = (const wchar_t *)strings.data_path_wide.get_data();
		strings.library_paths[0] = (const wchar_t *)strings.library_path_wide.get_data();
#else
		static_assert(sizeof(wchar_t) == sizeof(char32_t));
		const wchar_t *data_path = (const wchar_t *)strings.data_path.get_data();
		strings.library_paths[0] = (const wchar_t *)strings.library_path.get_data();
#endif
		strings.common_info.PathListInfo.Path = strings.library_paths;
		strings.common_info.PathListInfo.Length = 1;
		strings.common_info.LoggingInfo.MinimumLoggingLevel = OS::get_singleton()->is_stdout_verbose() ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;

		strings.discovery.SDKVersion = NVSDK_NGX_Version_API;
		strings.discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
		strings.discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
		strings.discovery.Identifier.v.ProjectDesc.ProjectId = NGX_PROJECT_ID;
		strings.discovery.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
		strings.discovery.Identifier.v.ProjectDesc.EngineVersion = GODOT_VERSION_NUMBER;
		strings.discovery.ApplicationDataPath = data_path;
		strings.discovery.FeatureInfo = &strings.common_info;
		strings.ready = true;
	}
	return strings;
}

void NGXVulkan::get_instance_extensions(LocalVector<CharString> &r_extensions) {
	uint32_t count = 0;
	VkExtensionProperties *properties = nullptr;
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&_ngx_strings().discovery, &count, &properties);
	if (NVSDK_NGX_FAILED(result)) {
		print_verbose(vformat("NGX: No instance extension requirements for DLSS (0x%x); DLSS is not available.", (uint32_t)result));
		return;
	}
	for (uint32_t i = 0; i < count; i++) {
		r_extensions.push_back(CharString(properties[i].extensionName));
	}
}

void NGXVulkan::get_device_extensions(VkInstance p_instance, VkPhysicalDevice p_physical_device, LocalVector<CharString> &r_extensions) {
	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(p_physical_device, &properties);
	if (properties.vendorID != NVIDIA_VENDOR_ID) {
		return;
	}

	uint32_t count = 0;
	VkExtensionProperties *extensions = nullptr;
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(p_instance, p_physical_device, &_ngx_strings().discovery, &count, &extensions);
	if (NVSDK_NGX_FAILED(result)) {
		print_verbose(vformat("NGX: No device extension requirements for DLSS (0x%x); DLSS is not available.", (uint32_t)result));
		return;
	}
	for (uint32_t i = 0; i < count; i++) {
		r_extensions.push_back(CharString(extensions[i].extensionName));
	}
}

void NGXVulkan::initialize(VkInstance p_instance, VkPhysicalDevice p_physical_device, VkDevice p_device) {
	if (device) {
		return;
	}
	NGXStrings &strings = _ngx_strings();
	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(NGX_PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, GODOT_VERSION_NUMBER, strings.discovery.ApplicationDataPath,
			p_instance, p_physical_device, p_device, vkGetInstanceProcAddr, vkGetDeviceProcAddr, &strings.common_info);
	if (NVSDK_NGX_FAILED(result)) {
		print_verbose(vformat("NGX: Initialization failed (0x%x); DLSS is not available.", (uint32_t)result));
		return;
	}
	device = p_device;

	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&parameters);
	if (NVSDK_NGX_FAILED(result)) {
		print_verbose(vformat("NGX: No capability parameters (0x%x); DLSS is not available.", (uint32_t)result));
		finalize(p_device);
		return;
	}

	int available = 0;
	int needs_updated_driver = 0;
	NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
	NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_updated_driver);
	if (needs_updated_driver) {
		WARN_PRINT("DLSS needs a newer NVIDIA driver; it is not available.");
	}
	dlss_available = available && !needs_updated_driver;
	print_verbose(vformat("NGX: DLSS %s (runtime library from %s).", dlss_available ? "available" : "not available", strings.library_path));
}

void NGXVulkan::finalize(VkDevice p_device) {
	if (p_device != device) {
		return;
	}
	dlss_available = false;
	// The device is idle by now.
	for (const PendingRelease &pending : pending_releases) {
		NVSDK_NGX_VULKAN_ReleaseFeature(pending.feature);
	}
	pending_releases.clear();
	if (parameters) {
		NVSDK_NGX_VULKAN_DestroyParameters(parameters);
		parameters = nullptr;
	}
	if (device) {
		NVSDK_NGX_VULKAN_Shutdown1(device);
		device = nullptr;
	}
}

// DLSS's quality modes differ in the range of render sizes they take: the
// first mode whose range holds the render size, best quality first.
static NVSDK_NGX_PerfQuality_Value _dlss_quality(NVSDK_NGX_Parameter *p_parameters, const NGXVulkan::DLSSEvaluate &p_params) {
	if (p_params.render_width >= p_params.output_width) {
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
		NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(p_parameters, p_params.output_width, p_params.output_height, mode,
				&optimal_width, &optimal_height, &max_width, &max_height, &min_width, &min_height, &sharpness);
		if (!NVSDK_NGX_FAILED(result) && p_params.render_width >= min_width && p_params.render_width <= max_width && p_params.render_height >= min_height && p_params.render_height <= max_height) {
			return mode;
		}
	}
	return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

static NVSDK_NGX_Resource_VK _dlss_resource(const NGXVulkan::Image &p_image, uint32_t p_width, uint32_t p_height, bool p_read_write) {
	VkFormat format = (VkFormat)p_image.format;
	bool depth = format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_X8_D24_UNORM_PACK32 || format == VK_FORMAT_D32_SFLOAT ||
			format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
	VkImageSubresourceRange range = {};
	range.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.baseArrayLayer = p_image.layer;
	range.layerCount = 1;
	return NVSDK_NGX_Create_ImageView_Resource_VK((VkImageView)p_image.view, (VkImage)p_image.image, range, format, p_width, p_height, p_read_write);
}

bool NGXVulkan::evaluate_dlss(RenderingDeviceDriver *p_driver, RenderingDeviceDriver::CommandBufferID p_command_buffer, NVSDK_NGX_Handle *&r_feature, const DLSSEvaluate &p_params) {
	ERR_FAIL_COND_V(!dlss_available, false);
	VkCommandBuffer command_buffer = ((RenderingDeviceDriverVulkan::CommandBufferInfo *)p_command_buffer.id)->vk_command_buffer;

	if (r_feature == nullptr) {
		NVSDK_NGX_DLSS_Create_Params create = {};
		create.Feature.InWidth = p_params.render_width;
		create.Feature.InHeight = p_params.render_height;
		create.Feature.InTargetWidth = p_params.output_width;
		create.Feature.InTargetHeight = p_params.output_height;
		create.Feature.InPerfQualityValue = _dlss_quality(parameters, p_params);
		// Godot's internal colour is linear HDR, its depth reversed, its
		// motion vectors at render resolution without the jitter. DLSS
		// measures the exposure itself.
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
				NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT1(device, command_buffer, 1, 1, &r_feature, parameters, &create);
		if (NVSDK_NGX_FAILED(result)) {
			r_feature = nullptr;
			ERR_FAIL_V_MSG(false, vformat("DLSS feature creation failed (0x%x) for %dx%d to %dx%d.", (uint32_t)result, p_params.render_width, p_params.render_height, p_params.output_width, p_params.output_height));
		}
		print_verbose(vformat("NGX: DLSS feature for %dx%d to %dx%d, quality mode %d.", p_params.render_width, p_params.render_height, p_params.output_width, p_params.output_height, (int)create.Feature.InPerfQualityValue));
	}

	NVSDK_NGX_Resource_VK color = _dlss_resource(p_params.color, p_params.render_width, p_params.render_height, false);
	NVSDK_NGX_Resource_VK depth = _dlss_resource(p_params.depth, p_params.render_width, p_params.render_height, false);
	NVSDK_NGX_Resource_VK motion = _dlss_resource(p_params.motion, p_params.render_width, p_params.render_height, false);
	NVSDK_NGX_Resource_VK output = _dlss_resource(p_params.output, p_params.output_width, p_params.output_height, true);

	NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
	eval.Feature.pInColor = &color;
	eval.Feature.pInOutput = &output;
	eval.pInDepth = &depth;
	eval.pInMotionVectors = &motion;
	eval.InJitterOffsetX = p_params.jitter_x;
	eval.InJitterOffsetY = p_params.jitter_y;
	eval.InRenderSubrectDimensions.Width = p_params.render_width;
	eval.InRenderSubrectDimensions.Height = p_params.render_height;
	eval.InReset = p_params.reset ? 1 : 0;
	// Godot's motion vectors are in UV units; DLSS wants render pixels.
	eval.InMVScaleX = float(p_params.render_width);
	eval.InMVScaleY = float(p_params.render_height);
	eval.InFrameTimeDeltaInMsec = p_params.frame_time_ms;

	NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(command_buffer, r_feature, parameters, &eval);
	ERR_FAIL_COND_V_MSG(NVSDK_NGX_FAILED(result), true, vformat("DLSS evaluation failed (0x%x).", (uint32_t)result));
	return true;
}

void NGXVulkan::release_dlss(NVSDK_NGX_Handle *p_feature, uint64_t p_frame) {
	if (p_feature && device) {
		pending_releases.push_back({ p_feature, p_frame });
	}
}

void NGXVulkan::release_finished_dlss(uint64_t p_finished_frame) {
	for (uint32_t i = 0; i < pending_releases.size();) {
		if (pending_releases[i].frame <= p_finished_frame) {
			NVSDK_NGX_VULKAN_ReleaseFeature(pending_releases[i].feature);
			pending_releases.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

#endif // DLSS_ENABLED
