/**************************************************************************/
/*  gsr.h                                                                 */
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

#include "servers/rendering/renderer_rd/effects/spatial_upscaler.h"
#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/gsr.glsl.gen.h"

namespace RendererRD {

// Snapdragon Game Super Resolution 1 (Qualcomm, BSD-3-Clause): a one-pass
// spatial upscaler with a 12-tap Lanczos-like filter and edge sharpening,
// made for mobile GPUs. A fragment shader, so it runs where the render
// target cannot be a storage image (the Mobile renderer), unlike FSR 1.
class GSR : public SpatialUpscaler {
	struct PushConstant {
		float viewport_info[4]; // 1 / input size, input size.
		float edge_sharpness;
		float pad[3];
	};

	GsrShaderRD shader;
	RID shader_version;
	PipelineCacheRD pipeline;

public:
	virtual const Span<char> get_label() const final { return "Snapdragon GSR"; }
	virtual void ensure_context(Ref<RenderSceneBuffersRD> p_render_buffers) final {}
	virtual void process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_source_rd_texture, RID p_destination_texture) final;

	GSR();
	~GSR();
};

} // namespace RendererRD
