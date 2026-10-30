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
	NGXVulkan::release_dlss(feature, RD::get_singleton()->get_frames_drawn());
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

static NGXVulkan::Image _driver_image(RID p_texture) {
	RD *rd = RD::get_singleton();
	NGXVulkan::Image image;
	image.image = rd->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE, p_texture);
	image.view = rd->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE_VIEW, p_texture);
	image.format = rd->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE_DATA_FORMAT, p_texture);
	return image;
}

void DLSSEffect::upscale(DLSSContext *p_context, const Parameters &p_params) {
	RD *rd = RD::get_singleton();
	// Features dropped on a resize, once the GPU is past their last frame.
	uint64_t frames_drawn = rd->get_frames_drawn();
	uint64_t frame_delay = rd->get_frame_delay();
	if (frames_drawn > frame_delay) {
		NGXVulkan::release_finished_dlss(frames_drawn - frame_delay - 1);
	}

	CallbackArgs *args = args_allocator.alloc();
	args->owner = this;
	args->context = p_context;
	NGXVulkan::DLSSEvaluate &evaluate = args->evaluate;
	evaluate.color = _driver_image(p_params.color);
	evaluate.depth = _driver_image(p_params.depth);
	evaluate.motion = _driver_image(p_params.velocity);
	evaluate.output = _driver_image(p_params.output);
	evaluate.render_width = p_context->render_size.width;
	evaluate.render_height = p_context->render_size.height;
	evaluate.output_width = p_context->output_size.width;
	evaluate.output_height = p_context->output_size.height;
	evaluate.jitter_x = p_params.jitter.x;
	evaluate.jitter_y = p_params.jitter.y;
	evaluate.frame_time_ms = p_params.delta_time * 1000.0;
	evaluate.reset = p_context->reset;
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
		context->failed = !NGXVulkan::evaluate_dlss(p_driver, p_command_buffer, context->feature, p_args->evaluate);
	}
	CallbackArgs::free(&p_args);
}

#endif // DLSS_ENABLED
