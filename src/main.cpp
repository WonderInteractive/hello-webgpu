#include "webgpu.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <cmath>
#include <thread>
#include <vector>
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif
#define WEBGPU_START doing++; getTypeV_atomic([]() -> void* {
#define WEBGPU_END doing--; return nullptr; });
#define WEBGPU_WAIT while(doing > 0){};
#define WEBGPU_YIELD WEBGPU_WAIT yieldToJS();
void* yieldPtr = (void*)-1;
std::atomic<int> doing{0};
std::atomic<bool> waiting{false};
void* func_and_type2 = 0;
void wait1(volatile void *address, int on) {
	int state = emscripten_atomic_load_u32((void*)address);
	while(state == on)
		state = emscripten_atomic_load_u32((void*)address);
}
void wait2(volatile void *address, int on) {
	int state = emscripten_atomic_load_u32((void*)address);
	while(state != on)
		state = emscripten_atomic_load_u32((void*)address);
}
WGPUDevice device;
WGPUQueue queue;
WGPUSwapChain swapchain;

WGPURenderPipeline pipeline;
WGPUComputePipeline cpipeline;
WGPUBuffer vertBuf; // vertex buffer with triangle position and colours
WGPUBuffer indxBuf; // index buffer
WGPUBuffer uRotBuf; // uniform buffer (containing the rotation angle)
WGPUBuffer pixelsBuf;
WGPUBuffer resolutionBuf;
WGPUBindGroup bindGroup;
WGPUBindGroup cbindGroup;
uint32_t width = 350;
uint32_t height = 150;
std::vector<float> pixels(350*150);
std::vector<float> pixelsOut(350*150);

static uint32_t const triangle_vert_spirv[] = {	0x07230203, 0x00010000, 0x000d0008, 0x00000043, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,	0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000002d, 0x00000031, 0x0000003e,	0x00000040, 0x00050048, 0x00000009, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000009,	0x00000002, 0x00040047, 0x0000000b, 0x00000022, 0x00000000, 0x00040047, 0x0000000b, 0x00000021,	0x00000000, 0x00050048, 0x0000002b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000002b,	0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000002b, 0x00000002, 0x0000000b, 0x00000003,	0x00050048, 0x0000002b, 0x00000003, 0x0000000b, 0x00000004, 0x00030047, 0x0000002b, 0x00000002,	0x00040047, 0x00000031, 0x0000001e, 0x00000000, 0x00040047, 0x0000003e, 0x0000001e, 0x00000000,	0x00040047, 0x00000040, 0x0000001e, 0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003,	0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x0003001e, 0x00000009, 0x00000006, 0x00040020,	0x0000000a, 0x00000002, 0x00000009, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000002, 0x00040015,	0x0000000c, 0x00000020, 0x00000001, 0x0004002b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040020,	0x0000000e, 0x00000002, 0x00000006, 0x00040017, 0x00000018, 0x00000006, 0x00000003, 0x00040018,	0x00000019, 0x00000018, 0x00000003, 0x0004002b, 0x00000006, 0x0000001e, 0x00000000, 0x0004002b,	0x00000006, 0x00000022, 0x3f800000, 0x00040017, 0x00000027, 0x00000006, 0x00000004, 0x00040015,	0x00000028, 0x00000020, 0x00000000, 0x0004002b, 0x00000028, 0x00000029, 0x00000001, 0x0004001c,	0x0000002a, 0x00000006, 0x00000029, 0x0006001e, 0x0000002b, 0x00000027, 0x00000006, 0x0000002a,	0x0000002a, 0x00040020, 0x0000002c, 0x00000003, 0x0000002b, 0x0004003b, 0x0000002c, 0x0000002d,	0x00000003, 0x00040017, 0x0000002f, 0x00000006, 0x00000002, 0x00040020, 0x00000030, 0x00000001,	0x0000002f, 0x0004003b, 0x00000030, 0x00000031, 0x00000001, 0x00040020, 0x0000003b, 0x00000003,	0x00000027, 0x00040020, 0x0000003d, 0x00000003, 0x00000018, 0x0004003b, 0x0000003d, 0x0000003e,	0x00000003, 0x00040020, 0x0000003f, 0x00000001, 0x00000018, 0x0004003b, 0x0000003f, 0x00000040,	0x00000001, 0x0006002c, 0x00000018, 0x00000042, 0x0000001e, 0x0000001e, 0x00000022, 0x00050036,	0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000e,	0x0000000f, 0x0000000b, 0x0000000d, 0x0004003d, 0x00000006, 0x00000010, 0x0000000f, 0x0006000c,	0x00000006, 0x00000011, 0x00000001, 0x0000000b, 0x00000010, 0x0006000c, 0x00000006, 0x00000012,	0x00000001, 0x0000000e, 0x00000011, 0x0006000c, 0x00000006, 0x00000017, 0x00000001, 0x0000000d,	0x00000011, 0x0004007f, 0x00000006, 0x00000020, 0x00000017, 0x00060050, 0x00000018, 0x00000023,	0x00000012, 0x00000017, 0x0000001e, 0x00060050, 0x00000018, 0x00000024, 0x00000020, 0x00000012,	0x0000001e, 0x00060050, 0x00000019, 0x00000026, 0x00000023, 0x00000024, 0x00000042, 0x0004003d,	0x0000002f, 0x00000032, 0x00000031, 0x00050051, 0x00000006, 0x00000033, 0x00000032, 0x00000000,	0x00050051, 0x00000006, 0x00000034, 0x00000032, 0x00000001, 0x00060050, 0x00000018, 0x00000035,	0x00000033, 0x00000034, 0x00000022, 0x00050091, 0x00000018, 0x00000036, 0x00000026, 0x00000035,	0x00050051, 0x00000006, 0x00000037, 0x00000036, 0x00000000, 0x00050051, 0x00000006, 0x00000038,	0x00000036, 0x00000001, 0x00050051, 0x00000006, 0x00000039, 0x00000036, 0x00000002, 0x00070050,	0x00000027, 0x0000003a, 0x00000037, 0x00000038, 0x00000039, 0x00000022, 0x00050041, 0x0000003b,	0x0000003c, 0x0000002d, 0x0000000d, 0x0003003e, 0x0000003c, 0x0000003a, 0x0004003d, 0x00000018,	0x00000041, 0x00000040, 0x0003003e, 0x0000003e, 0x00000041, 0x000100fd, 0x00010038 };

//WGSL equivalent of \c triangle_vert_spirv.
static char const triangle_vert_wgsl[] = R"(
	struct VertexIn {
		@location(0) aPos : vec2<f32>;
		@location(1) aCol : vec3<f32>;
	};
	struct VertexOut {
		@location(0) vCol : vec3<f32>;
		@builtin(position) Position : vec4<f32>;
	};
	struct Rotation {
		@location(0) degs : f32;
	};
	@group(0) @binding(0) var<uniform> uRot : Rotation;
	@stage(vertex)
	fn main(input : VertexIn) -> VertexOut {
		var rads : f32 = radians(uRot.degs);
		var cosA : f32 = cos(rads);
		var sinA : f32 = sin(rads);
		var rot : mat3x3<f32> = mat3x3<f32>(
			vec3<f32>( cosA, sinA, 0.0),
			vec3<f32>(-sinA, cosA, 0.0),
			vec3<f32>( 0.0,  0.0,  1.0));
		var output : VertexOut;
		output.Position = vec4<f32>(rot * vec3<f32>(input.aPos, 1.0), 1.0);
		output.vCol = input.aCol;
		return output;
	}
)";

static uint32_t const triangle_frag_spirv[] = {	0x07230203, 0x00010000, 0x000d0007, 0x00000013, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,	0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,	0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00030010,	0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000c,	0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,	0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,	0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a,	0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,	0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000e, 0x3f800000, 0x00050036, 0x00000002,	0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,	0x0000000c, 0x00050051, 0x00000006, 0x0000000f, 0x0000000d, 0x00000000, 0x00050051, 0x00000006,	0x00000010, 0x0000000d, 0x00000001, 0x00050051, 0x00000006, 0x00000011, 0x0000000d, 0x00000002,	0x00070050, 0x00000007, 0x00000012, 0x0000000f, 0x00000010, 0x00000011, 0x0000000e, 0x0003003e,	0x00000009, 0x00000012, 0x000100fd, 0x00010038 };

//WGSL equivalent of \c triangle_frag_spirv.
static char const triangle_frag_wgsl[] = R"(
	@stage(fragment)
	fn main(@location(0) vCol : vec3<f32>) -> @location(0) vec4<f32> {
		return vec4<f32>(vCol, 1.0);
	}
)";

static char const compute_wgsl[] = R"(
struct Out {
    pixels: array<f32>;
};

struct Resolution {
    width: u32;
    height: u32;
};

@group(0) @binding(0) var<storage, write> result: Out;

@stage(compute) @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) global_id : vec3<u32>) {
    result.pixels[0] = 0.57;
    result.pixels[1] = 0.57;
    result.pixels[5110] = 19.57;
}
)";

/*static*/ WGPUShaderModule createShader(const uint32_t* code, uint32_t size, const char* label = nullptr) {
	WGPUShaderModuleSPIRVDescriptor spirv = {};
	spirv.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
	spirv.codeSize = size / sizeof(uint32_t);
	spirv.code = code;
	WGPUShaderModuleDescriptor desc = {};
	desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&spirv);
	desc.label = label;
	return wgpuDeviceCreateShaderModule(device, &desc);
}

static WGPUShaderModule createShader(const char* const code, const char* label = nullptr) {
	WGPUShaderModuleWGSLDescriptor wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
	wgsl.source = code;
	WGPUShaderModuleDescriptor desc = {};
	desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
	desc.label = label;
	return wgpuDeviceCreateShaderModule(device, &desc);
}

static WGPUBuffer createBuffer(const void* data, size_t size, WGPUBufferUsage usage) {
	WGPUBufferDescriptor desc = {};
	desc.usage = WGPUBufferUsage_CopyDst | usage;
	desc.size  = size;
	WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
	wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
	return buffer;
}

WGPUBuffer src;
WGPUBuffer dst;
float rotDeg = 0.0f;
static void createPipelineAndBuffers() {
	WGPUShaderModule vertMod = createShader(triangle_vert_wgsl);
	WGPUShaderModule fragMod = createShader(triangle_frag_wgsl);
	WGPUShaderModule computeMod = createShader(compute_wgsl);

	WGPUBufferBindingLayout buf = {};
	buf.type = WGPUBufferBindingType_Uniform;

	WGPUBindGroupLayoutEntry bglEntry = {};
	bglEntry.binding = 0;
	bglEntry.visibility = WGPUShaderStage_Vertex;
	bglEntry.buffer = buf;

	WGPUBindGroupLayoutDescriptor bglDesc = {};
	bglDesc.entryCount = 1;
	bglDesc.entries = &bglEntry;
	WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

	WGPUPipelineLayoutDescriptor layoutDesc = {};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = &bindGroupLayout;
	WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

	WGPUVertexAttribute vertAttrs[2] = {};
	vertAttrs[0].format = WGPUVertexFormat_Float32x2;
	vertAttrs[0].offset = 0;
	vertAttrs[0].shaderLocation = 0;
	vertAttrs[1].format = WGPUVertexFormat_Float32x3;
	vertAttrs[1].offset = 2 * sizeof(float);
	vertAttrs[1].shaderLocation = 1;
	WGPUVertexBufferLayout vertexBufferLayout = {};
	vertexBufferLayout.arrayStride = 5 * sizeof(float);
	vertexBufferLayout.attributeCount = 2;
	vertexBufferLayout.attributes = vertAttrs;

	WGPUBlendState blend = {};
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = WGPUBlendFactor_One;
	blend.color.dstFactor = WGPUBlendFactor_One;
	blend.alpha.operation = WGPUBlendOperation_Add;
	blend.alpha.srcFactor = WGPUBlendFactor_One;
	blend.alpha.dstFactor = WGPUBlendFactor_One;

	WGPUColorTargetState colorTarget = {};
	colorTarget.format = webgpu::getSwapChainFormat(device);
	colorTarget.blend = &blend;
	colorTarget.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragMod;
	fragment.entryPoint = "main";
	fragment.targetCount = 1;
	fragment.targets = &colorTarget;

	WGPURenderPipelineDescriptor desc = {};
	desc.fragment = &fragment;

	desc.layout = pipelineLayout;
	desc.depthStencil = nullptr;

	desc.vertex.module = vertMod;
	desc.vertex.entryPoint = "main";
	desc.vertex.bufferCount = 1;//0;
	desc.vertex.buffers = &vertexBufferLayout;

	desc.multisample.count = 1;
	desc.multisample.mask = 0xFFFFFFFF;
	desc.multisample.alphaToCoverageEnabled = false;

	desc.primitive.frontFace = WGPUFrontFace_CCW;
	desc.primitive.cullMode = WGPUCullMode_None;
	desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

	pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);

	wgpuPipelineLayoutRelease(pipelineLayout);
	wgpuShaderModuleRelease(fragMod);
	wgpuShaderModuleRelease(vertMod);

	float const vertData[] = {	-0.8f, -0.8f, 0.0f, 0.0f, 1.0f,	0.8f, -0.8f, 0.0f, 1.0f, 0.0f, -0.0f, 0.8f, 1.0f, 0.0f, 0.0f };
	uint16_t const indxData[] = { 0, 1, 2, 0 };
	uint32_t const data[] = { 350, 150 };
	vertBuf = createBuffer(vertData, sizeof(vertData), WGPUBufferUsage_Vertex);
	indxBuf = createBuffer(indxData, sizeof(indxData), WGPUBufferUsage_Index);
	uRotBuf = createBuffer(&rotDeg, sizeof(rotDeg), WGPUBufferUsage_Uniform);

	{
        WGPUBufferDescriptor descriptor{};
        descriptor.size = pixels.size() * 4;
        descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        pixelsBuf = wgpuDeviceCreateBuffer(device, &descriptor);
    }

	{
        WGPUBufferDescriptor descriptor{};
        descriptor.size = sizeof(data);
        descriptor.usage = WGPUBufferUsage_Uniform;
        resolutionBuf = wgpuDeviceCreateBuffer(device, &descriptor);
    }

	WGPUComputePipelineDescriptor desc2 = {};
	desc2.compute.module = computeMod;
	desc2.compute.entryPoint = "main";

	cpipeline = wgpuDeviceCreateComputePipeline(device, &desc2);

	auto bindGroupLayout2 = wgpuComputePipelineGetBindGroupLayout(cpipeline, 0);

	WGPUBindGroupEntry bgEntry2[2] = {};
	bgEntry2[0].binding = 0;
	bgEntry2[0].buffer = pixelsBuf;
	bgEntry2[0].offset = 0;
	bgEntry2[0].size = pixels.size() * 4;

	bgEntry2[1].binding = 1;
	bgEntry2[1].buffer = resolutionBuf;
	bgEntry2[1].offset = 0;
	bgEntry2[1].size = sizeof(data);

	WGPUBindGroupDescriptor bgDesc2 = {};
	bgDesc2.layout = bindGroupLayout2;
	bgDesc2.entryCount = 1;
	bgDesc2.entries = &bgEntry2[0];

	cbindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc2);

	wgpuBindGroupLayoutRelease(bindGroupLayout2);

    {
        WGPUBufferDescriptor descriptor{};
        descriptor.size = 4;
        descriptor.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
        src = wgpuDeviceCreateBuffer(device, &descriptor);
    }

	{
        WGPUBufferDescriptor descriptor{};
        descriptor.size = 4;
        descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        dst = wgpuDeviceCreateBuffer(device, &descriptor);
    }

	WGPUBindGroupEntry bgEntry = {};
	bgEntry.binding = 0;
	bgEntry.buffer = uRotBuf;
	bgEntry.offset = 0;
	bgEntry.size = sizeof(rotDeg);

	WGPUBindGroupDescriptor bgDesc = {};
	bgDesc.layout = bindGroupLayout;
	bgDesc.entryCount = 1;
	bgDesc.entries = &bgEntry;

	bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

	// last bit of clean-up
	wgpuBindGroupLayoutRelease(bindGroupLayout);
}

void copyBufToBuf(WGPUBuffer buf0, WGPUBuffer buf1, uint32_t size) {
	auto copyEncoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
	wgpuCommandEncoderCopyBufferToBuffer(copyEncoder, 
	buf0 /* source buffer */,
	0 /* source offset */,
	buf1 /* destination buffer */,
	0 /* destination offset */,
	size /* size */
	);

	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(copyEncoder, nullptr);				// create commands
	wgpuCommandEncoderRelease(copyEncoder);														// release encoder
	wgpuQueueSubmit(queue, 1, &commands);
	wgpuCommandBufferRelease(commands);
}

double blue = 0.0;
void getTypeV_atomic(void*(func)()) {
    auto ptr2 = (uintptr_t)func;
    wait2(&func_and_type2, 0);
    __c11_atomic_store((_Atomic uint32_t*)&func_and_type2, (uintptr_t)ptr2, __ATOMIC_RELAXED);
    __builtin_wasm_memory_atomic_notify((int *)&func_and_type2, 1);
}
void yieldToJS() {
	__c11_atomic_store((_Atomic(uint32_t) *)&func_and_type2, (uintptr_t)yieldPtr, __ATOMIC_SEQ_CST);
    __builtin_wasm_memory_atomic_notify((int *)&func_and_type2, 1);
}

WGPUBuffer readBuf;
void cb(WGPUBufferMapAsyncStatus status, void* user_data) {
	EM_ASM(runtimeKeepalivePush(););
	if (status == WGPUBufferMapAsyncStatus_Success) {
		uint8_t const* mapping = (uint8_t*)wgpuBufferGetConstMappedRange(
		readBuf, 0,
		pixels.size() * 4);
		memcpy((void*)pixelsOut.data(), mapping, pixelsOut.size() * 4);
		wgpuBufferUnmap(readBuf);
		EM_ASM(console.log('success', $0, $1), pixelsOut[0], pixelsOut[5110]);
	} else {
		EM_ASM(console.log('error'));
	}
	waiting = false;
}

KEEP_IN_MODULE void dispatch() {
	WEBGPU_START
	auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
	auto computeEncoder = wgpuCommandEncoderBeginComputePass(encoder, nullptr);

	wgpuComputePassEncoderSetPipeline(computeEncoder, cpipeline);
	wgpuComputePassEncoderSetBindGroup(computeEncoder, 0, cbindGroup, 0, 0);

    auto x = std::ceil(width / 8);
    auto y = std::ceil(height / 8);
	wgpuComputePassEncoderDispatch(computeEncoder, x, y, 1);
	wgpuComputePassEncoderEndPass(computeEncoder);
	WGPUBufferDescriptor desc = {};
	desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
	desc.size  = pixels.size() * 4;
	readBuf = wgpuDeviceCreateBuffer(device, &desc);
	wgpuCommandEncoderCopyBufferToBuffer(encoder, 
	pixelsBuf /* source buffer */,
	0 /* source offset */,
	readBuf /* destination buffer */,
	0 /* destination offset */,
	pixels.size() * 4 /* size */
	);

	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);				// create commands
	wgpuCommandEncoderRelease(encoder);														// release encoder

	wgpuQueueSubmit(queue, 1, &commands);
	wgpuCommandBufferRelease(commands);
	//wgpuBufferMapSync(readBuf, WGPUBufferUsage_MapRead, 0, pixels.size() * 4, (void*)pixelsOut.data());
	waiting = true;
	wgpuBufferMapAsync(readBuf, WGPUBufferUsage_MapRead, 0, pixels.size() * 4, cb, (void*)pixels.data());
	WEBGPU_END
	while (waiting) {
		WEBGPU_YIELD
	}
}

KEEP_IN_MODULE bool redraw() {
	WEBGPU_START
	WGPUTextureView backBufView = wgpuSwapChainGetCurrentTextureView(swapchain);			// create textureView

	WGPURenderPassColorAttachment colorDesc = {};
	colorDesc.view    = backBufView;
	colorDesc.loadOp  = WGPULoadOp_Clear;
	colorDesc.storeOp = WGPUStoreOp_Store;
	static bool dir = true;
	if (blue >= 1 || blue < 0) dir = !dir;
	if (dir)
		colorDesc.clearColor.r = blue += 0.01;
	else
		colorDesc.clearColor.r = blue -= 0.01;
	colorDesc.clearColor.g = 0.3f;
	colorDesc.clearColor.b = 0.3f;
	colorDesc.clearColor.a = 1.0f;

	WGPURenderPassDescriptor renderPass = {};
	renderPass.colorAttachmentCount = 1;
	renderPass.colorAttachments = &colorDesc;

	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);			// create encoder
	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPass);	// create pass
	
	wgpuRenderPassEncoderSetPipeline(pass, pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, 0);
	wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertBuf, 0, WGPU_WHOLE_SIZE);
	wgpuRenderPassEncoderSetIndexBuffer(pass, indxBuf, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
	wgpuRenderPassEncoderDrawIndexed(pass, 3, 1, 0, 0, 0);

	wgpuRenderPassEncoderEndPass(pass);
	wgpuRenderPassEncoderRelease(pass);														// release pass
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);				// create commands
	wgpuCommandEncoderRelease(encoder);														// release encoder

	wgpuQueueSubmit(queue, 1, &commands);
	wgpuCommandBufferRelease(commands);														// release commands
#ifndef __EMSCRIPTEN__
	wgpuSwapChainPresent(swapchain); //TODO: wgpuSwapChainPresent is unsupported in Emscripten, so what do we do?
#endif
	wgpuTextureViewRelease(backBufView);													// release textureView
	//EM_ASM(setTimeout(_redraw, 1););
	emscripten_thread_sleep(16);
	WEBGPU_END
	WEBGPU_YIELD
	return true;
}
auto start = emscripten_get_now();
auto end = emscripten_get_now();
KEEP_IN_MODULE void loop00() {
	while(1) {
	 	wait1(&func_and_type2, 0);
		auto ptr0 = __c11_atomic_exchange((_Atomic uintptr_t*)&func_and_type2, 0, __ATOMIC_RELAXED);
		if (ptr0 == (uintptr_t)yieldPtr) {
            EM_ASM(Module.channel.port2.postMessage(""););
            return;
        }
        auto f = (void*(*)())ptr0;
		f();
	}
}
void loop1_start() {
	EM_ASM({
		Module.channel = new MessageChannel();
		Module.channel.port1.onmessage = () => {
			_loop00();
		};
		Module.channel.port2.postMessage("");
	});
}
void mainloop() { }
std::thread t;
extern "C" void* __main__(int /*argc*/, char* /*argv*/[]) {
	if (window::Handle wHnd = window::create()) {
		if ((device = webgpu::create(wHnd))) {
			queue = wgpuDeviceGetQueue(device);
			swapchain = webgpu::createSwapChain(device);
			createPipelineAndBuffers();

			window::show(wHnd);
			t = std::thread([](){
				dispatch();
				while(1) {
					redraw();
				}
			});
			t.detach();
        	loop1_start();
		}
	}
	return 0;
}
EM_BOOL em_redraw(double /*time*/, void *userData) {
	return redraw(); // If this returns true, rAF() will continue, otherwise it will terminate
}
