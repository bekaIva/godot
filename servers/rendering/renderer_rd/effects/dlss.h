/**************************************************************************/
/*  dlss.h                                                                */
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

#include "core/templates/paged_allocator.h"
#include "drivers/vulkan/ngx_vulkan.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// One DLSS feature per render buffer size. NGX records its creation into a
// command buffer, so the feature is made on the first upscale.
struct DLSSContext {
	NVSDK_NGX_Handle *feature = nullptr;
	Size2i render_size;
	Size2i output_size;
	bool reset = true; // No history yet.
	bool failed = false; // NGX could not create the feature: not retried every frame.

	~DLSSContext();
};

class DLSSEffect {
	struct CallbackArgs {
		DLSSEffect *owner = nullptr;
		DLSSContext *context = nullptr;
		NGXVulkan::DLSSEvaluate evaluate;

		static void free(CallbackArgs **p_args) {
			(*p_args)->owner->args_allocator.free(*p_args);
			*p_args = nullptr;
		}
	};

	PagedAllocator<CallbackArgs, true, 16> args_allocator;
	static void callback(RDD *p_driver, RDD::CommandBufferID p_command_buffer, CallbackArgs *p_args);

public:
	struct Parameters {
		RID color;
		RID depth;
		RID velocity; // Every pixel's motion (MotionVectorsStore, then the motion pass).
		RID output;
		Vector2 jitter; // Render pixels.
		float delta_time = 0.0;
	};

	DLSSContext *create_context(Size2i p_render_size, Size2i p_output_size) const;
	void upscale(DLSSContext *p_context, const Parameters &p_params);

	DLSSEffect();
	~DLSSEffect();
};

} // namespace RendererRD

#endif // DLSS_ENABLED
