/**************************************************************************/
/*  xess.cpp                                                              */
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

#include "xess.h"

#ifdef XESS_ENABLED

#include <xess.h>

using namespace RendererRD;

// The smallest preset whose dynamic range (1x up to its scale) holds the
// upscale; at 1x XeSS anti-aliases at native resolution.
static xess_quality_settings_t _quality_for_upscale(float p_upscale) {
	if (p_upscale <= 1.001f) {
		return XESS_QUALITY_SETTING_AA;
	} else if (p_upscale <= 1.501f) {
		return XESS_QUALITY_SETTING_ULTRA_QUALITY;
	} else if (p_upscale <= 1.701f) {
		return XESS_QUALITY_SETTING_QUALITY;
	} else if (p_upscale <= 2.001f) {
		return XESS_QUALITY_SETTING_BALANCED;
	} else if (p_upscale <= 2.301f) {
		return XESS_QUALITY_SETTING_PERFORMANCE;
	}
	return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
}

XeSSContext *XeSSEffect::create_context(Size2i p_internal_size, Size2i p_target_size) {
	_free_pending(false);

	RDD::XeSSContextCreateInfo info;
	info.output_size = p_target_size;
	info.quality = _quality_for_upscale(MAX(float(p_target_size.width) / p_internal_size.width, float(p_target_size.height) / p_internal_size.height));
	// Godot draws reversed Z, with velocity at the render resolution (scaled to
	// pixels at execution) and HDR colour before tonemapping.
	info.init_flags = XESS_INIT_FLAG_INVERTED_DEPTH;

	XeSSContext *context = memnew(XeSSContext);
	context->handle = RD::get_singleton()->get_device_driver()->xess_context_create(info);
	ERR_FAIL_NULL_V_MSG(context->handle, context, "XeSS: the context could not be created; the 3D image will not be upscaled.");
	return context;
}

void XeSSEffect::free_context(XeSSContext *p_context) {
	if (p_context->handle != nullptr) {
		pending_free.push_back({ p_context->handle, RD::get_singleton()->get_frames_drawn() });
	}
	memdelete(p_context);
}

void XeSSEffect::_free_pending(bool p_all) {
	RenderingDevice *rd = RD::get_singleton();
	uint64_t frames_drawn = rd->get_frames_drawn();
	uint64_t frame_delay = rd->get_frame_delay();
	for (uint32_t i = 0; i < pending_free.size();) {
		if (p_all || frames_drawn > pending_free[i].frame + frame_delay) {
			rd->get_device_driver()->xess_context_free(pending_free[i].handle);
			pending_free.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

void XeSSEffect::upscale(const Parameters &p_params) {
	_free_pending(false);
	if (p_params.context->handle == nullptr) {
		return;
	}

	RenderingDevice *rd = RD::get_singleton();
	CallbackArgs *args = args_allocator.alloc();
	args->owner = this;
	args->context = p_params.context->handle;
	RDD::XeSSExecuteInfo &info = args->info;
	info.color = rd->texture_get_driver_id(p_params.color);
	info.velocity = rd->texture_get_driver_id(p_params.velocity);
	info.depth = rd->texture_get_driver_id(p_params.depth);
	info.output = rd->texture_get_driver_id(p_params.output);
	info.input_size = p_params.internal_size;
	// Godot's velocity is the previous minus the current position in UV; XeSS
	// takes the same direction in pixels, as FSR 2.2 does.
	info.velocity_scale = Vector2(p_params.internal_size);
	info.jitter = p_params.jitter;
	info.exposure_scale = p_params.exposure_scale;
	info.reset = p_params.context->reset;
	p_params.context->reset = false;

	RD::CallbackResource resources[4] = {
		{ p_params.color, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.velocity, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.depth, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.output, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
	};
	rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback, args, VectorView<RD::CallbackResource>(resources, 4));
}

void XeSSEffect::callback(RDD *p_driver, RDD::CommandBufferID p_command_buffer, CallbackArgs *p_userdata) {
	p_driver->command_xess_execute(p_command_buffer, p_userdata->context, p_userdata->info);
	CallbackArgs::free(&p_userdata);
}

XeSSEffect::~XeSSEffect() {
	_free_pending(true);
}

#endif // XESS_ENABLED
