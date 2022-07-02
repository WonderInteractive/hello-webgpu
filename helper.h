#include <atomic>
#include <string.h>
#include <thread>
#include <vector>
#include <emscripten.h>
#include <emscripten/threading.h>
#include "src/atomic_queue/atomic_queue.h"
#define USING_FUNCTION 1
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif
#define print(text) EM_ASM(console.log(text));
#define PUSH(func, ...) getTypeV_atomic([]() -> void* {\
		func(__VA_ARGS__);\
		return nullptr;\
	});
#if USING_FUNCTION
#define WEBGPU_EXEC(code, ...) q.push([& __VA_ARGS__](char i) -> void*{ code return nullptr; });
//#define WEBGPU_EXEC(code, ...) q.enqueue([__VA_ARGS__](char i) -> void*{ code return nullptr; }, 0);
#else
#define WEBGPU_EXEC(code, ...) q.push((uint32_t)static_cast<void*(*)(void)>([]() -> void* { code return nullptr; }));
//#define WEBGPU_EXEC(code, ...) total_size++; while(total_size > 100000){}q2.enqueue((uint32_t)static_cast<void*(*)(void)>([]() -> void* { code return nullptr; }), t_id);
#endif
#define WEBGPU_WAIT 
#define WEBGPU_YIELD WEBGPU_WAIT yieldToJS();