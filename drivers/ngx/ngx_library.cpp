/**************************************************************************/
/*  ngx_library.cpp                                                       */
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

#include "ngx_library.h"

#ifdef DLSS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/version.h"

#ifdef NGX_BRIDGE_DLL
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <type_traits>
#endif

// NGX asks engines without an application ID from NVIDIA for a GUID-like
// project ID of their own.
static constexpr const char *NGX_PROJECT_ID = "0f61f1e1-8264-484d-8def-b73e8b69e894";

NGXLibrary::Bridge NGXLibrary::bridge;

// The folders the bridge and NVIDIA's runtime are looked for in: beside the
// executable (exported games, the editor on Linux), then the project's folder
// (the editor and runs with --path on Windows).
static Vector<String> _ngx_folders() {
	Vector<String> folders;
	folders.push_back(OS::get_singleton()->get_executable_path().get_base_dir());
	if (ProjectSettings::get_singleton() != nullptr && !ProjectSettings::get_singleton()->get_resource_path().is_empty()) {
		String project = ProjectSettings::get_singleton()->get_resource_path();
		if (project != folders[0]) {
			folders.push_back(project);
		}
	}
	return folders;
}

bool NGXLibrary::_load() {
	if (load_attempted) {
		return loaded;
	}
	load_attempted = true;

#ifdef NGX_BRIDGE_DLL
	// NVIDIA's NGX library is for MSVC only: the bridge is its own DLL.
	HMODULE module = nullptr;
	for (const String &folder : _ngx_folders()) {
		String candidate = folder.path_join("patina_ngx.dll");
		if (!FileAccess::exists(candidate)) {
			continue;
		}
		module = LoadLibraryW((LPCWSTR)candidate.replace_char('/', '\\').utf16().get_data());
		if (module != nullptr) {
			loaded_path = candidate;
			break;
		}
	}
	if (module == nullptr) {
		print_verbose("NGX: patina_ngx.dll not found, DLSS is not available.");
		return false;
	}
	auto get_proc = [module](auto &r_function, const char *p_name) {
		r_function = (std::remove_reference_t<decltype(r_function)>)(void *)GetProcAddress(module, p_name);
		return r_function != nullptr;
	};
	bool ok = get_proc(bridge.get_version, "ngx_bridge_get_version");
	if (ok && bridge.get_version() != NGX_BRIDGE_VERSION) {
		WARN_PRINT(vformat("NGX: %s is bridge version %d, this engine needs %d; DLSS is not available.", loaded_path, bridge.get_version(), NGX_BRIDGE_VERSION));
		ok = false;
	}
	ok = ok && get_proc(bridge.vulkan_instance_extensions, "ngx_bridge_vulkan_instance_extensions");
	ok = ok && get_proc(bridge.vulkan_device_extensions, "ngx_bridge_vulkan_device_extensions");
	ok = ok && get_proc(bridge.vulkan_init, "ngx_bridge_vulkan_init");
	ok = ok && get_proc(bridge.d3d12_init, "ngx_bridge_d3d12_init");
	ok = ok && get_proc(bridge.dlss_capability, "ngx_bridge_dlss_capability");
	ok = ok && get_proc(bridge.dlss_create, "ngx_bridge_dlss_create");
	ok = ok && get_proc(bridge.dlss_evaluate, "ngx_bridge_dlss_evaluate");
	ok = ok && get_proc(bridge.dlss_release, "ngx_bridge_dlss_release");
	ok = ok && get_proc(bridge.shutdown, "ngx_bridge_shutdown");
	if (!ok) {
		WARN_PRINT(vformat("NGX: %s is not a bridge this engine can use; DLSS is not available.", loaded_path));
		FreeLibrary(module);
		return false;
	}
#else
	// Compiled in (ngx_bridge.cpp, linked with NVIDIA's static library).
	bridge.get_version = ngx_bridge_get_version;
	bridge.vulkan_instance_extensions = ngx_bridge_vulkan_instance_extensions;
	bridge.vulkan_device_extensions = ngx_bridge_vulkan_device_extensions;
	bridge.vulkan_init = ngx_bridge_vulkan_init;
	bridge.d3d12_init = ngx_bridge_d3d12_init;
	bridge.dlss_capability = ngx_bridge_dlss_capability;
	bridge.dlss_create = ngx_bridge_dlss_create;
	bridge.dlss_evaluate = ngx_bridge_dlss_evaluate;
	bridge.dlss_release = ngx_bridge_dlss_release;
	bridge.shutdown = ngx_bridge_shutdown;
	loaded_path = "the engine";
#endif
	loaded = true;
	return true;
}

// NGX takes wide strings: UTF-32 on Linux, UTF-16 on Windows. They must
// outlive every NGX call, so they are kept here.
const NGXBridgeSetup &NGXLibrary::_setup() {
	static struct {
		String data_path;
		Vector<String> library_paths;
#ifdef WINDOWS_ENABLED
		Char16String data_path_wide;
		Vector<Char16String> library_paths_wide;
#endif
		LocalVector<const wchar_t *> library_path_pointers;
		NGXBridgeSetup setup = {};
		bool ready = false;
	} strings;

	if (!strings.ready) {
		// Logs and caches go to the user data folder.
		strings.data_path = OS::get_singleton()->get_user_data_dir();
		strings.library_paths = _ngx_folders();
#ifdef WINDOWS_ENABLED
		strings.data_path_wide = strings.data_path.replace_char('/', '\\').utf16();
		for (const String &path : strings.library_paths) {
			strings.library_paths_wide.push_back(path.replace_char('/', '\\').utf16());
		}
		for (const Char16String &path : strings.library_paths_wide) {
			strings.library_path_pointers.push_back((const wchar_t *)path.get_data());
		}
		strings.setup.data_path = (const wchar_t *)strings.data_path_wide.get_data();
#else
		static_assert(sizeof(wchar_t) == sizeof(char32_t));
		for (const String &path : strings.library_paths) {
			strings.library_path_pointers.push_back((const wchar_t *)path.get_data());
		}
		strings.setup.data_path = (const wchar_t *)strings.data_path.get_data();
#endif
		strings.setup.project_id = NGX_PROJECT_ID;
		strings.setup.engine_version = GODOT_VERSION_NUMBER;
		strings.setup.library_paths = strings.library_path_pointers.ptr();
		strings.setup.library_path_count = strings.library_path_pointers.size();
		strings.setup.logging = OS::get_singleton()->is_stdout_verbose() ? 1 : 0;
		strings.ready = true;
	}
	return strings.setup;
}

static void _add_extension(void *p_userdata, const char *p_name) {
	((LocalVector<CharString> *)p_userdata)->push_back(CharString(p_name));
}

void NGXLibrary::get_vulkan_instance_extensions(LocalVector<CharString> &r_extensions) {
	if (!_load()) {
		return;
	}
	uint32_t result = bridge.vulkan_instance_extensions(&_setup(), _add_extension, &r_extensions);
	if (NGX_BRIDGE_FAILED(result)) {
		print_verbose(vformat("NGX: No instance extension requirements for DLSS (0x%x); DLSS is not available.", result));
	}
}

void NGXLibrary::get_vulkan_device_extensions(void *p_instance, void *p_physical_device, LocalVector<CharString> &r_extensions) {
	if (!_load()) {
		return;
	}
	uint32_t result = bridge.vulkan_device_extensions(&_setup(), p_instance, p_physical_device, _add_extension, &r_extensions);
	if (NGX_BRIDGE_FAILED(result)) {
		print_verbose(vformat("NGX: No device extension requirements for DLSS (0x%x); DLSS is not available.", result));
	}
}

void NGXLibrary::initialize_vulkan(void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr) {
	if (device || !_load()) {
		return;
	}
	_initialized(bridge.vulkan_init(&_setup(), p_instance, p_physical_device, p_device, p_get_instance_proc_addr, p_get_device_proc_addr), p_device, "Vulkan");
}

void NGXLibrary::initialize_d3d12(void *p_device) {
	if (device || !_load()) {
		return;
	}
	_initialized(bridge.d3d12_init(&_setup(), p_device), p_device, "Direct3D 12");
}

void NGXLibrary::_initialized(uint32_t p_result, void *p_device, const char *p_api) {
	if (NGX_BRIDGE_FAILED(p_result)) {
		print_verbose(vformat("NGX: Initialization on %s failed (0x%x); DLSS is not available.", p_api, p_result));
		return;
	}
	device = p_device;

	int available = 0;
	int needs_updated_driver = 0;
	bridge.dlss_capability(&available, &needs_updated_driver);
	if (needs_updated_driver) {
		WARN_PRINT("DLSS needs a newer NVIDIA driver; it is not available.");
	}
	dlss_available = available && !needs_updated_driver;
	print_verbose(vformat("NGX: DLSS %s on %s (bridge from %s, runtime library from %s).", dlss_available ? "available" : "not available", p_api, loaded_path, String(", ").join(_ngx_folders())));
}

void NGXLibrary::finalize(void *p_device) {
	if (device == nullptr || p_device != device) {
		return;
	}
	dlss_available = false;
	// The device is idle by now.
	for (const PendingRelease &pending : pending_releases) {
		bridge.dlss_release(pending.feature);
	}
	pending_releases.clear();
	bridge.shutdown();
	device = nullptr;
}

bool NGXLibrary::evaluate_dlss(void *p_command_buffer, void *&r_feature, const NGXBridgeDLSS &p_params) {
	ERR_FAIL_COND_V(!dlss_available, false);

	if (r_feature == nullptr) {
		int quality = 0;
		uint32_t result = bridge.dlss_create(p_command_buffer, &p_params, &r_feature, &quality);
		if (NGX_BRIDGE_FAILED(result)) {
			r_feature = nullptr;
			ERR_FAIL_V_MSG(false, vformat("DLSS feature creation failed (0x%x) for %dx%d to %dx%d.", result, p_params.render_width, p_params.render_height, p_params.output_width, p_params.output_height));
		}
		print_verbose(vformat("NGX: DLSS feature for %dx%d to %dx%d, quality mode %d.", p_params.render_width, p_params.render_height, p_params.output_width, p_params.output_height, quality));
	}

	uint32_t result = bridge.dlss_evaluate(p_command_buffer, r_feature, &p_params);
	ERR_FAIL_COND_V_MSG(NGX_BRIDGE_FAILED(result), true, vformat("DLSS evaluation failed (0x%x).", result));
	return true;
}

void NGXLibrary::release_dlss(void *p_feature, uint64_t p_frame) {
	if (p_feature && device) {
		pending_releases.push_back({ p_feature, p_frame });
	}
}

void NGXLibrary::release_finished_dlss(uint64_t p_finished_frame) {
	for (uint32_t i = 0; i < pending_releases.size();) {
		if (pending_releases[i].frame <= p_finished_frame) {
			bridge.dlss_release(pending_releases[i].feature);
			pending_releases.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

#endif // DLSS_ENABLED
