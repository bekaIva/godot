/**************************************************************************/
/*  xess.h                                                                */
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

#include "core/templates/local_vector.h"
#include "core/templates/paged_allocator.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// One per render-buffer size (RenderBufferDataForwardClustered::ensure_xess).
struct XeSSContext {
	void *handle = nullptr; // The driver's XeSS context.
	bool reset = true; // The next frame starts a new history.
};

// Intel XeSS super resolution through the D3D12 and Vulkan drivers
// (RenderingDeviceDriver::command_xess_execute, drivers/xess), from the same
// inputs as FSR 2.2: the internal colour, depth, velocity and jitter.
class XeSSEffect {
	struct CallbackArgs {
		XeSSEffect *owner = nullptr;
		void *context = nullptr;
		RDD::XeSSExecuteInfo info;

		static void free(CallbackArgs **p_args) {
			(*p_args)->owner->args_allocator.free(*p_args);
			*p_args = nullptr;
		}
	};

	PagedAllocator<CallbackArgs, true, 16> args_allocator;
	static void callback(RDD *p_driver, RDD::CommandBufferID p_command_buffer, CallbackArgs *p_userdata);

	// Contexts freed while the GPU may still run frames that use them.
	struct PendingFree {
		void *handle = nullptr;
		uint64_t frame = 0;
	};
	LocalVector<PendingFree> pending_free;
	void _free_pending(bool p_all);

public:
	struct Parameters {
		XeSSContext *context = nullptr;
		Size2i internal_size;
		RID color;
		RID depth;
		RID velocity;
		RID output;
		Vector2 jitter; // Pixels.
		float exposure_scale = 1.0f;
	};

	XeSSContext *create_context(Size2i p_internal_size, Size2i p_target_size);
	void free_context(XeSSContext *p_context);
	void upscale(const Parameters &p_params);

	~XeSSEffect();
};

} // namespace RendererRD

#endif // XESS_ENABLED
