#include "webgpu.h"

#include <string.h>
#include <emscripten.h>
#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <future>
#include <vector>
#include "pthreadfs.h"
#include "concurrentqueue/concurrentqueue.h"
std::atomic_flag condAtomicFlag1{false};
std::atomic_flag condAtomicFlag2{false};
std::mutex mtx;
std::atomic<bool> doYield = {false};
std::atomic<bool> done = {false};
std::atomic<bool> waiting = {false};
enum Type {
	Sync, Async, AsyncToSync, Yield
};
struct Event {
	Type isType = Sync;
	std::function<void*()> func;
	void* out;
};
Event evt2;

//moodycamel::ConcurrentQueue<Event> eventQueue1(1024 * 1024);
//moodycamel::ConcurrentQueue<uint32_t> eventQueue2(1024 * 1024);
//atomic_queue::AtomicQueue<uint32_t, 1024 * 1024, 0> eventQueue3;
//moodycamel::ConcurrentQueue<std::pair<Event, Descriptor>> eventQueue1(1024);
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

/**
 * Current rotation angle (in degrees, updated per frame).
 */
float rotDeg = 0.0f;

/**
 * Vertex shader SPIR-V.
 * \code
 *	// glslc -Os -mfmt=num -o - -c in.vert
 *	#version 450
 *	layout(set = 0, binding = 0) uniform Rotation {
 *		float uRot;
 *	};
 *	layout(location = 0) in  vec2 aPos;
 *	layout(location = 1) in  vec3 aCol;
 *	layout(location = 0) out vec3 vCol;
 *	void main() {
 *		float cosA = cos(radians(uRot));
 *		float sinA = sin(radians(uRot));
 *		mat3 rot = mat3(cosA, sinA, 0.0,
 *					   -sinA, cosA, 0.0,
 *						0.0,  0.0,  1.0);
 *		gl_Position = vec4(rot * vec3(aPos, 1.0), 1.0);
 *		vCol = aCol;
 *	}
 * \endcode
 */
static uint32_t const triangle_vert_spirv[] = {
	0x07230203, 0x00010000, 0x000d0008, 0x00000043, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
	0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
	0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000002d, 0x00000031, 0x0000003e,
	0x00000040, 0x00050048, 0x00000009, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000009,
	0x00000002, 0x00040047, 0x0000000b, 0x00000022, 0x00000000, 0x00040047, 0x0000000b, 0x00000021,
	0x00000000, 0x00050048, 0x0000002b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000002b,
	0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000002b, 0x00000002, 0x0000000b, 0x00000003,
	0x00050048, 0x0000002b, 0x00000003, 0x0000000b, 0x00000004, 0x00030047, 0x0000002b, 0x00000002,
	0x00040047, 0x00000031, 0x0000001e, 0x00000000, 0x00040047, 0x0000003e, 0x0000001e, 0x00000000,
	0x00040047, 0x00000040, 0x0000001e, 0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
	0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x0003001e, 0x00000009, 0x00000006, 0x00040020,
	0x0000000a, 0x00000002, 0x00000009, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000002, 0x00040015,
	0x0000000c, 0x00000020, 0x00000001, 0x0004002b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040020,
	0x0000000e, 0x00000002, 0x00000006, 0x00040017, 0x00000018, 0x00000006, 0x00000003, 0x00040018,
	0x00000019, 0x00000018, 0x00000003, 0x0004002b, 0x00000006, 0x0000001e, 0x00000000, 0x0004002b,
	0x00000006, 0x00000022, 0x3f800000, 0x00040017, 0x00000027, 0x00000006, 0x00000004, 0x00040015,
	0x00000028, 0x00000020, 0x00000000, 0x0004002b, 0x00000028, 0x00000029, 0x00000001, 0x0004001c,
	0x0000002a, 0x00000006, 0x00000029, 0x0006001e, 0x0000002b, 0x00000027, 0x00000006, 0x0000002a,
	0x0000002a, 0x00040020, 0x0000002c, 0x00000003, 0x0000002b, 0x0004003b, 0x0000002c, 0x0000002d,
	0x00000003, 0x00040017, 0x0000002f, 0x00000006, 0x00000002, 0x00040020, 0x00000030, 0x00000001,
	0x0000002f, 0x0004003b, 0x00000030, 0x00000031, 0x00000001, 0x00040020, 0x0000003b, 0x00000003,
	0x00000027, 0x00040020, 0x0000003d, 0x00000003, 0x00000018, 0x0004003b, 0x0000003d, 0x0000003e,
	0x00000003, 0x00040020, 0x0000003f, 0x00000001, 0x00000018, 0x0004003b, 0x0000003f, 0x00000040,
	0x00000001, 0x0006002c, 0x00000018, 0x00000042, 0x0000001e, 0x0000001e, 0x00000022, 0x00050036,
	0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000e,
	0x0000000f, 0x0000000b, 0x0000000d, 0x0004003d, 0x00000006, 0x00000010, 0x0000000f, 0x0006000c,
	0x00000006, 0x00000011, 0x00000001, 0x0000000b, 0x00000010, 0x0006000c, 0x00000006, 0x00000012,
	0x00000001, 0x0000000e, 0x00000011, 0x0006000c, 0x00000006, 0x00000017, 0x00000001, 0x0000000d,
	0x00000011, 0x0004007f, 0x00000006, 0x00000020, 0x00000017, 0x00060050, 0x00000018, 0x00000023,
	0x00000012, 0x00000017, 0x0000001e, 0x00060050, 0x00000018, 0x00000024, 0x00000020, 0x00000012,
	0x0000001e, 0x00060050, 0x00000019, 0x00000026, 0x00000023, 0x00000024, 0x00000042, 0x0004003d,
	0x0000002f, 0x00000032, 0x00000031, 0x00050051, 0x00000006, 0x00000033, 0x00000032, 0x00000000,
	0x00050051, 0x00000006, 0x00000034, 0x00000032, 0x00000001, 0x00060050, 0x00000018, 0x00000035,
	0x00000033, 0x00000034, 0x00000022, 0x00050091, 0x00000018, 0x00000036, 0x00000026, 0x00000035,
	0x00050051, 0x00000006, 0x00000037, 0x00000036, 0x00000000, 0x00050051, 0x00000006, 0x00000038,
	0x00000036, 0x00000001, 0x00050051, 0x00000006, 0x00000039, 0x00000036, 0x00000002, 0x00070050,
	0x00000027, 0x0000003a, 0x00000037, 0x00000038, 0x00000039, 0x00000022, 0x00050041, 0x0000003b,
	0x0000003c, 0x0000002d, 0x0000000d, 0x0003003e, 0x0000003c, 0x0000003a, 0x0004003d, 0x00000018,
	0x00000041, 0x00000040, 0x0003003e, 0x0000003e, 0x00000041, 0x000100fd, 0x00010038
};

/**
 * WGSL equivalent of \c triangle_vert_spirv.
 */
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

/**
 * Fragment shader SPIR-V.
 * \code
 *	// glslc -Os -mfmt=num -o - -c in.frag
 *	#version 450
 *	layout(location = 0) in  vec3 vCol;
 *	layout(location = 0) out vec4 fragColor;
 *	void main() {
 *		fragColor = vec4(vCol, 1.0);
 *	}
 * \endcode
 */
static uint32_t const triangle_frag_spirv[] = {
	0x07230203, 0x00010000, 0x000d0007, 0x00000013, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
	0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
	0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00030010,
	0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000c,
	0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
	0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
	0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a,
	0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
	0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000e, 0x3f800000, 0x00050036, 0x00000002,
	0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
	0x0000000c, 0x00050051, 0x00000006, 0x0000000f, 0x0000000d, 0x00000000, 0x00050051, 0x00000006,
	0x00000010, 0x0000000d, 0x00000001, 0x00050051, 0x00000006, 0x00000011, 0x0000000d, 0x00000002,
	0x00070050, 0x00000007, 0x00000012, 0x0000000f, 0x00000010, 0x00000011, 0x0000000e, 0x0003003e,
	0x00000009, 0x00000012, 0x000100fd, 0x00010038
};

/**
 * WGSL equivalent of \c triangle_frag_spirv.
 */
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

/**
 * Helper to create a shader from SPIR-V IR.
 *
 * \param[in] code shader source (output using the \c -V \c -x options in \c glslangValidator)
 * \param[in] size size of \a code in bytes
 * \param[in] label optional shader name
 */
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

/**
 * Helper to create a shader from WGSL source.
 *
 * \param[in] code WGSL shader source
 * \param[in] label optional shader name
 */
static WGPUShaderModule createShader(const char* const code, const char* label = nullptr) {
	WGPUShaderModuleWGSLDescriptor wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
	wgsl.source = code;
	WGPUShaderModuleDescriptor desc = {};
	desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
	desc.label = label;
	return wgpuDeviceCreateShaderModule(device, &desc);
}

/**
 * Helper to create a buffer.
 *
 * \param[in] data pointer to the start of the raw data
 * \param[in] size number of bytes in \a data
 * \param[in] usage type of buffer
 */
static WGPUBuffer createBuffer(const void* data, size_t size, WGPUBufferUsage usage) {
	WGPUBufferDescriptor desc = {};
	desc.usage = WGPUBufferUsage_CopyDst | usage;
	desc.size  = size;
	WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
	wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
	return buffer;
}
static WGPUBuffer createBuffer2(const void* data, size_t size, WGPUBufferUsage usage) {
	WGPUBufferDescriptor desc = {};
	desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite;
	desc.size  = size;
	WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
	//wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
	return buffer;
}

WGPUBuffer src;
WGPUBuffer dst;

/**
 * Bare minimum pipeline to draw a triangle using the above shaders.
 */
static void createPipelineAndBuffers() {
	// compile shaders
	// NOTE: these are now the WGSL shaders (tested with Dawn and Chrome Canary)
	WGPUShaderModule vertMod = createShader(triangle_vert_wgsl);
	WGPUShaderModule fragMod = createShader(triangle_frag_wgsl);
	WGPUShaderModule computeMod = createShader(compute_wgsl);

	// keep the old unused SPIR-V shaders around for a while...
	(void) triangle_vert_spirv;
	(void) triangle_frag_spirv;

	WGPUBufferBindingLayout buf = {};
	buf.type = WGPUBufferBindingType_Uniform;

	// bind group layout (used by both the pipeline layout and uniform bind group, released at the end of this function)
	WGPUBindGroupLayoutEntry bglEntry = {};
	bglEntry.binding = 0;
	bglEntry.visibility = WGPUShaderStage_Vertex;
	bglEntry.buffer = buf;

	WGPUBindGroupLayoutDescriptor bglDesc = {};
	bglDesc.entryCount = 1;
	bglDesc.entries = &bglEntry;
	WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

	// pipeline layout (used by the render pipeline, released after its creation)
	WGPUPipelineLayoutDescriptor layoutDesc = {};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = &bindGroupLayout;
	WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

	// describe buffer layouts
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

	// Fragment state
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

	// Other state
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

	// partial clean-up (just move to the end, no?)
	wgpuPipelineLayoutRelease(pipelineLayout);

	wgpuShaderModuleRelease(fragMod);
	wgpuShaderModuleRelease(vertMod);

	// create the buffers (x, y, r, g, b)
	float const vertData[] = {
		-0.8f, -0.8f, 0.0f, 0.0f, 1.0f, // BL
		 0.8f, -0.8f, 0.0f, 1.0f, 0.0f, // BR
		-0.0f,  0.8f, 1.0f, 0.0f, 0.0f, // top
	};
	uint16_t const indxData[] = {
		0, 1, 2,
		0 // padding (better way of doing this?)
	};
	uint32_t const data[] = {
		350, 150
	};
	vertBuf = createBuffer(vertData, sizeof(vertData), WGPUBufferUsage_Vertex);
	indxBuf = createBuffer(indxData, sizeof(indxData), WGPUBufferUsage_Index);

	// create the uniform bind group (note 'rotDeg' is copied here, not bound in any way)
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

	// last bit of clean-up
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
#define EM_PTHREADFS_1(func, arg)                                                                     \
  g_sync_to_async_helper.invoke([&arg](emscripten::sync_to_async::Callback resume) {                   \
    g_resumeFct = [resume]() { (*resume)(); };  \
	func(arg); \
	resumeWrapper_v();                                               \
});
void test(int arg) {

}
void writeTest() {
#if 0
	//method 1 - queueWriteBuffer
	//wgpuQueueWriteBuffer(queue, uRotBuf, 0, &rotDeg, sizeof(rotDeg));
#if 0
	wgpuBufferMapAsync(src, WGPUMapMode::WGPUMapMode_Write, 0, 4, 
	[](WGPUBufferMapAsyncStatus status, void* vp_userdata){
		auto ptr = (uint8_t*)vp_userdata;
		ptr[0] = 1;ptr[1] = 2;ptr[2] = 3;
		wgpuBufferUnmap(src);
		UnWait();
	}, reinterpret_cast<void * >(&rotDeg));
#endif
	auto ptr = &src;
	auto src2 = (uint32_t)src;

	//method 2 - EM_PTHREADFS
	EM_PTHREADFS_ASM_1({
		var bufferWrapper = WebGPU.mgrBuffer.objects[$1];
		bufferWrapper.mapMode = GPUMapMode.WRITE;
    	bufferWrapper.onUnmap = [];
		var buffer = bufferWrapper.object;
		console.log($1, bufferWrapper, buffer);
		await buffer.mapAsync(GPUMapMode.WRITE);
		buffer.unmap();
		//await myMapReadBuffer.mapAsync(GPUMapMode.WRITE);
		//const data = myMapReadBuffer.getMappedRange();
		// Do something with the data
		//myMapReadBuffer.unmap();
	}, src2);

#endif
}
void readTest() {
#if 0
	//method 1 - EM_PTHREADFS
	EM_PTHREADFS_ASM1({
		var bufferWrapper = WebGPU.mgrBuffer.objects[$1];
		bufferWrapper.mapMode = GPUMapMode.READ;
    	bufferWrapper.onUnmap = [];
		var buffer = bufferWrapper.object;
		await buffer.mapAsync(GPUMapMode.READ);
		buffer.unmap();
	}, src);
#endif
}
void fenceTest() {

}
void copyBufToBuf(WGPUBuffer buf0, WGPUBuffer buf1, uint32_t size);
void allTests() {
	writeTest(); //increment the 4 uints
	copyBufToBuf(src, dst, 4); //copy the buffer from the gpu read buffer to the cpu write buffer
	readTest(); //read the cpu write buffer
	fenceTest();
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
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif
double blue = 0.0;
std::atomic<Event*>  evt = {nullptr};
//#define PUSH(func, ...) func(__VA_ARGS__);
//#define PUSH(func, ...) {evt = new Event{Sync, [](){func(__VA_ARGS__);}}}
//promise queue
//#define PUSH(func, ...) {eventQueue1.enqueue(Event{Sync, [](){func(__VA_ARGS__);}});}
void cb(WGPUBufferMapAsyncStatus status, void* user_data);
void wgpuBufferMapSync(WGPUBuffer buffer, WGPUMapModeFlags mode, size_t offset, size_t size, void* out) {
	mtx.lock();
	evt2 = Event{AsyncToSync, [&]() -> void* {
		wgpuBufferMapAsync(buffer, mode, offset, size, cb, nullptr);
		return nullptr;
	}};
	mtx.unlock();
	condAtomicFlag1.test_and_set();
	condAtomicFlag1.notify_one();
	condAtomicFlag2.wait(false);
	condAtomicFlag2.clear();
	condAtomicFlag1.clear();
	memcpy(out, evt2.out, size);
}
template <typename T>
T getType(std::function<void*()> func) {
	mtx.lock();
	evt2 = Event{Sync, func};
	mtx.unlock();
	condAtomicFlag1.test_and_set();
	condAtomicFlag1.notify_one();
	condAtomicFlag2.wait(false);
	condAtomicFlag2.clear();
	condAtomicFlag1.clear();
	auto val = *(T*)evt2.out;
	//free(evt2.out);
	return val;
}
void getTypeV(std::function<void*()> func) {
	mtx.lock();
	evt2 = Event{Sync, func};
	mtx.unlock();
	condAtomicFlag1.test_and_set();
	condAtomicFlag1.notify_one();
	condAtomicFlag2.wait(false);
	condAtomicFlag2.clear();
	condAtomicFlag1.clear();
}
void yieldToJS() {
	mtx.lock();
	evt2 = Event{Yield};
	mtx.unlock();
	condAtomicFlag1.test_and_set();
	condAtomicFlag1.notify_one();
	condAtomicFlag2.wait(false);
	condAtomicFlag2.clear();
	condAtomicFlag1.clear();
}
void* reserve = malloc(1024 * 1024 * 1);
#define PUSH(func, ...) getTypeV([&]() -> void* {\
		func(__VA_ARGS__);\
		return nullptr;\
	});
#define PUSH2(T, func, ...) getType<T>([&]() -> void* {\
		T in = func(__VA_ARGS__);\
		memcpy(reserve, &in, sizeof(T));\
		return reserve;\
	});
#define wgpuDeviceCreateCommandEncoder(...) PUSH2(WGPUCommandEncoder, wgpuDeviceCreateCommandEncoder, __VA_ARGS__)
#define wgpuSwapChainGetCurrentTextureView(...) PUSH2(WGPUTextureView, wgpuSwapChainGetCurrentTextureView, __VA_ARGS__)
#define wgpuCommandEncoderBeginRenderPass(...) PUSH2(WGPURenderPassEncoder, wgpuCommandEncoderBeginRenderPass, __VA_ARGS__)
#define wgpuRenderPassEncoderSetPipeline(...) PUSH(wgpuRenderPassEncoderSetPipeline, __VA_ARGS__)
#define wgpuRenderPassEncoderSetBindGroup(...) PUSH(wgpuRenderPassEncoderSetBindGroup, __VA_ARGS__)
#define wgpuRenderPassEncoderSetVertexBuffer(...) PUSH(wgpuRenderPassEncoderSetVertexBuffer, __VA_ARGS__)
#define wgpuRenderPassEncoderSetIndexBuffer(...) PUSH(wgpuRenderPassEncoderSetIndexBuffer, __VA_ARGS__)
#define wgpuCommandEncoderFinish(...) PUSH2(WGPUCommandBuffer, wgpuCommandEncoderFinish, __VA_ARGS__)
#define wgpuRenderPassEncoderDrawIndexed(...) PUSH(wgpuRenderPassEncoderDrawIndexed, __VA_ARGS__)
#define wgpuRenderPassEncoderEndPass(...) PUSH(wgpuRenderPassEncoderEndPass, __VA_ARGS__)
#define wgpuRenderPassEncoderRelease(...) PUSH(wgpuRenderPassEncoderRelease, __VA_ARGS__)
#define wgpuCommandEncoderRelease(...) PUSH(wgpuCommandEncoderRelease, __VA_ARGS__)
#define wgpuQueueSubmit(...) PUSH(wgpuQueueSubmit, __VA_ARGS__)
#define wgpuCommandBufferRelease(...) PUSH(wgpuCommandBufferRelease, __VA_ARGS__)
#define wgpuTextureViewRelease(...) PUSH(wgpuTextureViewRelease, __VA_ARGS__)
//#define wgpuBufferUnmap(...) PUSH(wgpuBufferUnmap, __VA_ARGS__)
#define wgpuCommandEncoderBeginComputePass(...) PUSH2(WGPUComputePassEncoder, wgpuCommandEncoderBeginComputePass, __VA_ARGS__)
//#define wgpuBufferGetConstMappedRange(...) PUSH2(const void*, wgpuBufferGetConstMappedRange, __VA_ARGS__)
#define wgpuDeviceCreateBuffer(...) PUSH2(WGPUBuffer, wgpuDeviceCreateBuffer, __VA_ARGS__)
#define wgpuComputePassEncoderSetPipeline(...) PUSH(wgpuComputePassEncoderSetPipeline, __VA_ARGS__)
#define wgpuComputePassEncoderSetBindGroup(...) PUSH(wgpuComputePassEncoderSetBindGroup, __VA_ARGS__)
#define wgpuComputePassEncoderDispatch(...) PUSH(wgpuComputePassEncoderDispatch, __VA_ARGS__)
#define wgpuComputePassEncoderEndPass(...) PUSH(wgpuComputePassEncoderEndPass, __VA_ARGS__)
#define wgpuCommandEncoderCopyBufferToBuffer(...) PUSH(wgpuCommandEncoderCopyBufferToBuffer, __VA_ARGS__)
#define wgpuBufferMapAsync(...) PUSH(wgpuBufferMapAsync, __VA_ARGS__)

WGPUBuffer readBuf;
void cb(WGPUBufferMapAsyncStatus status, void* user_data) {
	EM_ASM(runtimeKeepalivePush(););
	EM_ASM(console.log('callback'));
	if (status == WGPUBufferMapAsyncStatus_Success) {
		uint8_t const* mapping = (uint8_t*)wgpuBufferGetConstMappedRange(
		readBuf, 0,
		pixels.size() * 4);
		memcpy((void*)pixelsOut.data(), mapping, pixelsOut.size() * 4);
		wgpuBufferUnmap(readBuf);
		EM_ASM(console.log('success', $0, $1), pixelsOut[0], pixelsOut[5110]);
		EM_ASM(console.log('success', $0, $1), mapping[0], mapping[5110]);
	} else {
		EM_ASM(console.log('error'));
	}
	waiting = false;
	//condAtomicFlag2.test_and_set();
	//condAtomicFlag2.notify_all();
}

KEEP_IN_MODULE void dispatch() {
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
	wgpuBufferMapAsync(readBuf, WGPUBufferUsage_MapRead, 0, pixels.size() * 4, cb, (void*)pixels.data());
	waiting = true;
	while (waiting) {
		yieldToJS();
	}
	//await readBuffer.mapAsync(GPUMapMode.READ, 0, pixelsBufferSize);
	//memcpy(pixelsOut.data(), readBuffer.getMappedRange(), pixelsOut.size() * 4);
}

KEEP_IN_MODULE bool redraw() {
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

	// update the rotation
	rotDeg += 0.1f;

	//do fence
	//setTimeout(..., 0);

	//map buffer and in compute shader set to +1 +2 +3

	//map buffer and set value in cpp to be read in shader (set triangle corner colors)

	//writeTest();
	//readTest();
	//fenceTest();

	
	// draw the triangle (comment these five lines to simply clear the screen)
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
	/*
	 * TODO: wgpuSwapChainPresent is unsupported in Emscripten, so what do we do?
	 */
	wgpuSwapChainPresent(swapchain);
#endif
	wgpuTextureViewRelease(backBufView);													// release textureView
				//EM_ASM(setTimeout(_redraw, 1););
	yieldToJS();
	emscripten_thread_sleep(16);
	return true;
}
uint32_t counter = 0;
uint32_t counter1 = 0;
uint32_t counter2 = 0;
auto start = emscripten_get_now();
auto end = emscripten_get_now();
EM_BOOL tick(double time, void *userData) {
	if (emscripten_get_now() - start > 5000) {
		EM_ASM(console.log('count:', $0);, counter);
		return false;
	}
	counter++;
	//redraw();
	return true;
}
void loop2(void *userData) {
	counter++;
	if (emscripten_get_now() - start > 5000) {
		EM_ASM(console.log('count:', $0);, counter);
	} else
	emscripten_set_timeout(loop2, 0, (void*)2);

	for (;;) {
		//dequeue
		//if item is an async item then we must break the 
	}
}
int counter3 = 0;
KEEP_IN_MODULE void loop0() {
	while(1) {
		//evt.wait();

	}
}
KEEP_IN_MODULE void loop1();
KEEP_IN_MODULE void loop11() {
	if (done)
		loop1();
	else {
		EM_ASM(Module.channel.port2.postMessage(""););
	}
}
KEEP_IN_MODULE void loop00() {
	#if 0
	if (waiting) {
		EM_ASM(Module.channel.port2.postMessage(""););
		return;
	}
	#endif
	//EM_ASM(console.log('loop00'));
	while(1) { //possibly don't need this loop if MessageChannel is fast enough
		condAtomicFlag1.wait(false);
        condAtomicFlag1.clear();
		//EM_ASM(console.log('start sync1'));
		mtx.lock();
		if (evt2.isType == Yield) {
			mtx.unlock();
			condAtomicFlag2.test_and_set();
			condAtomicFlag2.notify_one();
			EM_ASM(Module.channel.port2.postMessage(""););
			break;
		} else if (evt2.isType == AsyncToSync) {
			evt2.out = evt2.func();
			mtx.unlock();
			waiting = true;
			EM_ASM(Module.channel.port2.postMessage(""););
			break;
		}
		evt2.out = evt2.func();
		mtx.unlock();
        condAtomicFlag2.test_and_set();
        condAtomicFlag2.notify_one();
	}
}
KEEP_IN_MODULE void loop1() {
	#if 0
	while(1) {
		Event evt;
		while (eventQueue1.try_dequeue(evt)) {
			evt.func();
			if (evt.isType == Async)
				doYield = true;
			else if (evt.isType == AsyncToSync) {
				EM_ASM(Module.channel.port2.postMessage(""););
				return;
			}
		}
	}
	redraw();
	/*counter++;
	if (counter3++ < 1024 * 1024) {
		eventQueue1.enqueue(Event{false, [](){}});
		//eventQueue2.enqueue((uint32_t)new Event{false, [](){}});
		eventQueue3.push(1);
	}*/
	EM_ASM(Module.channel.port2.postMessage(""););
	#endif
}
KEEP_IN_MODULE int syncTest();
void loop1_start() {
	EM_ASM({
		Module.channel = new MessageChannel();
		Module.channel.port1.onmessage = () => {
			_loop00();
		};
		Module.channel.port2.postMessage("");
	});
}
void* eventQueueThread1(void* args) {
	#if 0
	while(1) {
		Event evt;
		while (eventQueue1.try_dequeue(evt)) {
			counter1++;
		}
	}
	#endif
	return nullptr;
}
void* eventQueueThread2(void* args) {
	while(1) {
		uint32_t evt;
		//while (eventQueue2.try_dequeue(evt)) {

		//}
	}
}
void* eventQueueThread3(void* args) {
	return nullptr;
}
void mainloop() {
//	EM_ASM(console.log('1'););

}
int syncTest_internal() {
	return 1;
}
std::future<int> asyncTest_internal() {
	return std::async([](){ return 1;});
}
int asyncToSyncTest_internal() {
	return asyncTest_internal().get();
}
std::atomic<bool> evtLock = {false};
KEEP_IN_MODULE void printTest(){
						EM_ASM(console.log('count:', $0);, counter);
}
KEEP_IN_MODULE int syncTest() {
	//EM_ASM(console.log('start sync0'));
	//EM_ASM(console.log('start wait evt0'));
	#if 0
	auto v = new Event{Sync, []() -> void* {
		//EM_ASM(console.log('cool'));
		//auto in = syncTest_internal();
		//auto out = malloc(sizeof(int));
		//memcpy(out, &in, sizeof(int));
		return new int(1);
	}};
	#endif
	mtx.lock();
	evt2 = Event{Sync, []() -> void* {
		//auto in = syncTest_internal();
		WGPUCommandEncoder in = wgpuDeviceCreateCommandEncoder(device, nullptr);
		auto out = malloc(sizeof(WGPUCommandEncoder));
		memcpy(out, &in, sizeof(WGPUCommandEncoder));
		return out;
	}};
	mtx.unlock();
	condAtomicFlag1.test_and_set();
	condAtomicFlag1.notify_one();
	condAtomicFlag2.wait(false);
	condAtomicFlag2.clear();
	condAtomicFlag1.clear();
	auto val = *(int*)evt2.out;
	//free(evt2.out);
	//free(v->out);
	//delete v;
	//v = nullptr;
	//EM_ASM(console.log('end sync0'));
	return val;
}
std::thread t;
extern "C" void* __main__(int /*argc*/, char* /*argv*/[]) {

	//pthread_t thread1; pthread_create(&thread1, 0, eventQueueThread1, 0);
	//pthread_t thread2; pthread_create(&thread2, 0, eventQueueThread2, 0);
	//pthread_t thread3; pthread_create(&thread3, 0, eventQueueThread3, 0);

	if (window::Handle wHnd = window::create()) {
		if ((device = webgpu::create(wHnd))) {
			queue = wgpuDeviceGetQueue(device);
			swapchain = webgpu::createSwapChain(device);
			createPipelineAndBuffers();

			window::show(wHnd);
			//emscripten_request_animation_frame_loop(tick, (void*)1);
			//emscripten_set_timeout_loop(tick, 1, (void*)1);
			//emscripten_set_timeout(loop2, 1, (void*)2);
			t = std::thread([](){
				dispatch();
#if 1
				while(1) {
					redraw();
				}
#else
				while(1) {
					counter += syncTest();
					if (emscripten_get_now() - start > 5000) {
						EM_ASM(console.log('count:', $0);, counter);
						break;
					}
				}
#endif
			});
			t.detach();
			loop1_start();

			//loop2();
			//for(;;) {
		
			//redraw();
				//emscripten_current_thread_process_queued_calls();
				//emscripten_thread_sleep(1);
				//EM_ASM(setTimeout(_redraw, 1););
			//}

		}
	}
	return 0;
}
EM_BOOL em_redraw(double /*time*/, void *userData) {
	return redraw(); // If this returns true, rAF() will continue, otherwise it will terminate
}
