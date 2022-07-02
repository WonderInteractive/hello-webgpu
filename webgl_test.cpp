#include "helper.cpp"

inline void setupGL() {
        WEBGPU_EXEC({

        }, 1);
}
inline void drawTest() {
        WEBGPU_EXEC({

        }, 1);
}
double start1 = 0.0;
uint64_t N = 1000;
uint64_t N2 = 1000;
uint64_t T = 1;
int main() {
    thread1 = std::thread([](){
        printf("starting thread\n");
        loop_atomic_start();
    });
    thread1.detach();

    setupGL();

    std::vector<std::thread> threads;
    for (int i = 0; i < T; ++i)
        threads.push_back(std::thread([](){
            for(;;){
                for (uint64_t i = 0; i < N; ++i) {
                    drawTest();
                    WEBGPU_YIELD
                }
                WEBGPU_YIELD
            }
        }));
    for (int i = 0; i < T; ++i)
        threads[i].join();
}