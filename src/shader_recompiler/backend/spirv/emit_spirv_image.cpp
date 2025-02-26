// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/static_vector.hpp>
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

namespace Shader::Backend::SPIRV {

struct ImageOperands {
    void Add(spv::ImageOperandsMask new_mask, Id value) {
        if (!Sirit::ValidId(value)) {
            return;
        }
        mask = static_cast<spv::ImageOperandsMask>(static_cast<u32>(mask) |
                                                   static_cast<u32>(new_mask));
        operands.push_back(value);
    }
    void Add(spv::ImageOperandsMask new_mask, Id value1, Id value2) {
        mask = static_cast<spv::ImageOperandsMask>(static_cast<u32>(mask) |
                                                   static_cast<u32>(new_mask));
        operands.push_back(value1);
        operands.push_back(value2);
    }

    void AddOffset(EmitContext& ctx, const IR::Value& offset,
                   bool can_use_runtime_offsets = false) {
        if (offset.IsEmpty()) {
            return;
        }
        if (offset.IsImmediate()) {
            const s32 operand = offset.U32();
            Add(spv::ImageOperandsMask::ConstOffset, ctx.ConstS32(operand));
            return;
        }
        IR::Inst* const inst{offset.InstRecursive()};
        if (inst->AreAllArgsImmediates()) {
            switch (inst->GetOpcode()) {
            case IR::Opcode::CompositeConstructU32x2:
                Add(spv::ImageOperandsMask::ConstOffset,
                    ctx.ConstS32(static_cast<s32>(inst->Arg(0).U32()),
                                 static_cast<s32>(inst->Arg(1).U32())));
                return;
            case IR::Opcode::CompositeConstructU32x3:
                Add(spv::ImageOperandsMask::ConstOffset,
                    ctx.ConstS32(static_cast<s32>(inst->Arg(0).U32()),
                                 static_cast<s32>(inst->Arg(1).U32()),
                                 static_cast<s32>(inst->Arg(2).U32())));
                return;
            default:
                break;
            }
        }
        if (can_use_runtime_offsets) {
            Add(spv::ImageOperandsMask::Offset, ctx.Def(offset));
        } else {
            LOG_WARNING(Render_Vulkan,
                        "Runtime offset provided to unsupported image sample instruction");
        }
    }

    void AddDerivatives(EmitContext& ctx, Id derivatives) {
        if (!Sirit::ValidId(derivatives)) {
            return;
        }
        const Id dx{ctx.OpVectorShuffle(ctx.F32[2], derivatives, derivatives, 0, 1)};
        const Id dy{ctx.OpVectorShuffle(ctx.F32[2], derivatives, derivatives, 2, 3)};
        Add(spv::ImageOperandsMask::Grad, dx, dy);
    }

    spv::ImageOperandsMask mask{};
    boost::container::static_vector<Id, 4> operands;
};

Id EmitImageSampleImplicitLod(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id bias,
                              const IR::Value& offset) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.Add(spv::ImageOperandsMask::Bias, bias);
    operands.AddOffset(ctx, offset);
    return ctx.OpImageSampleImplicitLod(ctx.F32[4], sampled_image, coords, operands.mask,
                                        operands.operands);
}

Id EmitImageSampleExplicitLod(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id lod,
                              const IR::Value& offset) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.Add(spv::ImageOperandsMask::Lod, lod);
    operands.AddOffset(ctx, offset);
    return ctx.OpImageSampleExplicitLod(ctx.F32[4], sampled_image, coords, operands.mask,
                                        operands.operands);
}

Id EmitImageSampleDrefImplicitLod(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id dref,
                                  Id bias, const IR::Value& offset) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.Add(spv::ImageOperandsMask::Bias, bias);
    operands.AddOffset(ctx, offset);
    return ctx.OpImageSampleDrefImplicitLod(ctx.F32[1], sampled_image, coords, dref, operands.mask,
                                            operands.operands);
}

Id EmitImageSampleDrefExplicitLod(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id dref,
                                  Id lod, const IR::Value& offset) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.AddOffset(ctx, offset);
    operands.Add(spv::ImageOperandsMask::Lod, lod);
    return ctx.OpImageSampleDrefExplicitLod(ctx.F32[1], sampled_image, coords, dref, operands.mask,
                                            operands.operands);
}

Id EmitImageGather(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords,
                   const IR::Value& offset) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    const u32 comp = inst->Flags<IR::TextureInstInfo>().gather_comp.Value();
    ImageOperands operands;
    operands.AddOffset(ctx, offset, true);
    return ctx.OpImageGather(ctx.F32[4], sampled_image, coords, ctx.ConstU32(comp), operands.mask,
                             operands.operands);
}

Id EmitImageGatherDref(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords,
                       const IR::Value& offset, Id dref) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.AddOffset(ctx, offset, true);
    return ctx.OpImageDrefGather(ctx.F32[4], sampled_image, coords, dref, operands.mask,
                                 operands.operands);
}

Id EmitImageFetch(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, const IR::Value& offset,
                  Id lod, Id ms) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id result_type = texture.data_types->Get(4);
    ImageOperands operands;
    operands.AddOffset(ctx, offset);
    operands.Add(spv::ImageOperandsMask::Lod, lod);
    const Id texel =
        texture.is_storage
            ? ctx.OpImageRead(result_type, image, coords, operands.mask, operands.operands)
            : ctx.OpImageFetch(result_type, image, coords, operands.mask, operands.operands);
    return ctx.OpBitcast(ctx.F32[4], texel);
}

Id EmitImageQueryDimensions(EmitContext& ctx, IR::Inst* inst, u32 handle, Id lod, bool skip_mips) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const auto type = ctx.info.images[handle & 0xFFFF].type;
    const Id zero = ctx.u32_zero_value;
    const auto mips{[&] { return skip_mips ? zero : ctx.OpImageQueryLevels(ctx.U32[1], image); }};
    const bool uses_lod{type != AmdGpu::ImageType::Color2DMsaa};
    const auto query{[&](Id type) {
        return uses_lod ? ctx.OpImageQuerySizeLod(type, image, lod)
                        : ctx.OpImageQuerySize(type, image);
    }};
    switch (type) {
    case AmdGpu::ImageType::Color1D:
        return ctx.OpCompositeConstruct(ctx.U32[4], query(ctx.U32[1]), zero, zero, mips());
    case AmdGpu::ImageType::Color1DArray:
    case AmdGpu::ImageType::Color2D:
    case AmdGpu::ImageType::Cube:
        return ctx.OpCompositeConstruct(ctx.U32[4], query(ctx.U32[2]), zero, mips());
    case AmdGpu::ImageType::Color2DArray:
    case AmdGpu::ImageType::Color3D:
        return ctx.OpCompositeConstruct(ctx.U32[4], query(ctx.U32[3]), mips());
    default:
        UNREACHABLE_MSG("SPIR-V Instruction");
    }
}

Id EmitImageQueryLod(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    const Id zero{ctx.f32_zero_value};
    return ctx.OpImageQueryLod(ctx.F32[2], sampled_image, coords);
}

Id EmitImageGradient(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id derivatives,
                     const IR::Value& offset, Id lod_clamp) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id sampler = ctx.OpLoad(ctx.sampler_type, ctx.samplers[handle >> 16]);
    const Id sampled_image = ctx.OpSampledImage(texture.sampled_type, image, sampler);
    ImageOperands operands;
    operands.AddDerivatives(ctx, derivatives);
    operands.AddOffset(ctx, offset);
    return ctx.OpImageSampleExplicitLod(ctx.F32[4], sampled_image, coords, operands.mask,
                                        operands.operands);
}

Id EmitImageRead(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

void EmitImageWrite(EmitContext& ctx, IR::Inst* inst, u32 handle, Id coords, Id color) {
    const auto& texture = ctx.images[handle & 0xFFFF];
    const Id image = ctx.OpLoad(texture.image_type, texture.id);
    const Id color_type = texture.data_types->Get(4);
    ctx.OpImageWrite(image, coords, ctx.OpBitcast(color_type, color));
}

} // namespace Shader::Backend::SPIRV
