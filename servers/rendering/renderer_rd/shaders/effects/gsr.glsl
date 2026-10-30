/* clang-format off */
#[vertex]

#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;
/* clang-format on */

void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
	uv_interp = clamp(gl_Position.xy, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; // saturate(x) * 2.0
}

/* clang-format off */
#[fragment]

#version 450

#VERSION_DEFINES

// Snapdragon Game Super Resolution 1, the RGBA operation mode of
// sgsr1_shader_mobile.frag from https://github.com/SnapdragonStudios/snapdragon-gsr
// (commit d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e), adapted to Godot's
// RenderingDevice shaders (inputs, alpha kept).
//
// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

layout(location = 0) in vec2 uv_interp;
/* clang-format on */

layout(set = 0, binding = 0) uniform sampler2D source_color;

layout(push_constant, std430) uniform Params {
	vec4 viewport_info; // 1 / input size, input size.
	float edge_sharpness;
	float pad[3];
}
params;

layout(location = 0) out vec4 frag_color;

#define EDGE_THRESHOLD (8.0 / 255.0)

float fast_lanczos2(float x) {
	float wA = x - 4.0;
	float wB = x * wA - wA;
	wA *= wA;
	return wB * wA;
}

vec2 weight_y(float dx, float dy, float c, float std) {
	float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
	float w = fast_lanczos2(x);
	return vec2(w, w * c);
}

void main() {
	// RGBA mode: the green channel stands in for luma.
	const int mode = 1;
	vec4 source = textureLod(source_color, uv_interp, 0.0);
	vec4 color = vec4(source.rgb, 0.0);

	highp vec2 img_coord = ((uv_interp * params.viewport_info.zw) + vec2(-0.5, 0.5));
	highp vec2 img_coord_pixel = floor(img_coord);
	highp vec2 coord = (img_coord_pixel * params.viewport_info.xy);
	vec2 pl = (img_coord + (-img_coord_pixel));
	vec4 left = textureGather(source_color, coord, mode);

	float edge_vote = abs(left.z - left.y) + abs(color[mode] - left.y) + abs(color[mode] - left.z);
	if (edge_vote > EDGE_THRESHOLD) {
		coord.x += params.viewport_info.x;

		vec4 right = textureGather(source_color, coord + vec2(params.viewport_info.x, 0.0), mode);
		vec4 up_down;
		up_down.xy = textureGather(source_color, coord + vec2(0.0, -params.viewport_info.y), mode).wz;
		up_down.zw = textureGather(source_color, coord + vec2(0.0, params.viewport_info.y), mode).yx;

		float mean = (left.y + left.z + right.x + right.w) * 0.25;
		left = left - vec4(mean);
		right = right - vec4(mean);
		up_down = up_down - vec4(mean);
		color.w = color[mode] - mean;

		float sum = (((((abs(left.x) + abs(left.y)) + abs(left.z)) + abs(left.w)) + (((abs(right.x) + abs(right.y)) + abs(right.z)) + abs(right.w))) + (((abs(up_down.x) + abs(up_down.y)) + abs(up_down.z)) + abs(up_down.w)));
		float std = 2.181818 / sum;

		vec2 aWY = weight_y(pl.x, pl.y + 1.0, up_down.x, std);
		aWY += weight_y(pl.x - 1.0, pl.y + 1.0, up_down.y, std);
		aWY += weight_y(pl.x - 1.0, pl.y - 2.0, up_down.z, std);
		aWY += weight_y(pl.x, pl.y - 2.0, up_down.w, std);
		aWY += weight_y(pl.x + 1.0, pl.y - 1.0, left.x, std);
		aWY += weight_y(pl.x, pl.y - 1.0, left.y, std);
		aWY += weight_y(pl.x, pl.y, left.z, std);
		aWY += weight_y(pl.x + 1.0, pl.y, left.w, std);
		aWY += weight_y(pl.x - 1.0, pl.y - 1.0, right.x, std);
		aWY += weight_y(pl.x - 2.0, pl.y - 1.0, right.y, std);
		aWY += weight_y(pl.x - 2.0, pl.y, right.z, std);
		aWY += weight_y(pl.x - 1.0, pl.y, right.w, std);

		float final_y = aWY.y / aWY.x;

		float max_y = max(max(left.y, left.z), max(right.x, right.w));
		float min_y = min(min(left.y, left.z), min(right.x, right.w));
		final_y = clamp(params.edge_sharpness * final_y, min_y, max_y);

		float delta_y = final_y - color.w;

		// Smooth high contrast input.
		delta_y = clamp(delta_y, -23.0 / 255.0, 23.0 / 255.0);

		color.x = clamp((color.x + delta_y), 0.0, 1.0);
		color.y = clamp((color.y + delta_y), 0.0, 1.0);
		color.z = clamp((color.z + delta_y), 0.0, 1.0);
	}

	frag_color = vec4(color.rgb, source.a);
}
