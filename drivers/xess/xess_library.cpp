/**************************************************************************/
/*  xess_library.cpp                                                      */
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

#include "xess_library.h"

#ifdef XESS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

XeSSLibrary XeSSLibrary::singleton;

void *XeSSLibrary::_get_proc(const char *p_name) const {
	if (library == nullptr) {
		return nullptr;
	}
	return (void *)GetProcAddress((HMODULE)library, p_name);
}

bool XeSSLibrary::load() {
	XeSSLibrary &lib = singleton;
	if (lib.load_attempted) {
		return lib.library != nullptr;
	}
	lib.load_attempted = true;

	// Beside the executable (exported games), then in the project's folder
	// (the editor and runs with --path), then the usual DLL search path.
	Vector<String> candidates;
	candidates.push_back(OS::get_singleton()->get_executable_path().get_base_dir().path_join("libxess.dll"));
	if (ProjectSettings::get_singleton() != nullptr && !ProjectSettings::get_singleton()->get_resource_path().is_empty()) {
		candidates.push_back(ProjectSettings::get_singleton()->get_resource_path().path_join("libxess.dll"));
	}
	candidates.push_back("libxess.dll");

	for (const String &candidate : candidates) {
		if (candidate.is_absolute_path() && !FileAccess::exists(candidate)) {
			continue;
		}
		HMODULE module = LoadLibraryW((LPCWSTR)candidate.replace_char('/', '\\').utf16().get_data());
		if (module != nullptr) {
			lib.library = (void *)module;
			lib.loaded_path = candidate;
			break;
		}
	}
	if (lib.library == nullptr) {
		print_verbose("XeSS: libxess.dll not found, XeSS is unavailable.");
		return false;
	}

	bool ok = true;
	ok &= lib.get_proc(lib.GetVersion, "xessGetVersion");
	ok &= lib.get_proc(lib.DestroyContext, "xessDestroyContext");
	ok &= lib.get_proc(lib.IsOptimalDriver, "xessIsOptimalDriver");
	ok &= lib.get_proc(lib.SetLoggingCallback, "xessSetLoggingCallback");
	ok &= lib.get_proc(lib.GetOptimalInputResolution, "xessGetOptimalInputResolution");
	ok &= lib.get_proc(lib.SetVelocityScale, "xessSetVelocityScale");
	ok &= lib.get_proc(lib.SetJitterScale, "xessSetJitterScale");
	if (!ok) {
		WARN_PRINT(vformat("XeSS: %s lacks functions this engine needs; XeSS is unavailable.", lib.loaded_path));
		FreeLibrary((HMODULE)lib.library);
		lib.library = nullptr;
		return false;
	}

	print_verbose(vformat("XeSS: loaded %s (SDK %s).", lib.loaded_path, lib.get_version_string()));
	return true;
}

String XeSSLibrary::get_version_string() const {
	xess_version_t version = {};
	if (GetVersion == nullptr || GetVersion(&version) != XESS_RESULT_SUCCESS) {
		return "unknown";
	}
	return vformat("%d.%d.%d", version.major, version.minor, version.patch);
}

const char *XeSSLibrary::result_to_string(xess_result_t p_result) {
	switch (p_result) {
		case XESS_RESULT_WARNING_NONEXISTING_FOLDER:
			return "WARNING_NONEXISTING_FOLDER";
		case XESS_RESULT_WARNING_OLD_DRIVER:
			return "WARNING_OLD_DRIVER";
		case XESS_RESULT_SUCCESS:
			return "SUCCESS";
		case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
			return "ERROR_UNSUPPORTED_DEVICE";
		case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
			return "ERROR_UNSUPPORTED_DRIVER";
		case XESS_RESULT_ERROR_UNINITIALIZED:
			return "ERROR_UNINITIALIZED";
		case XESS_RESULT_ERROR_INVALID_ARGUMENT:
			return "ERROR_INVALID_ARGUMENT";
		case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
			return "ERROR_DEVICE_OUT_OF_MEMORY";
		case XESS_RESULT_ERROR_DEVICE:
			return "ERROR_DEVICE";
		case XESS_RESULT_ERROR_NOT_IMPLEMENTED:
			return "ERROR_NOT_IMPLEMENTED";
		case XESS_RESULT_ERROR_INVALID_CONTEXT:
			return "ERROR_INVALID_CONTEXT";
		case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS:
			return "ERROR_OPERATION_IN_PROGRESS";
		case XESS_RESULT_ERROR_UNSUPPORTED:
			return "ERROR_UNSUPPORTED";
		case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:
			return "ERROR_CANT_LOAD_LIBRARY";
		case XESS_RESULT_ERROR_WRONG_CALL_ORDER:
			return "ERROR_WRONG_CALL_ORDER";
		default:
			return "ERROR_UNKNOWN";
	}
}

void XeSSLibrary::log_callback(const char *p_message, xess_logging_level_t p_level) {
	// Called from any thread, also concurrently.
	if (p_level >= XESS_LOGGING_LEVEL_ERROR) {
		ERR_PRINT(vformat("XeSS: %s", String::utf8(p_message)));
	} else if (p_level >= XESS_LOGGING_LEVEL_WARNING) {
		WARN_PRINT(vformat("XeSS: %s", String::utf8(p_message)));
	} else {
		print_verbose(vformat("XeSS: %s", String::utf8(p_message)));
	}
}

#endif // XESS_ENABLED
