#include <atomic>
#include <string.h>
#include <thread>
#include <vector>
#include <emscripten.h>
#include <emscripten/threading.h>
#include "src/concurrentqueue/concurrentqueue.h"
moodycamel::ConcurrentQueue<void*> eventQueue1(1024);
#define print(text) EM_ASM(console.log(text));
#define CPP_ATOMICS 0
#define C11_ATOMICS 1
#define USING_QUEUE 0
void* yieldPtr = (void*)-1;
enum Type {
    Sync, Async, Yield
};
std::thread thread1;
std::atomic<void*> func_and_type{nullptr};
void* func_and_type2 = nullptr;
std::atomic<int> ptr_index{0};
std::atomic<int> doing{0};
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif
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
template <typename T>
struct Future {
    void* func = nullptr;
    void* data = nullptr;
    T get() {
        wait1(data, 0);
        return *(T*)data;
    }
};
std::vector<void*> funcs{1000}; 
std::vector<void*> datas{1000};
std::atomic<Type> yieldType {Sync};
std::atomic<bool> needsRet {false};

void wait(volatile void *address, int on) {
	volatile auto ret = __builtin_wasm_memory_atomic_wait32((int*)&func_and_type2, on, -1);
    //sprintf("ret %d\n", ret);
}
//benchmark using a mutex vs atomic vs same thread
void* reserve = malloc(1024 * 1024 * 1);
template <typename T>
T getType_atomic(void*(func)()) {
    needsRet = true;
    auto ptr2 = (uintptr_t)func;
    #if CPP_ATOMICS
    func_and_type.store((void*)ptr2);
    func_and_type.notify_one();
    func_and_type.wait((void*)ptr2);
    auto f = func_and_type.load();
    func_and_type.store(0);
    #else
    //auto index = ptr_index++;
    //__c11_atomic_store((_Atomic uint32_t*)func_and_type2, (uintptr_t)index, __ATOMIC_RELAXED);
    //__c11_atomic_store((_Atomic uint32_t*)funcs[index], (uintptr_t)ptr2, __ATOMIC_RELAXED);
    __c11_atomic_store((_Atomic uint32_t*)&func_and_type2, (uintptr_t)ptr2, __ATOMIC_RELAXED);
    __builtin_wasm_memory_atomic_notify((int *)&func_and_type2, 1);
    wait1(func_and_type2, ptr2);
    auto f = __c11_atomic_exchange((_Atomic uint32_t*)&func_and_type2, 0, __ATOMIC_RELAXED);
    #endif
	auto val = *(T*)reserve;
	return val;
}
void getTypeV_atomic(void*(func)()) {
    needsRet = false;
    auto ptr2 = (uintptr_t)func;
    wait2(&func_and_type2, 0);
    __c11_atomic_store((_Atomic uint32_t*)&func_and_type2, (uintptr_t)ptr2, __ATOMIC_RELAXED);
    __builtin_wasm_memory_atomic_notify((int *)&func_and_type2, 1);
    //wait1(func_and_type2, ptr2);
    //__c11_atomic_store((_Atomic uint32_t*)func_and_type2, 0, __ATOMIC_RELAXED);
}
#define PUSH(func, ...) getTypeV_atomic([]() -> void* {\
		func(__VA_ARGS__);\
		return nullptr;\
	});
#define PUSH_ASYNC(func, ...) getTypeV_atomic([]() -> void* {\
		func(__VA_ARGS__);\
		return nullptr;\
	});
#define PUSH2(T, func, ...) getType_atomic<T>([]() -> void* {\
		T in = func(__VA_ARGS__);\
		memcpy(reserve, &in, sizeof(T));\
		return reserve;\
	});
#if USING_QUEUE
#define WEBGPU_START doing++; eventQueue1.enqueue((void*)static_cast<void*(*)(void)>([]() -> void* {
#define WEBGPU_END doing--; return nullptr; }));
//#define WEBGPU_WAIT while(eventQueue1.size_approx() > 0){};
#define WEBGPU_WAIT while(doing > 0){};
#define WEBGPU_YIELD yieldToJS();
#else
#define WEBGPU_START doing++; getTypeV_atomic([]() -> void* {
#define WEBGPU_END doing--; return nullptr; });
#define WEBGPU_WAIT while(doing > 0){};
#define WEBGPU_YIELD yieldToJS();
#endif
std::atomic<bool> waiting{false};
std::atomic<int> ret3{1};
int fib_n = 25;
int fib(int n) {
   if (n <= 1)
      return n;
   return fib(n-1) + fib(n-2);
}
KEEP_IN_MODULE void setReturn3() {
    ret3 = fib(fib_n);
    waiting = false;
};
void return3() {
    waiting = true;
    EM_ASM({
        setTimeout(_setReturn3(), 500);
    });
}
int return2() {
    return 1;
}
int return5() {
    return 5;
}
int return1() {
    return fib(fib_n);
}
#define return1(...) PUSH(return2, __VA_ARGS__)
#define return2(...) PUSH2(int, return2, __VA_ARGS__)
#define return3(...) PUSH_ASYNC(return3, __VA_ARGS__)
KEEP_IN_MODULE void loop_mutex() {
}
KEEP_IN_MODULE void loop_atomic() {
	while(1) {
        #if CPP_ATOMICS
        func_and_type.wait(0);
        if (func_and_type == yieldPtr) {
            EM_ASM(Module.channel.port2.postMessage(""););
            return;
        }
        auto f = (void*(*)())func_and_type.load();
        auto ptr = f();
        func_and_type.store(ptr);
        func_and_type.notify_one();
        func_and_type.wait(ptr);
        #elif C11_ATOMICS
        wait1(&func_and_type2, 0);
        auto ptr0 = __c11_atomic_exchange((_Atomic uintptr_t*)&func_and_type2, 0, __ATOMIC_RELAXED);
        if (ptr0 == (uintptr_t)yieldPtr) {
            EM_ASM(Module.channel.port2.postMessage(""););
            return;
        }
        //__builtin_wasm_memory_atomic_notify((int *)func_and_type2, 1);
        auto f = (void*(*)())ptr0;
        auto ptr = f();
        //auto ptr = reserve;
        //__c11_atomic_store((_Atomic(uint32_t) *)func_and_type2, (uintptr_t)ptr, __ATOMIC_RELAXED);
        //if(needsRet) {
        //    wait1(func_and_type2, (int)ptr);
        //}
        #else
        while(1) {
		void* evt;
		while (eventQueue1.try_dequeue(evt)) {
            auto f = (void*(*)())evt;
			if (evt != yieldPtr)
				auto ptr = f();
			else {
				EM_ASM(Module.channel.port2.postMessage(""););
				return;
			}
		}
        //printf("size of queue %d\n", eventQueue1.size_approx());
	    }
        #endif
	}
}
void loop_mutex_start() {
	EM_ASM({
		Module.channel = new MessageChannel();
		Module.channel.port1.onmessage = () => {
			_loop_mutex();
		};
		Module.channel.port2.postMessage("");
	});
}
void loop_atomic_start() {
	EM_ASM({
		Module.channel = new MessageChannel();
		Module.channel.port1.onmessage = () => {
			_loop_atomic();
		};
		Module.channel.port2.postMessage("");
	});
}
void yieldToJS() {
    #if CPP_ATOMICS
    func_and_type.store(yieldPtr);
    func_and_type.notify_one();
    #else
    __c11_atomic_store((_Atomic(uint32_t) *)&func_and_type2, (uintptr_t)yieldPtr, __ATOMIC_SEQ_CST);
    __builtin_wasm_memory_atomic_notify((int *)&func_and_type2, 1);
    #endif
}
//compare with proxied asyncify using a queue (actually probably cant use a queue without waiting, so do direct)
//compare with proxied looper thread
//compare with pthreadfs
//do we need to wait with a queue at all if we always return promised objects?
static volatile std::atomic<uint64_t> ret{0};
void simpleTest() {
        WEBGPU_START
        for (int i = 0; i < 1000; ++i) {
            ret += 
            return5();
        }
        //printf("inside %" PRIu64 "\n", ret.load());
        WEBGPU_END
}
int main() {
    printf("starting\n");
    thread1 = std::thread([](){
        printf("starting thread\n");
        loop_atomic_start();
    });
    thread1.detach();
    int N = 1000;
    //sync test - atomics
    auto start = emscripten_get_now();
    for (;;) {
        ret = 0;
        start = emscripten_get_now();
        for (int i = 0; i < N; ++i) {
        simpleTest();
        }
        auto start2 = emscripten_get_now();
        WEBGPU_WAIT
        auto r = emscripten_get_now() - start;
        auto r2 = emscripten_get_now() - start2;
        //printf("outside %" PRIu64 "\n", ret.load());
        printf("sync test - void (%" PRIu64 "): %f : %f : %f\n", ret.load(), r, r / N, r2);
        #if 0
        start = emscripten_get_now();
        for (int i = 0; i < N; ++i) {
            ret = 
            return2();
        }
        auto r2 = emscripten_get_now() - start;
        printf("sync test - atomics (%d): %f : %f\n", ret, r2, r2 / N);
        #endif
    }
    //sync test - mutex
    start = emscripten_get_now();
    for (int i = 0; i < N; ++i) {

    }
    printf("sync test - mutex: %f\n", emscripten_get_now() - start);
    //sync test - same thread
    start = emscripten_get_now();
    for (int i = 0; i < N; ++i) {
    //    ret = return1();
    }
    printf("sync test - same thread (%d): %f\n", ret, emscripten_get_now() - start);
    //async test - atomics or mutex, depending on the prior tests
    start = emscripten_get_now();
    for (int i = 0; i < N; ++i) {
        return3();
    }
    while (waiting) {
		WEBGPU_YIELD
	}
    //ret = ret3;
    printf("async test (%d): %f\n", ret, emscripten_get_now() - start);
    printf("res: %d\n", ret);
    return ret;
}