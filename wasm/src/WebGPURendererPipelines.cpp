/**
 * BespokeSynth WASM - WebGPU Renderer: pipelines and GPU resources
 *
 * Shader modules, render/compute pipelines, the shared bind group, the SDF text
 * atlas texture and the live-analyzer compute pass. Per-frame drawing lives in
 * WebGPURenderer.cpp. WGSL sources are wasm/shaders/*.wgsl, embedded at build
 * time into BespokeWasm/generated/WgslSources.inc (scripts/embed_shaders.cmake).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGPURenderer.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/SdfFont.h"
#include "BespokeWasm/generated/WgslSources.inc"

#include <cstring>

namespace bespoke
{
   namespace wasm
   {

      namespace
      {

         WGPUStringView s(const char* str)
         {
            return WGPUStringView{ str, strlen(str) };
         }

         struct PipelineEntry
         {
            WGPURenderPipeline Pipelines::*slot;
            const char* entryPoint;
         };

         // Every triangle-list pipeline built from render2d.wgsl (vs_main + one fragment entry point).
         constexpr PipelineEntry kPipelineEntries[] = {
            { &Pipelines::solid, "fs_solid" },
            { &Pipelines::knob_highlight, "fs_knob_highlight" },
            { &Pipelines::wire_glow, "fs_wire_glow" },
            { &Pipelines::vu_meter, "fs_vu_meter" },
            { &Pipelines::connection_pulse, "fs_connection_pulse" },
            { &Pipelines::slider_track, "fs_slider_track" },
            { &Pipelines::slider_fill, "fs_slider_fill" },
            { &Pipelines::slider_handle, "fs_slider_handle" },
            { &Pipelines::button, "fs_button" },
            { &Pipelines::button_hover, "fs_button_hover" },
            { &Pipelines::toggle_switch, "fs_toggle_switch" },
            { &Pipelines::toggle_thumb, "fs_toggle_thumb" },
            { &Pipelines::adsr_envelope, "fs_adsr_envelope" },
            { &Pipelines::adsr_grid, "fs_adsr_grid" },
            { &Pipelines::waveform, "fs_waveform" },
            { &Pipelines::waveform_filled, "fs_waveform_filled" },
            { &Pipelines::spectrum_bar, "fs_spectrum_bar" },
            { &Pipelines::spectrum_peak, "fs_spectrum_peak" },
            { &Pipelines::panel_background, "fs_panel_background" },
            { &Pipelines::panel_bordered, "fs_panel_bordered" },
            { &Pipelines::text_glow, "fs_text_glow" },
            { &Pipelines::text_shadow, "fs_text_shadow" },
            { &Pipelines::progress_bar, "fs_progress_bar" },
            { &Pipelines::scope_display, "fs_scope_display" },
            { &Pipelines::scope_grid, "fs_scope_grid" },
            { &Pipelines::led_indicator, "fs_led_indicator" },
            { &Pipelines::led_off, "fs_led_off" },
            { &Pipelines::dial_ticks, "fs_dial_ticks" },
            { &Pipelines::fader_groove, "fs_fader_groove" },
            { &Pipelines::fader_cap, "fs_fader_cap" },
            { &Pipelines::mod_wheel, "fs_mod_wheel" },
            { &Pipelines::pixel_text, "fs_pixel_text" },
            { &Pipelines::sdf_text, "fs_sdf_text" },
            { &Pipelines::live_waveform, "fs_live_waveform" },
            { &Pipelines::live_spectrum, "fs_live_spectrum" },
         };

         WGPUShaderModule createWgslModule(WGPUDevice device, const char* source, const char* label)
         {
            WGPUShaderSourceWGSL wgsl = {};
            wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
            wgsl.code = s(source);

            WGPUShaderModuleDescriptor desc = {};
            desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
            desc.label = s(label);
            return wgpuDeviceCreateShaderModule(device, &desc);
         }

         WGPUBuffer createBuffer(WGPUDevice device, uint64_t size, WGPUBufferUsage usage, const char* label)
         {
            WGPUBufferDescriptor desc = {};
            desc.label = s(label);
            desc.size = size;
            desc.usage = usage;
            return wgpuDeviceCreateBuffer(device, &desc);
         }

      } // namespace

      WebGPURenderer::~WebGPURenderer()
      {
         if (mBindGroup)
            wgpuBindGroupRelease(mBindGroup);
         if (mBindGroupLayout)
            wgpuBindGroupLayoutRelease(mBindGroupLayout);
         if (mUniformBuffer)
            wgpuBufferRelease(mUniformBuffer);
         if (mVertexBuffer)
            wgpuBufferRelease(mVertexBuffer);

         if (mAtlasView)
            wgpuTextureViewRelease(mAtlasView);
         if (mAtlasTexture)
            wgpuTextureRelease(mAtlasTexture);
         if (mAtlasSampler)
            wgpuSamplerRelease(mAtlasSampler);

         if (mAnalyzerBindGroup)
            wgpuBindGroupRelease(mAnalyzerBindGroup);
         if (mAnalyzerPipeline)
            wgpuComputePipelineRelease(mAnalyzerPipeline);
         if (mAnalyzerParamsBuffer)
            wgpuBufferRelease(mAnalyzerParamsBuffer);
         if (mAnalysisSampleBuffer)
            wgpuBufferRelease(mAnalysisSampleBuffer);
         if (mAnalysisSpectrumBuffer)
            wgpuBufferRelease(mAnalysisSpectrumBuffer);

         if (mStrokePipeline)
            wgpuRenderPipelineRelease(mStrokePipeline);
         for (const PipelineEntry& entry : kPipelineEntries)
            if (mPipelines.*entry.slot)
               wgpuRenderPipelineRelease(mPipelines.*entry.slot);
      }

      bool WebGPURenderer::initialize()
      {
         if (!mContext.isInitialized())
            return false;

         createPipelines();
         createTextAtlas();
         createAnalyzer();
         createBuffers();

         mCurrentPipeline = mPipelines.solid;
         return true;
      }

      void WebGPURenderer::createPipelines()
      {
         WGPUDevice device = mContext.getDevice();
         WGPUShaderModule shaderModule = createWgslModule(device, kRender2DWgsl, "render2d.wgsl");

         WGPUVertexAttribute attributes[3] = {};
         attributes[0].format = WGPUVertexFormat_Float32x2; // position
         attributes[0].offset = offsetof(Vertex2D, x);
         attributes[0].shaderLocation = 0;
         attributes[1].format = WGPUVertexFormat_Float32x2; // texcoord
         attributes[1].offset = offsetof(Vertex2D, u);
         attributes[1].shaderLocation = 1;
         attributes[2].format = WGPUVertexFormat_Float32x4; // color
         attributes[2].offset = offsetof(Vertex2D, color);
         attributes[2].shaderLocation = 2;

         WGPUVertexBufferLayout vertexBufferLayout = {};
         vertexBufferLayout.arrayStride = sizeof(Vertex2D);
         vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
         vertexBufferLayout.attributeCount = 3;
         vertexBufferLayout.attributes = attributes;

         // One bind group shared by every render pipeline (see Uniforms in render2d.wgsl):
         // 0 uniforms, 1/2 SDF atlas sampler + texture, 3/4 live analyzer samples + spectrum.
         WGPUBindGroupLayoutEntry entries[5] = {};
         entries[0].binding = 0;
         entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
         entries[0].buffer.type = WGPUBufferBindingType_Uniform;
         entries[0].buffer.minBindingSize = sizeof(FrameUniforms);
         entries[1].binding = 1;
         entries[1].visibility = WGPUShaderStage_Fragment;
         entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
         entries[2].binding = 2;
         entries[2].visibility = WGPUShaderStage_Fragment;
         entries[2].texture.sampleType = WGPUTextureSampleType_Float;
         entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
         entries[3].binding = 3;
         entries[3].visibility = WGPUShaderStage_Fragment;
         entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
         entries[4].binding = 4;
         entries[4].visibility = WGPUShaderStage_Fragment;
         entries[4].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

         WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
         bindGroupLayoutDesc.entryCount = 5;
         bindGroupLayoutDesc.entries = entries;
         mBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

         WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
         pipelineLayoutDesc.bindGroupLayoutCount = 1;
         pipelineLayoutDesc.bindGroupLayouts = &mBindGroupLayout;
         WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

         WGPURenderPipelineDescriptor pipelineDesc = {};
         pipelineDesc.layout = pipelineLayout;
         pipelineDesc.vertex.module = shaderModule;
         pipelineDesc.vertex.entryPoint = s("vs_main");
         pipelineDesc.vertex.bufferCount = 1;
         pipelineDesc.vertex.buffers = &vertexBufferLayout;

         WGPUBlendState blendState = {};
         blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
         blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
         blendState.color.operation = WGPUBlendOperation_Add;
         blendState.alpha.srcFactor = WGPUBlendFactor_One;
         blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
         blendState.alpha.operation = WGPUBlendOperation_Add;

         WGPUColorTargetState colorTarget = {};
         colorTarget.format = mContext.getSwapChainFormat();
         colorTarget.writeMask = WGPUColorWriteMask_All;
         colorTarget.blend = &blendState;

         WGPUFragmentState fragmentState = {};
         fragmentState.module = shaderModule;
         fragmentState.targetCount = 1;
         fragmentState.targets = &colorTarget;
         pipelineDesc.fragment = &fragmentState;

         pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
         pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
         pipelineDesc.primitive.cullMode = WGPUCullMode_None;
         pipelineDesc.multisample.count = 1;
         pipelineDesc.multisample.mask = ~0u;

         for (const PipelineEntry& entry : kPipelineEntries)
         {
            fragmentState.entryPoint = s(entry.entryPoint);
            mPipelines.*entry.slot = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
         }

         // Stroke pipeline (lines) - solid color shader on a line list.
         pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
         fragmentState.entryPoint = s("fs_solid");
         mStrokePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

         wgpuShaderModuleRelease(shaderModule);
         wgpuPipelineLayoutRelease(pipelineLayout);
      }

      void WebGPURenderer::createTextAtlas()
      {
         WGPUDevice device = mContext.getDevice();
         const SdfFont& font = uiSdfFont();

         // Without an atlas the binding still needs a texture; a 1x1 placeholder keeps the
         // bind group valid and text() stays on the pixel font (useSdfTextForFace()).
         const uint8_t placeholder = 0;
         const bool hasAtlas = font.isLoaded();
         const uint32_t width = hasAtlas ? static_cast<uint32_t>(font.atlasWidth()) : 1u;
         const uint32_t height = hasAtlas ? static_cast<uint32_t>(font.atlasHeight()) : 1u;

         WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
         textureDesc.label = s("ui-sdf-atlas");
         textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
         textureDesc.dimension = WGPUTextureDimension_2D;
         textureDesc.size = { width, height, 1 };
         textureDesc.format = WGPUTextureFormat_R8Unorm;
         mAtlasTexture = wgpuDeviceCreateTexture(device, &textureDesc);

         WGPUTexelCopyTextureInfo destination = {};
         destination.texture = mAtlasTexture;
         destination.aspect = WGPUTextureAspect_All;
         WGPUTexelCopyBufferLayout layout = {};
         layout.bytesPerRow = width;
         layout.rowsPerImage = height;
         const WGPUExtent3D size = { width, height, 1 };
         wgpuQueueWriteTexture(mContext.getQueue(), &destination, hasAtlas ? font.atlasPixels() : &placeholder,
                               static_cast<size_t>(width) * height, &layout, &size);

         mAtlasView = wgpuTextureCreateView(mAtlasTexture, nullptr);

         WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
         samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
         samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
         samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
         samplerDesc.magFilter = WGPUFilterMode_Linear;
         samplerDesc.minFilter = WGPUFilterMode_Linear;
         samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
         mAtlasSampler = wgpuDeviceCreateSampler(device, &samplerDesc);

         mUniforms.atlasSize[0] = static_cast<float>(width);
         mUniforms.atlasSize[1] = static_cast<float>(height);
         mUniforms.sdfPxRange = hasAtlas ? font.pxRange() : 1.0f;
      }

      void WebGPURenderer::createAnalyzer()
      {
         WGPUDevice device = mContext.getDevice();

         mAnalyzerUpload.assign(static_cast<size_t>(AudioAnalysis::kFftSize), 0.0f);
         mAnalysisSampleBuffer = createBuffer(device, sizeof(float) * AudioAnalysis::kFftSize,
                                              WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "analyzer-samples");
         mAnalysisSpectrumBuffer = createBuffer(device, sizeof(float) * AudioAnalysis::kSpectrumBins,
                                                WGPUBufferUsage_Storage, "analyzer-spectrum");
         mAnalyzerParamsBuffer =
         createBuffer(device, sizeof(uint32_t) * 4, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "analyzer-params");
         const uint32_t params[4] = { static_cast<uint32_t>(AudioAnalysis::kSpectrumBins), 0u, 0u, 0u };
         wgpuQueueWriteBuffer(mContext.getQueue(), mAnalyzerParamsBuffer, 0, params, sizeof(params));
         wgpuQueueWriteBuffer(mContext.getQueue(), mAnalysisSampleBuffer, 0, mAnalyzerUpload.data(),
                              mAnalyzerUpload.size() * sizeof(float));
         mUniforms.analyzerBins = static_cast<float>(AudioAnalysis::kSpectrumBins);

         WGPUBindGroupLayoutEntry entries[3] = {};
         entries[0].binding = 0;
         entries[0].visibility = WGPUShaderStage_Compute;
         entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
         entries[1].binding = 1;
         entries[1].visibility = WGPUShaderStage_Compute;
         entries[1].buffer.type = WGPUBufferBindingType_Storage;
         entries[2].binding = 2;
         entries[2].visibility = WGPUShaderStage_Compute;
         entries[2].buffer.type = WGPUBufferBindingType_Uniform;
         WGPUBindGroupLayoutDescriptor layoutDesc = {};
         layoutDesc.entryCount = 3;
         layoutDesc.entries = entries;
         WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

         WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
         pipelineLayoutDesc.bindGroupLayoutCount = 1;
         pipelineLayoutDesc.bindGroupLayouts = &layout;
         WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

         WGPUShaderModule module = createWgslModule(device, kAnalyzerFftWgsl, "analyzer_fft.wgsl");
         WGPUComputePipelineDescriptor pipelineDesc = {};
         pipelineDesc.label = s("analyzer-fft");
         pipelineDesc.layout = pipelineLayout;
         pipelineDesc.compute.module = module;
         pipelineDesc.compute.entryPoint = s("cs_spectrum");
         mAnalyzerPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

         WGPUBindGroupEntry groupEntries[3] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                                WGPU_BIND_GROUP_ENTRY_INIT };
         groupEntries[0].binding = 0;
         groupEntries[0].buffer = mAnalysisSampleBuffer;
         groupEntries[1].binding = 1;
         groupEntries[1].buffer = mAnalysisSpectrumBuffer;
         groupEntries[2].binding = 2;
         groupEntries[2].buffer = mAnalyzerParamsBuffer;
         WGPUBindGroupDescriptor groupDesc = {};
         groupDesc.layout = layout;
         groupDesc.entryCount = 3;
         groupDesc.entries = groupEntries;
         mAnalyzerBindGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);

         wgpuShaderModuleRelease(module);
         wgpuPipelineLayoutRelease(pipelineLayout);
         wgpuBindGroupLayoutRelease(layout);
      }

      void WebGPURenderer::createBuffers()
      {
         WGPUDevice device = mContext.getDevice();

         mUniformBuffer = createBuffer(device, sizeof(FrameUniforms), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                       "frame-uniforms");
         mVertexBuffer = createBuffer(device, sizeof(Vertex2D) * 65536, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                      "vertices");

         WGPUBindGroupEntry entries[5] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                           WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
         entries[0].binding = 0;
         entries[0].buffer = mUniformBuffer;
         entries[1].binding = 1;
         entries[1].sampler = mAtlasSampler;
         entries[2].binding = 2;
         entries[2].textureView = mAtlasView;
         entries[3].binding = 3;
         entries[3].buffer = mAnalysisSampleBuffer;
         entries[4].binding = 4;
         entries[4].buffer = mAnalysisSpectrumBuffer;

         WGPUBindGroupDescriptor bindGroupDesc = {};
         bindGroupDesc.layout = mBindGroupLayout;
         bindGroupDesc.entryCount = 5;
         bindGroupDesc.entries = entries;
         mBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
      }

      void WebGPURenderer::dispatchAnalyzer()
      {
         if (!mAnalyzerPipeline || !mAnalyzerBindGroup)
            return;

         // Only the newest FFT window crosses to the GPU (2 KB); the transform itself and
         // the scope/spectrum rasterization never touch the CPU.
         AudioAnalysis::copyLatest(mAnalyzerUpload.data(), static_cast<int>(mAnalyzerUpload.size()));
         WGPUQueue queue = mContext.getQueue();
         wgpuQueueWriteBuffer(queue, mAnalysisSampleBuffer, 0, mAnalyzerUpload.data(),
                              mAnalyzerUpload.size() * sizeof(float));

         // Submitted ahead of the frame's render command buffer, so fs_live_spectrum
         // reads this frame's result.
         WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(mContext.getDevice(), nullptr);
         WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
         wgpuComputePassEncoderSetPipeline(pass, mAnalyzerPipeline);
         wgpuComputePassEncoderSetBindGroup(pass, 0, mAnalyzerBindGroup, 0, nullptr);
         wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
         wgpuComputePassEncoderEnd(pass);
         wgpuComputePassEncoderRelease(pass);
         WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
         wgpuQueueSubmit(queue, 1, &commands);
         wgpuCommandBufferRelease(commands);
         wgpuCommandEncoderRelease(encoder);

         AudioAnalysis::noteGpuSpectrumDispatch();
      }

   } // namespace wasm
} // namespace bespoke
