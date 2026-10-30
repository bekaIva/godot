/**************************************************************************/
/*  ngx_library.h                                                         */
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
#include "drivers/ngx/ngx_bridge.h"

// NVIDIA NGX, the runtime behind DLSS, through our bridge (ngx_bridge.h):
// compiled into the engine on Linux (SCons `dlss_sdk_path`), loaded from
// patina_ngx.dll at run time on Windows (NGX_BRIDGE_DLL; beside the
// executable, then in the project's folder). NVIDIA's DLSS runtime
// (libnvidia-ngx-dlss.so.<version>, nvngx_dlss.dll) is looked for in the same
// folders. NGX serves one device: the first one the Vulkan or Direct3D 12
// driver initializes it on (the main rendering device).
class NGXLibrary {
	struct Bridge {
		decltype(&ngx_bridge_get_version) get_version = nullptr;
		decltype(&ngx_bridge_vulkan_instance_extensions) vulkan_instance_extensions = nullptr;
		decltype(&ngx_bridge_vulkan_device_extensions) vulkan_device_extensions = nullptr;
		decltype(&ngx_bridge_vulkan_init) vulkan_init = nullptr;
		decltype(&ngx_bridge_d3d12_init) d3d12_init = nullptr;
		decltype(&ngx_bridge_dlss_capability) dlss_capability = nullptr;
		decltype(&ngx_bridge_dlss_create) dlss_create = nullptr;
		decltype(&ngx_bridge_dlss_evaluate) dlss_evaluate = nullptr;
		decltype(&ngx_bridge_dlss_release) dlss_release = nullptr;
		decltype(&ngx_bridge_shutdown) shutdown = nullptr;
	};
	static Bridge bridge;
	static inline bool load_attempted = false;
	static inline bool loaded = false;
	static inline String loaded_path;

	static inline bool dlss_available = false;
	static inline void *device = nullptr;

	struct PendingRelease {
		void *feature = nullptr;
		uint64_t frame = 0;
	};
	static inline LocalVector<PendingRelease> pending_releases;

	static bool _load();
	static const NGXBridgeSetup &_setup();
	static void _initialized(uint32_t p_result, void *p_device, const char *p_api);

public:
	// Vulkan: the extensions NGX needs (none when NGX is not usable here),
	// then NGX once the device exists with all of them.
	static void get_vulkan_instance_extensions(LocalVector<CharString> &r_extensions);
	static void get_vulkan_device_extensions(void *p_instance, void *p_physical_device, LocalVector<CharString> &r_extensions);
	static void initialize_vulkan(void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr);
	// Direct3D 12: NGX on the ID3D12Device.
	static void initialize_d3d12(void *p_device);
	static void finalize(void *p_device);

	static bool is_dlss_available(void *p_device) { return dlss_available && p_device == device; }

	// Records DLSS into the command buffer or list, creating the feature on its
	// first use; false when the feature cannot be created.
	static bool evaluate_dlss(void *p_command_buffer, void *&r_feature, const NGXBridgeDLSS &p_params);
	// A feature no longer used from frame p_frame on; it is released once the
	// GPU has finished that frame (release_finished_dlss), or at finalize().
	static void release_dlss(void *p_feature, uint64_t p_frame);
	static void release_finished_dlss(uint64_t p_finished_frame);
};

#endif // DLSS_ENABLED
