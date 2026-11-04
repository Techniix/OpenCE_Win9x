/*
RETRO_VERTEX.C

The game's vertex programs (NV2A vertex shader microcode) run on the
processor, as the cards of 1998 to 2001 have no programmable vertex stage
(a Voodoo3 has none at all: the processor transforms every vertex for it).
Each program is decoded once into retro_vertex_instruction (the fields as
port/linux/src/nv2a_vsh.c reads them, where each is explained), then run on
each vertex of a draw. The position it leaves in screen space is turned
back into clip space for OpenGL, as nv2a_vsh.c's GLSL does.
*/

#include "retro_internal.h"

#include <math.h>
#include <string.h>

enum
{
	_mac_nop, _mac_mov, _mac_mul, _mac_add, _mac_mad, _mac_dp3, _mac_dph, _mac_dp4,
	_mac_dst, _mac_min, _mac_max, _mac_slt, _mac_sge, _mac_arl,
};

enum
{
	_ilu_nop, _ilu_mov, _ilu_rcp, _ilu_rcc, _ilu_rsq, _ilu_exp, _ilu_log, _ilu_lit,
};

enum
{
	_mux_unknown, _mux_temporary, _mux_input, _mux_constant,
};

/* the output registers (o[]) a program writes, by their address */
enum
{
	_output_position = 0,
	_output_diffuse = 3,
	_output_specular = 4,
	_output_fog = 5,
	_output_point_size = 6,
	_output_back_diffuse = 7,
	_output_back_specular = 8,
	_output_texture0 = 9,
	_output_count = 13,
};

/* the register file while a vertex runs: r0 to r11, then r12 (the
position output, which r12 reads back) */
#define POSITION_REGISTER 12

/* ---------- decoding */

static unsigned long field(const DWORD *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1UL << bit_count) - 1);
}

static void operand_decode(const DWORD *instruction, char which, struct retro_vertex_operand *operand)
{
	switch (which)
	{
	case 'A':
		operand->negate = (unsigned char)field(instruction, 1, 8, 1);
		operand->swizzle[0] = (unsigned char)field(instruction, 1, 6, 2);
		operand->swizzle[1] = (unsigned char)field(instruction, 1, 4, 2);
		operand->swizzle[2] = (unsigned char)field(instruction, 1, 2, 2);
		operand->swizzle[3] = (unsigned char)field(instruction, 1, 0, 2);
		operand->index = (unsigned char)field(instruction, 2, 28, 4);
		operand->mux = (unsigned char)field(instruction, 2, 26, 2);
		break;
	case 'B':
		operand->negate = (unsigned char)field(instruction, 2, 25, 1);
		operand->swizzle[0] = (unsigned char)field(instruction, 2, 23, 2);
		operand->swizzle[1] = (unsigned char)field(instruction, 2, 21, 2);
		operand->swizzle[2] = (unsigned char)field(instruction, 2, 19, 2);
		operand->swizzle[3] = (unsigned char)field(instruction, 2, 17, 2);
		operand->index = (unsigned char)field(instruction, 2, 13, 4);
		operand->mux = (unsigned char)field(instruction, 2, 11, 2);
		break;
	default:
		operand->negate = (unsigned char)field(instruction, 2, 10, 1);
		operand->swizzle[0] = (unsigned char)field(instruction, 2, 8, 2);
		operand->swizzle[1] = (unsigned char)field(instruction, 2, 6, 2);
		operand->swizzle[2] = (unsigned char)field(instruction, 2, 4, 2);
		operand->swizzle[3] = (unsigned char)field(instruction, 2, 2, 2);
		operand->index = (unsigned char)((field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2));
		operand->mux = (unsigned char)field(instruction, 3, 28, 2);
		break;
	}
}

void retro_vertex_program_decode(struct retro_vertex_program *program, const DWORD *instructions,
	unsigned long instruction_count)
{
	unsigned long index;

	program->instruction_count = 0;
	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		struct retro_vertex_instruction *decoded = &program->instructions[program->instruction_count++];

		memset(decoded, 0, sizeof(*decoded));
		decoded->mac = (unsigned char)field(instruction, 1, 21, 4);
		decoded->ilu = (unsigned char)field(instruction, 1, 25, 3);
		decoded->mac_mask = (unsigned char)field(instruction, 3, 24, 4);
		decoded->temporary = (unsigned char)field(instruction, 3, 20, 4);
		decoded->ilu_mask = (unsigned char)field(instruction, 3, 16, 4);
		decoded->output_mask = (unsigned char)field(instruction, 3, 12, 4);
		decoded->output_register = (unsigned char)field(instruction, 3, 11, 1);
		decoded->output_address = (unsigned char)field(instruction, 3, 3, 8);
		decoded->output_from_ilu = (unsigned char)field(instruction, 3, 2, 1);
		decoded->relative = (unsigned char)field(instruction, 3, 1, 1);
		decoded->constant = (unsigned short)field(instruction, 1, 13, 8);
		decoded->input = (unsigned short)field(instruction, 1, 9, 4);
		/* (the ILU writes r1 when both units run) */
		decoded->ilu_temporary = decoded->mac != _mac_nop ? 1 : decoded->temporary;
		operand_decode(instruction, 'A', &decoded->a);
		operand_decode(instruction, 'B', &decoded->b);
		operand_decode(instruction, 'C', &decoded->c);
		decoded->captures_clip = decoded->ilu == _ilu_rcc && decoded->c.mux == _mux_temporary &&
			decoded->c.index == POSITION_REGISTER;
		/* (only the outputs the device reads: a write elsewhere is none) */
		if (!decoded->output_register || decoded->output_address >= _output_count)
			decoded->output_mask = 0;
		if (field(instruction, 3, 0, 1))
			break;
	}
}

/* ---------- the stream's formats */

void retro_vertex_fetch(const struct retro_vertex_element *element, const unsigned char *source, float *value)
{
	const float *floats = (const float *)source;
	const short *shorts = (const short *)source;
	int count = 0, index;

	value[0] = value[1] = value[2] = 0.0f;
	value[3] = 1.0f;
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: count = 1; break;
	case D3DVSDT_FLOAT2: count = 2; break;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: count = 3; break;
	case D3DVSDT_FLOAT4: count = 4; break;
	case D3DVSDT_D3DCOLOR:
		/* (B, G, R, A in memory; x is red) */
		value[0] = source[2] * (1.0f / 255.0f);
		value[1] = source[1] * (1.0f / 255.0f);
		value[2] = source[0] * (1.0f / 255.0f);
		value[3] = source[3] * (1.0f / 255.0f);
		return;
	case D3DVSDT_SHORT1: case D3DVSDT_SHORT2: case D3DVSDT_SHORT3: case D3DVSDT_SHORT4:
		count = element->type == D3DVSDT_SHORT1 ? 1 : element->type == D3DVSDT_SHORT2 ? 2 :
			element->type == D3DVSDT_SHORT3 ? 3 : 4;
		for (index = 0; index < count; index++)
			value[index] = (float)shorts[index];
		return;
	case D3DVSDT_NORMSHORT1: case D3DVSDT_NORMSHORT2: case D3DVSDT_NORMSHORT3: case D3DVSDT_NORMSHORT4:
		count = element->type == D3DVSDT_NORMSHORT1 ? 1 : element->type == D3DVSDT_NORMSHORT2 ? 2 :
			element->type == D3DVSDT_NORMSHORT3 ? 3 : 4;
		for (index = 0; index < count; index++)
		{
			float normalized = shorts[index] * (1.0f / 32767.0f);

			value[index] = normalized < -1.0f ? -1.0f : normalized;
		}
		return;
	case D3DVSDT_PBYTE1: case D3DVSDT_PBYTE2: case D3DVSDT_PBYTE3: case D3DVSDT_PBYTE4:
		count = element->type == D3DVSDT_PBYTE1 ? 1 : element->type == D3DVSDT_PBYTE2 ? 2 :
			element->type == D3DVSDT_PBYTE3 ? 3 : 4;
		for (index = 0; index < count; index++)
			value[index] = source[index] * (1.0f / 255.0f);
		return;
	case D3DVSDT_NORMPACKED3:
	{
		DWORD packed = (DWORD)source[0] | ((DWORD)source[1] << 8) | ((DWORD)source[2] << 16) |
			((DWORD)source[3] << 24);
		long x = (long)(packed << 21) >> 21;
		long y = (long)(packed << 10) >> 21;
		long z = (long)packed >> 22;

		value[0] = x / 1023.0f;
		value[1] = y / 1023.0f;
		value[2] = z / 511.0f;
		return;
	}
	default:
		count = 4;
		break;
	}
	for (index = 0; index < count; index++)
		value[index] = floats[index];
}

static unsigned long type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	default: return 0;
	}
}

void retro_vertex_declaration_parse(struct retro_vertex_program *program, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				/* skip: the count is in dwords, or in bytes with bit 27 */
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (program->element_count < RETRO_ATTRIBUTE_COUNT)
			{
				struct retro_vertex_element *element = &program->elements[program->element_count++];

				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->type = (unsigned char)((token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT);
				element->bytes = (unsigned char)type_bytes(element->type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}

/* ---------- running */

static float clamp_unit(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

static unsigned char color_byte(float value)
{
	return (unsigned char)(clamp_unit(value) * 255.0f + 0.5f);
}

void retro_vertex_run(const struct retro_vertex_program *program, const struct retro_vertex_state *state,
	const float (*inputs)[4], struct retro_vertex *out)
{
	/* r0 to r11 and the position (r12) */
	float registers[13][4];
	float outputs[_output_count][4];
	float clip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	int clip_captured = FALSE;
	long a0 = 0;
	unsigned long index;
	int component;

	memset(registers, 0, sizeof(registers));
	registers[POSITION_REGISTER][3] = 1.0f;
	for (index = 0; index < _output_count; index++)
	{
		outputs[index][0] = outputs[index][1] = outputs[index][2] = 0.0f;
		outputs[index][3] = 1.0f;
	}
	outputs[_output_fog][0] = outputs[_output_fog][1] = outputs[_output_fog][2] = 1.0f;

	for (index = 0; index < program->instruction_count; index++)
	{
		const struct retro_vertex_instruction *instruction = &program->instructions[index];
		const struct retro_vertex_operand *operands[3];
		float values[3][4];
		float mac[4], ilu[4];
		int which;

		operands[0] = &instruction->a;
		operands[1] = &instruction->b;
		operands[2] = &instruction->c;
		for (which = 0; which < 3; which++)
		{
			const struct retro_vertex_operand *operand = operands[which];
			const float *source;
			long constant;

			switch (operand->mux)
			{
			case _mux_temporary:
				source = registers[operand->index < 13 ? operand->index : 0];
				break;
			case _mux_input:
				source = inputs[instruction->input];
				break;
			case _mux_constant:
				constant = instruction->constant + (instruction->relative ? a0 : 0);
				if (constant < 0)
					constant = 0;
				if (constant >= RETRO_CONSTANT_COUNT)
					constant = RETRO_CONSTANT_COUNT - 1;
				source = state->constants[constant];
				break;
			default:
				source = NULL;
				break;
			}
			for (component = 0; component < 4; component++)
			{
				float value = source ? source[operand->swizzle[component]] : 0.0f;

				values[which][component] = operand->negate ? -value : value;
			}
		}
#define A values[0]
#define B values[1]
#define C values[2]
		switch (instruction->mac)
		{
		case _mac_mov: case _mac_arl:
			memcpy(mac, A, sizeof(mac));
			break;
		case _mac_mul:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] * B[component];
			break;
		case _mac_add:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] + C[component];
			break;
		case _mac_mad:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] * B[component] + C[component];
			break;
		case _mac_dp3:
			mac[0] = mac[1] = mac[2] = mac[3] = A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
			break;
		case _mac_dph:
			mac[0] = mac[1] = mac[2] = mac[3] = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + B[3];
			break;
		case _mac_dp4:
			mac[0] = mac[1] = mac[2] = mac[3] = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3];
			break;
		case _mac_dst:
			mac[0] = 1.0f;
			mac[1] = A[1] * B[1];
			mac[2] = A[2];
			mac[3] = B[3];
			break;
		case _mac_min:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] < B[component] ? A[component] : B[component];
			break;
		case _mac_max:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] > B[component] ? A[component] : B[component];
			break;
		case _mac_slt:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] < B[component] ? 1.0f : 0.0f;
			break;
		case _mac_sge:
			for (component = 0; component < 4; component++)
				mac[component] = A[component] >= B[component] ? 1.0f : 0.0f;
			break;
		default:
			mac[0] = mac[1] = mac[2] = mac[3] = 0.0f;
			break;
		}
		switch (instruction->ilu)
		{
		case _ilu_mov:
			memcpy(ilu, C, sizeof(ilu));
			break;
		case _ilu_rcp:
			ilu[0] = ilu[1] = ilu[2] = ilu[3] = 1.0f / C[0];
			break;
		case _ilu_rcc:
		{
			float r = 1.0f / C[0];

			if (r > 0.0f)
				r = r < 5.42101e-20f ? 5.42101e-20f : r > 1.884467e+19f ? 1.884467e+19f : r;
			else
				r = r < -1.884467e+19f ? -1.884467e+19f : r > -5.42101e-20f ? -5.42101e-20f : r;
			ilu[0] = ilu[1] = ilu[2] = ilu[3] = r;
			break;
		}
		case _ilu_rsq:
			ilu[0] = ilu[1] = ilu[2] = ilu[3] = 1.0f / sqrtf(fabsf(C[0]));
			break;
		case _ilu_exp:
		{
			float whole = floorf(C[0]);

			ilu[0] = (float)ldexp(1.0, (int)whole);
			ilu[1] = C[0] - whole;
			ilu[2] = (float)pow(2.0, C[0]);
			ilu[3] = 1.0f;
			break;
		}
		case _ilu_log:
		{
			float x = fabsf(C[0]);

			if (x == 0.0f)
			{
				ilu[0] = ilu[2] = -1.0e30f;
				ilu[1] = ilu[3] = 1.0f;
			}
			else
			{
				int exponent;
				double mantissa = frexp(x, &exponent);

				/* (the exponent and mantissa of 1 <= m < 2) */
				ilu[0] = (float)(exponent - 1);
				ilu[1] = (float)(mantissa * 2.0);
				ilu[2] = (float)(log(x) / log(2.0));
				ilu[3] = 1.0f;
			}
			break;
		}
		case _ilu_lit:
		{
			float power = C[3] < -127.9961f ? -127.9961f : C[3] > 127.9961f ? 127.9961f : C[3];

			ilu[0] = 1.0f;
			ilu[1] = C[0] > 0.0f ? C[0] : 0.0f;
			ilu[2] = C[0] > 0.0f ? (float)pow(C[1] > 0.0f ? C[1] : 0.0f, power) : 0.0f;
			ilu[3] = 1.0f;
			break;
		}
		default:
			ilu[0] = ilu[1] = ilu[2] = ilu[3] = 0.0f;
			break;
		}
#undef A
#undef B
#undef C
		/* the screen-space conversion reads the clip-space position's w:
		keep the position it converts (retro_vertex_run's end) */
		if (instruction->captures_clip)
		{
			memcpy(clip, registers[POSITION_REGISTER], sizeof(clip));
			clip_captured = TRUE;
		}
		/* results are written only after both units have read their inputs */
		if (instruction->mac == _mac_arl)
		{
			a0 = (long)floorf(mac[0] + 0.001f);
		}
		else if (instruction->mac != _mac_nop && instruction->mac_mask && instruction->temporary <= POSITION_REGISTER)
		{
			for (component = 0; component < 4; component++)
				if (instruction->mac_mask & (8 >> component))
					registers[instruction->temporary][component] = mac[component];
		}
		if (instruction->ilu != _ilu_nop && instruction->ilu_mask && instruction->ilu_temporary <= POSITION_REGISTER)
		{
			for (component = 0; component < 4; component++)
				if (instruction->ilu_mask & (8 >> component))
					registers[instruction->ilu_temporary][component] = ilu[component];
		}
		if (instruction->output_mask && (instruction->output_from_ilu ? instruction->ilu : instruction->mac) != 0)
		{
			const float *source = instruction->output_from_ilu ? ilu : mac;
			float *destination = instruction->output_address == _output_position ? registers[POSITION_REGISTER] :
				outputs[instruction->output_address];

			for (component = 0; component < 4; component++)
				if (instruction->output_mask & (8 >> component))
					destination[component] = source[component];
		}
	}

	/* undo the screen-space conversion done with c[-38] and c[-37], as
	nv2a_vsh.c's GLSL does (where the comments explain it): the position
	in Direct3D's clip space, whose depth OpenGL wants from -w to w */
	{
		const float *position = registers[POSITION_REGISTER];
		const float *viewport_scale = state->constants[RETRO_CONSTANT_BIAS - 38];
		const float *viewport_offset = state->constants[RETRO_CONSTANT_BIAS - 37];
		float scale[3];
		float *result = out->position;

		for (component = 0; component < 3; component++)
			scale[component] = state->viewport_scale[component] != 0.0f ? state->viewport_scale[component] : 1.0f;
		if (clip_captured)
		{
			float centre[3];

			centre[0] = 0.5f + state->screen_offset;
			centre[1] = 0.5f;
			centre[2] = 0.0f;
			for (component = 0; component < 3; component++)
			{
				result[component] = (clip[component] * viewport_scale[component] +
					(viewport_offset[component] + centre[component] - state->viewport_offset[component]) * clip[3]) /
					scale[component];
			}
			result[3] = clip[3];
		}
		else
		{
			result[0] = (position[0] + 0.5f + state->screen_offset - state->viewport_offset[0]) / scale[0] * position[3];
			result[1] = (position[1] + 0.5f - state->viewport_offset[1]) / scale[1] * position[3];
			result[2] = (position[2] - state->viewport_offset[2]) / scale[2] * position[3];
			result[3] = position[3];
		}
		/* (a w of zero or not a number: behind the camera, which the clipper
		takes: nv2a_vsh.c says why) */
		if (!(fabsf(result[3]) > 0.0f))
		{
			result[0] = result[1] = result[2] = 0.0f;
			result[3] = -1.0f;
		}
		/* Direct3D's depth from 0 to w, OpenGL's from -w to w */
		result[2] = 2.0f * result[2] - result[3];
	}
	for (component = 0; component < 4; component++)
	{
		out->diffuse[component] = color_byte(outputs[_output_diffuse][component]);
		out->specular[component] = color_byte(outputs[_output_specular][component]);
	}
	for (index = 0; index < 4; index++)
		memcpy(out->texture[index], outputs[_output_texture0 + index], sizeof(out->texture[0]));
	out->fog = outputs[_output_fog][0];
}
