#include <atomic>
#include <string.h>
#include <thread>
#include <emscripten.h>
#include <emscripten/threading.h>
#define print(text) EM_ASM(console.log(text));
#define CPP_ATOMICS 0
void* yieldPtr = (void*)-1;
enum Type {
    Sync, Async, Yield
};
std::thread thread1;
std::atomic<void*> func_and_type{nullptr};
void* func_and_type2 = nullptr;
std::atomic<Type> yieldType {Sync};
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif
void wait1(volatile void *address, int on) {
	int state = emscripten_atomic_load_u32((void*)address);
	while(state == on)
		state = emscripten_atomic_load_u32((void*)address);
}
void wait(volatile void *address, int on) {
	volatile auto ret = __builtin_wasm_memory_atomic_wait32((int*)func_and_type2, on, -1);
    //sprintf("ret %d\n", ret);
}
//benchmark using a mutex vs atomic vs same thread
void* reserve = malloc(1024 * 1024 * 1);
template <typename T>
T getType_atomic(void*(func)()) {
    auto ptr2 = (uintptr_t)func;
    #if CPP_ATOMICS
    func_and_type.store((void*)ptr2);
    func_and_type.notify_one();
    func_and_type.wait((void*)ptr2);
    auto f = func_and_type.load();
    func_and_type.store(0);
    #else
    __c11_atomic_store((_Atomic uint32_t*)func_and_type2, (uintptr_t)ptr2, __ATOMIC_RELAXED);
    __builtin_wasm_memory_atomic_notify((int *)func_and_type2, 1);
    wait1(func_and_type2, ptr2);
    auto f = __c11_atomic_load((_Atomic uint32_t*)func_and_type2, __ATOMIC_RELAXED) ;
    __c11_atomic_store((_Atomic uint32_t*)func_and_type2, 0, __ATOMIC_RELAXED);
    #endif
	auto val = *(T*)reserve;
	return val;
}
void getTypeV_atomic(void*(func)()) {
    auto ptr2 = (uintptr_t)func;
    func_and_type = (void*)ptr2;
    func_and_type.notify_one();
    func_and_type.wait((void*)func);
    func_and_type.store(0);
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
int return1() {
    return fib(fib_n);
}
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
        #else
        wait1(func_and_type2, 0);
        if (func_and_type2 == yieldPtr) {
            EM_ASM(Module.channel.port2.postMessage(""););
            return;
        }
        auto ptr0 = __c11_atomic_load((_Atomic uintptr_t*)func_and_type2, __ATOMIC_RELAXED);
        auto f = (void*(*)())ptr0;
        auto ptr = f();
        //auto ptr = reserve;
        __c11_atomic_store((_Atomic(uint32_t) *)func_and_type2, (uintptr_t)ptr, __ATOMIC_RELAXED);
        __builtin_wasm_memory_atomic_notify((int *)func_and_type2, 1);
        wait1(func_and_type2, (int)ptr);
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
    __c11_atomic_store((_Atomic(uint32_t) *)func_and_type2, (uintptr_t)yieldPtr, __ATOMIC_SEQ_CST);
    __builtin_wasm_memory_atomic_notify((int *)func_and_type2, 1);
    #endif
}
static volatile int ret = 0;
int main() {
    printf("starting\n");
    thread1 = std::thread([](){
        printf("starting thread\n");
        loop_atomic_start();
    });
    thread1.detach();
    int N = 10000;
    //sync test - atomics
    auto start = emscripten_get_now();
    for (;;) {
        start = emscripten_get_now();
        for (int i = 0; i < N; ++i) {
            ret = return2();
        }
        auto r = emscripten_get_now() - start;
        printf("sync test - atomics (%d): %f : %f\n", ret, r, r / N * 1000);
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
		yieldToJS();
	}
    ret = ret3;
    printf("async test (%d): %f\n", ret, emscripten_get_now() - start);
    printf("res: %d\n", ret);
    return ret;
}