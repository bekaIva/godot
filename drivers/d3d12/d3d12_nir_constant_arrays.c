/**************************************************************************/
/*  d3d12_nir_constant_arrays.c                                           */
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

// SCsub defines `_Static_assert` as `static_assert` for the C++ files that include Mesa's headers.
// This is the only C file there, and in C `_Static_assert` is the keyword itself: with GCC and Clang
// the define leaves Mesa's `static_assert` undeclared (MinGW builds fail), so undo it here.
#if defined(__GNUC__) && !defined(__cplusplus)
#undef _Static_assert
#endif

#include "d3d12_nir_constant_arrays.h"

#include "nir.h"
#include "nir_builder.h"

// Constant arrays in GLSL (e.g. `const vec4 samples[32] = { ... };` indexed in a loop) reach NIR as
// function-local arrays that every invocation fills with constant stores and then reads with dynamic
// indices. NIR to DXIL turns those into per-thread `alloca` arrays, which drivers may place in scratch
// memory, so each invocation writes and reads the whole table through memory. Instead, move such arrays
// into a single constant global, which NIR to DXIL emits as a DXIL constant like `static const` in HLSL.

static bool lower_load_constant(nir_builder *b, nir_intrinsic_instr *intr, void *data) {
	if (intr->intrinsic != nir_intrinsic_load_constant) {
		return false;
	}

	nir_variable *words_var = (nir_variable *)data;
	const unsigned bit_size = intr->def.bit_size;
	const unsigned byte_size = bit_size / 8;

	b->cursor = nir_before_instr(&intr->instr);
	nir_def *offset = nir_iadd_imm(b, intr->src[0].ssa, nir_intrinsic_base(intr));
	nir_deref_instr *words = nir_build_deref_var(b, words_var);

	nir_def *components[NIR_MAX_VEC_COMPONENTS];
	for (unsigned i = 0; i < intr->def.num_components; i++) {
		nir_def *byte = nir_iadd_imm(b, offset, i * byte_size);
		nir_def *word_index = nir_ushr_imm(b, byte, 2);
		nir_def *word = nir_load_deref(b, nir_build_deref_array(b, words, word_index));
		if (bit_size == 32) {
			components[i] = word;
		} else if (bit_size == 64) {
			nir_def *high = nir_load_deref(b, nir_build_deref_array(b, words, nir_iadd_imm(b, word_index, 1)));
			components[i] = nir_pack_64_2x32_split(b, word, high);
		} else {
			nir_def *shift = nir_imul_imm(b, nir_iand_imm(b, byte, 3), 8);
			components[i] = nir_u2uN(b, nir_ushr(b, word, shift), bit_size);
		}
	}

	nir_def_replace(&intr->def, nir_vec(b, components, intr->def.num_components));
	return true;
}

bool godot_nir_lower_constant_arrays_to_global(nir_shader *p_shader) {
	if (p_shader->constant_data_size != 0) {
		// Not expected from SPIR-V; keep the shader as it is rather than mixing two sets of constant data.
		return false;
	}

	nir_lower_var_copies(p_shader);
	if (!nir_opt_large_constants(p_shader, glsl_get_natural_size_align_bytes, 16)) {
		return false;
	}

	const unsigned word_count = DIV_ROUND_UP(p_shader->constant_data_size, 4);
	nir_variable *words_var = nir_variable_create(p_shader, nir_var_mem_constant, glsl_array_type(glsl_uint_type(), word_count, 4), "constant_arrays");

	nir_constant *initializer = rzalloc(p_shader, nir_constant);
	initializer->num_elements = word_count;
	initializer->elements = rzalloc_array(p_shader, nir_constant *, word_count);
	for (unsigned i = 0; i < word_count; i++) {
		uint32_t value = 0;
		memcpy(&value, (const uint8_t *)p_shader->constant_data + i * 4, MIN2(4, p_shader->constant_data_size - i * 4));
		nir_constant *element = rzalloc(p_shader, nir_constant);
		element->values[0].u32 = value;
		element->is_null_constant = value == 0;
		initializer->elements[i] = element;
	}
	words_var->constant_initializer = initializer;

	nir_shader_intrinsics_pass(p_shader, lower_load_constant, nir_metadata_control_flow, words_var);

	p_shader->constant_data_size = 0;
	return true;
}
