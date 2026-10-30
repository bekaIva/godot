/**************************************************************************/
/*  ngx_bridge.h                                                          */
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

// NVIDIA NGX (the runtime behind DLSS) behind a plain C API of our own, so
// that no NVIDIA type reaches the engine. ngx_bridge.cpp makes every NGX call.
//
// - Linux: SCons `dlss_sdk_path` compiles ngx_bridge.cpp into the engine and
//   links NVIDIA's libnvsdk_ngx.a.
// - Windows: NVIDIA ships only MSVC static libraries (nvsdk_ngx_s.lib), which
//   MinGW cannot link, so ngx_bridge.cpp is built with MSVC as its own DLL
//   (patina_ngx.dll, NGX_BRIDGE_BUILD_DLL) and the engine loads it at run
//   time (ngx_library.cpp). Without the DLL the engine has no DLSS.
//
// Handles are passed as void * (VkInstance, ID3D12Device *, ...) or uint64_t
// (non-dispatchable Vulkan handles). Results are NGX's result codes.

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(NGX_BRIDGE_BUILD_DLL)
#define NGX_BRIDGE_API __declspec(dllexport)
#else
#define NGX_BRIDGE_API
#endif

// Raised whenever a function or structure below changes; the engine refuses
// a DLL of another version.
#define NGX_BRIDGE_VERSION 1

// NGX's failures are 0xBADxxxxx; success is 1.
#define NGX_BRIDGE_FAILED(m_result) ((((uint32_t)(m_result)) & 0xFFF00000u) == 0xBAD00000u)
#define NGX_BRIDGE_RESULT_FAIL 0xBAD00000u

// What NGX needs to know about the application, for every call that
// initializes it. The strings must outlive the bridge's use of NGX.
typedef struct NGXBridgeSetup {
	const char *project_id; // A GUID-like project ID of our own.
	const char *engine_version;
	const wchar_t *data_path; // NGX's logs and caches.
	const wchar_t *const *library_paths; // Where NVIDIA's DLSS runtime is looked for.
	uint32_t library_path_count;
	int logging; // NGX's logs into data_path.
} NGXBridgeSetup;

typedef void (*NGXBridgeAddExtension)(void *p_userdata, const char *p_name);

typedef struct NGXBridgeImage {
	uint64_t resource; // VkImage, or ID3D12Resource *.
	uint64_t view; // VkImageView (Vulkan only).
	uint32_t format; // VkFormat (Vulkan only).
	uint32_t aspect; // VkImageAspectFlags of the view (Vulkan only).
	uint32_t base_layer; // Vulkan only.
	uint32_t width;
	uint32_t height;
} NGXBridgeImage;

typedef struct NGXBridgeDLSS {
	NGXBridgeImage color; // Render size, linear HDR.
	NGXBridgeImage depth; // Render size, reversed Z.
	NGXBridgeImage motion; // Render size, current to previous position.
	NGXBridgeImage output; // Output size, written as a storage image.
	uint32_t render_width;
	uint32_t render_height;
	uint32_t output_width;
	uint32_t output_height;
	float jitter_x; // Render pixels.
	float jitter_y;
	float motion_scale_x; // From the motion texture's units to render pixels.
	float motion_scale_y;
	float frame_time_ms;
	int reset; // No history (a new feature, a camera cut).
} NGXBridgeDLSS;

NGX_BRIDGE_API uint32_t ngx_bridge_get_version(void);

// Vulkan: the extensions NGX needs, known before the instance and the device
// are created (none when NGX cannot run here), then NGX on the device that
// has them all. The proc addresses are the engine's vkGetInstanceProcAddr and
// vkGetDeviceProcAddr.
NGX_BRIDGE_API uint32_t ngx_bridge_vulkan_instance_extensions(const NGXBridgeSetup *p_setup, NGXBridgeAddExtension p_add, void *p_userdata);
NGX_BRIDGE_API uint32_t ngx_bridge_vulkan_device_extensions(const NGXBridgeSetup *p_setup, void *p_instance, void *p_physical_device, NGXBridgeAddExtension p_add, void *p_userdata);
NGX_BRIDGE_API uint32_t ngx_bridge_vulkan_init(const NGXBridgeSetup *p_setup, void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr);

// Direct3D 12 (Windows only): NGX on the ID3D12Device; it needs nothing at
// device creation.
NGX_BRIDGE_API uint32_t ngx_bridge_d3d12_init(const NGXBridgeSetup *p_setup, void *p_device);

// Once initialized (one device at a time): whether DLSS super resolution runs
// on this GPU and driver.
NGX_BRIDGE_API uint32_t ngx_bridge_dlss_capability(int *r_available, int *r_needs_updated_driver);

// Records into the command buffer (VkCommandBuffer) or command list
// (ID3D12GraphicsCommandList *). Create makes the feature for the render and
// output sizes in p_params, in the best quality mode whose range holds the
// render size (DLAA when they are equal); r_quality is NGX's mode.
NGX_BRIDGE_API uint32_t ngx_bridge_dlss_create(void *p_command_buffer, const NGXBridgeDLSS *p_params, void **r_feature, int *r_quality);
NGX_BRIDGE_API uint32_t ngx_bridge_dlss_evaluate(void *p_command_buffer, void *p_feature, const NGXBridgeDLSS *p_params);
// Only once the GPU has finished every frame that used the feature.
NGX_BRIDGE_API void ngx_bridge_dlss_release(void *p_feature);

// The device is idle; every feature is released.
NGX_BRIDGE_API void ngx_bridge_shutdown(void);

#ifdef __cplusplus
}
#endif
