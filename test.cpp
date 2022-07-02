#include "helper.cpp"

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
int return5() {
    return 5;
}
int return1() {
    return fib(fib_n);
}
#define return1(...) PUSH(return2, __VA_ARGS__)
KEEP_IN_MODULE void loop_mutex() {
}

//compare with proxied asyncify using a queue (actually probably cant use a queue without waiting, so do direct)
//compare with proxied looper thread
//compare with pthreadfs
//do we need to wait with a queue at all if we always return promised objects?
volatile int b;
inline void simpleTest() {
        int test1 = 1;
        int test2 = 2;
        b++;
        WEBGPU_EXEC({
            //printf("test1 %d test2 %d\n", test1, test2);
            //exit(1);
            //volatile int f = fib(5);
            //uint64_t v1 = 0;
            //for (uint64_t i = 0; i < N2; ++i) {
             //   v1 += 1;
                //return5();
            //}
            //ret += v1;
            //printf("inside %" PRIu64 "\n", ret.load());
        });
}
volatile int func = 0;
void* threadTest(void*) {
    t_id = t_idinc++;
    uint64_t tick = 0;
    for(;;) {
       for (uint64_t i = 0; i < CALLS_PER; ++i) {
            simpleTest();
            if (tick++ % YIELD_FREQ == 0)
                WEBGPU_YIELD
        }
        //WEBGPU_YIELD
        //volatile int f = fib(41);
        //emscripten_thread_sleep(10000);
    }
    return nullptr;
}
int main() {
    thread1 = std::thread([](){
        printf("starting thread\n");
        loop_atomic_start();
        //emscripten_thread_sleep(5000);
        //func = 1;
        //__builtin_wasm_memory_atomic_notify((int*)&func, 1);
        //volatile int f = fib(80);
    });
    thread1.detach();
    //__builtin_wasm_memory_atomic_wait32((int*)&func, 0, -1);
    //printf("done wait test\n");
    //__builtin_wasm_memory_atomic_wait32((int*)&func, 1, -1);
    //volatile int f = fib(80);
    //sync test - atomics
    #if 1
    std::vector<pthread_t> threads(T);
    for (int i = 0; i < T; ++i)
        pthread_create(&threads[i], 0, threadTest, 0);
    for (int i = 0; i < T; ++i)
        pthread_join(threads[i], 0);
    exit(1);
    #endif
    auto start = emscripten_get_now();
    for (;;) {
        ret = 0;
        //start = emscripten_get_now();
        start = EM_ASM_DOUBLE(return performance.now());
        for (uint64_t i = 0; i < CALLS; ++i) {
            simpleTest();
            WEBGPU_YIELD
        }
        auto start2 = EM_ASM_DOUBLE(return performance.now());
        WEBGPU_YIELD
        auto r = EM_ASM_DOUBLE(return performance.now()) - start;
        r = EM_ASM_DOUBLE(return (performance.now() - $0), start);
        auto r2 = EM_ASM_DOUBLE(return performance.now()) - start2;
        //printf("outside %" PRIu64 "\n", ret.load());
        MAIN_THREAD_EM_ASM(window.document.title = $0;, r);
        //printf("sync test - void (%" PRIu64 "): %f : %f : %f\n", ret.load(), r, r / N, r2);
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
    start = emscripten_get_now();
    while (waiting) {
		WEBGPU_YIELD
	}
    //ret = ret3;
    printf("async test (%d): %f\n", ret, emscripten_get_now() - start);
    printf("res: %d\n", ret);
    return ret;
}