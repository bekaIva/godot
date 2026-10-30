/**************************************************************************/
/*  ngx_vulkan.h                                                          */
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

#pragma once

#ifdef DLSS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device_driver.h"

// The Vulkan handles, declared as vulkan_core.h does, so that users of this
// header need no Vulkan include path.
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

// NVIDIA NGX, the runtime behind DLSS, on the Vulkan driver. Built only when
// SCons is given `dlss_sdk_path` (NVIDIA's DLSS SDK, which is not part of
// Godot's source and is not redistributed with it). NGX needs extra instance
// and device extensions, known before each is created, and is initialized
// once the device exists; the DLSS runtime library
// (libnvidia-ngx-dlss.so.<version>) is loaded from the executable's folder.
class NGXVulkan {
	static inline bool dlss_available = false;
	static inline VkDevice device = nullptr;
	static inline NVSDK_NGX_Parameter *parameters = nullptr;

	struct PendingRelease {
		NVSDK_NGX_Handle *feature = nullptr;
		uint64_t frame = 0;
	};
	static inline LocalVector<PendingRelease> pending_releases;

public:
	// Extensions NGX needs for DLSS (none when NGX is not usable here).
	static void get_instance_extensions(LocalVector<CharString> &r_extensions);
	static void get_device_extensions(VkInstance p_instance, VkPhysicalDevice p_physical_device, LocalVector<CharString> &r_extensions);

	// Called once the device exists with every extension NGX asked for. NGX
	// serves one device: the first one (the main rendering device); local
	// devices created later go without.
	static void initialize(VkInstance p_instance, VkPhysicalDevice p_physical_device, VkDevice p_device);
	static void finalize(VkDevice p_device);

	static bool is_dlss_available(VkDevice p_device) { return dlss_available && p_device == device; }

	// A texture as RenderingDevice::get_driver_resource() gives it.
	struct Image {
		uint64_t image = 0; // VkImage.
		uint64_t view = 0; // VkImageView.
		uint64_t format = 0; // VkFormat.
		uint32_t layer = 0;
	};

	struct DLSSEvaluate {
		Image color; // Internal resolution, linear HDR.
		Image depth; // Reverse Z.
		Image motion; // Current to previous position, in UV.
		Image output; // Target resolution, storage.
		uint32_t render_width = 0;
		uint32_t render_height = 0;
		uint32_t output_width = 0;
		uint32_t output_height = 0;
		float jitter_x = 0.0; // Render pixels.
		float jitter_y = 0.0;
		float frame_time_ms = 0.0;
		bool reset = false;
	};

	// Records DLSS into the command buffer, creating the feature on its first
	// use; false when the feature cannot be created.
	static bool evaluate_dlss(RenderingDeviceDriver *p_driver, RenderingDeviceDriver::CommandBufferID p_command_buffer, NVSDK_NGX_Handle *&r_feature, const DLSSEvaluate &p_params);
	// A feature no longer used from frame p_frame on; it is released once the
	// GPU has finished that frame (release_finished_dlss), or at finalize().
	static void release_dlss(NVSDK_NGX_Handle *p_feature, uint64_t p_frame);
	static void release_finished_dlss(uint64_t p_finished_frame);
};

#endif // DLSS_ENABLED
