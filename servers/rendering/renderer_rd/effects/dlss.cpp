/**************************************************************************/
/*  dlss.cpp                                                              */
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

#include "dlss.h"

#ifdef DLSS_ENABLED

using namespace RendererRD;

DLSSContext::~DLSSContext() {
	// The GPU may still run this frame's evaluation.
	NGXLibrary::release_dlss(feature, RD::get_singleton()->get_frames_drawn());
}

DLSSEffect::DLSSEffect() {
	DEV_ASSERT(RD::get_singleton()->has_feature(RD::SUPPORTS_DLSS));
}

DLSSEffect::~DLSSEffect() {
}

DLSSContext *DLSSEffect::create_context(Size2i p_render_size, Size2i p_output_size) const {
	DLSSContext *context = memnew(DLSSContext);
	context->render_size = p_render_size;
	context->output_size = p_output_size;
	return context;
}

void DLSSEffect::upscale(DLSSContext *p_context, const Parameters &p_params) {
	RD *rd = RD::get_singleton();
	// Features dropped on a resize, once the GPU is past their last frame.
	uint64_t frames_drawn = rd->get_frames_drawn();
	uint64_t frame_delay = rd->get_frame_delay();
	if (frames_drawn > frame_delay) {
		NGXLibrary::release_finished_dlss(frames_drawn - frame_delay - 1);
	}

	CallbackArgs *args = args_allocator.alloc();
	args->owner = this;
	args->context = p_context;
	RDD::DLSSEvaluateInfo &info = args->info;
	info.color = rd->texture_get_driver_id(p_params.color);
	info.depth = rd->texture_get_driver_id(p_params.depth);
	info.motion = rd->texture_get_driver_id(p_params.velocity);
	info.output = rd->texture_get_driver_id(p_params.output);
	info.render_size = p_context->render_size;
	info.output_size = p_context->output_size;
	info.jitter = p_params.jitter;
	info.frame_time_ms = p_params.delta_time * 1000.0;
	info.reset = p_context->reset;
	p_context->reset = false;

	// DLSS reads its inputs from compute shaders and writes the output as a
	// storage image; the render graph puts each in that state.
	RD::CallbackResource resources[4] = {
		{ p_params.color, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.depth, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.velocity, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
		{ p_params.output, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
	};
	rd->driver_callback_add((RDD::DriverCallback)DLSSEffect::callback, args, VectorView<RD::CallbackResource>(resources, 4));
}

void DLSSEffect::callback(RDD *p_driver, RDD::CommandBufferID p_command_buffer, CallbackArgs *p_args) {
	DLSSContext *context = p_args->context;
	if (!context->failed) {
		context->failed = !p_driver->command_dlss_evaluate(p_command_buffer, context->feature, p_args->info);
	}
	CallbackArgs::free(&p_args);
}

#endif // DLSS_ENABLED
