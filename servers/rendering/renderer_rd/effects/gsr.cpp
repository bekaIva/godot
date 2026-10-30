/**************************************************************************/
/*  gsr.cpp                                                               */
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

#include "gsr.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

GSR::GSR() {
	Vector<String> modes;
	modes.push_back("");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline.setup(shader.version_get_shader(shader_version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), RD::PipelineColorBlendState::create_disabled(), 0);
}

GSR::~GSR() {
	pipeline.clear();
	shader.version_free(shader_version);
}

void GSR::process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_source_rd_texture, RID p_destination_texture) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	Size2i internal_size = p_render_buffers->get_internal_size();

	PushConstant push_constant = {};
	push_constant.viewport_info[0] = 1.0 / internal_size.width;
	push_constant.viewport_info[1] = 1.0 / internal_size.height;
	push_constant.viewport_info[2] = internal_size.width;
	push_constant.viewport_info[3] = internal_size.height;
	// Qualcomm's default (1 to 2); Godot's FSR sharpness, where 0 is sharpest, maps onto it.
	push_constant.edge_sharpness = CLAMP(2.0 - p_render_buffers->get_fsr_sharpness() * 0.5, 1.0, 2.0);

	RID shader_rid = shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader_rid.is_null());

	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source_rd_texture }));

	RID framebuffer = FramebufferCacheRD::get_singleton()->get_cache(p_destination_texture);
	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(framebuffer, RD::DRAW_IGNORE_COLOR_ALL);
	RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipeline.get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(framebuffer)));
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader_rid, 0, u_source), 0);
	RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
	RD::get_singleton()->draw_list_end();
}
