/**************************************************************************/
/*  xess_library.h                                                        */
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

#ifdef XESS_ENABLED

#include "core/string/ustring.h"

#include <xess.h>

// Intel XeSS super resolution (https://github.com/intel/xess), loaded from
// libxess.dll at run time: Intel ships an import library for MSVC only, and the
// official Windows builds are made with MinGW. The DLL is not part of Godot; a
// game ships it beside its executable (the editor also looks in the project's
// folder). Without it, or on a GPU XeSS cannot run on, the drivers report no
// SUPPORTS_XESS and Viewport's XeSS mode falls back to FSR 2.
// The library stays loaded until the process ends.
class XeSSLibrary {
	void *library = nullptr;
	bool load_attempted = false;
	String loaded_path;

	static XeSSLibrary singleton;

	void *_get_proc(const char *p_name) const;

public:
	// Functions common to the D3D12 and Vulkan contexts (xess.h). The drivers
	// get their own with get_proc().
	decltype(&xessGetVersion) GetVersion = nullptr;
	decltype(&xessDestroyContext) DestroyContext = nullptr;
	decltype(&xessIsOptimalDriver) IsOptimalDriver = nullptr;
	decltype(&xessSetLoggingCallback) SetLoggingCallback = nullptr;
	decltype(&xessGetOptimalInputResolution) GetOptimalInputResolution = nullptr;
	decltype(&xessSetVelocityScale) SetVelocityScale = nullptr;
	decltype(&xessSetJitterScale) SetJitterScale = nullptr;

	// Loads libxess.dll once; false if it is missing or lacks a function.
	static bool load();
	static XeSSLibrary &get() { return singleton; }

	template <typename T>
	bool get_proc(T &r_function, const char *p_name) const {
		r_function = reinterpret_cast<T>(_get_proc(p_name));
		return r_function != nullptr;
	}

	String get_version_string() const;
	static const char *result_to_string(xess_result_t p_result);
	// Prints XeSS's own warnings and errors.
	static void log_callback(const char *p_message, xess_logging_level_t p_level);
};

#endif // XESS_ENABLED
