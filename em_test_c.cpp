#define _GNU_SOURCE
#include <assert.h>
#include <emscripten.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include "proxying.h"

// The worker threads we will use. `looper` sits in a loop, continuously
// processing work as it becomes available, while `returner` returns to the JS
// event loop each time it processes work.
pthread_t main_thread;
pthread_t looper;
pthread_t returner;

// The queue used to send work to both `looper` and `returner`.
em_proxying_queue* proxy_queue = NULL;

// Whether `looper` should exit.
_Atomic int should_quit = 0;

// Whether `returner` has spun up.
_Atomic int has_begun = 0;

void* looper_main(void* arg) {
  while (!should_quit) {
    emscripten_proxy_execute_queue(proxy_queue);
    sched_yield();
  }
  return NULL;
}

void* returner_main(void* arg) {
  has_begun = 1;
  emscripten_exit_with_live_runtime();
}

typedef struct widget {
  // `val` will be stored to `out` and the current thread will be stored to
  // `thread` when the widget is run.
  int* out;
  int val;
  pthread_t thread;

  // Synchronization to allow waiting on a widget to run.
  pthread_mutex_t mutex;
  pthread_cond_t cond;

  // Nonzero iff the widget has been run.
  int done;

  // Only used for async_as_sync tests.
  em_proxying_ctx* ctx;
} widget;

void init_widget(widget* w, int* out, int val) {
  *w = (widget){.out = out,
                .val = val,
                // .thread will be set in `run_widget`.
                .mutex = PTHREAD_MUTEX_INITIALIZER,
                .cond = PTHREAD_COND_INITIALIZER,
                .done = 0,
                .ctx = NULL};
}

void destroy_widget(widget* w) {
  pthread_mutex_destroy(&w->mutex);
  pthread_cond_destroy(&w->cond);
}

void run_widget(widget* w) {
  pthread_t self = pthread_self();
  const char* name = pthread_equal(self, main_thread) ? "main"
                     : pthread_equal(self, looper)    ? "looper"
                     : pthread_equal(self, returner)  ? "returner"
                                                      : "unknown";
  printf("running widget %d on %s\n", w->val, name);
  pthread_mutex_lock(&w->mutex);
  if (w->out) {
    *w->out = w->val;
  }
  w->thread = pthread_self();
  w->done = 1;
  pthread_mutex_unlock(&w->mutex);
  pthread_cond_broadcast(&w->cond);
}

void await_widget(widget* w) {
  pthread_mutex_lock(&w->mutex);
  while (!w->done) {
    pthread_cond_wait(&w->cond, &w->mutex);
  }
  pthread_mutex_unlock(&w->mutex);
}

// Helper functions we will proxy to perform our work.
int count = 0;
double start = 0.0;
void do_run_widget(void* arg) {
    count++;
    if (count == 1)
       start = EM_ASM_DOUBLE(return performance.now());
    else if(count == 160000) {
        EM_ASM(console.log('took', performance.now()-$0), start);
        count = 0;
    }
}

void finish_running_widget(void* arg) {
  widget* w = (widget*)arg;
  run_widget(w);
  emscripten_proxy_finish(w->ctx);
}

void start_running_widget(em_proxying_ctx* ctx, void* arg) {
  ((widget*)arg)->ctx = ctx;
  emscripten_async_call(finish_running_widget, arg, 0);
}

void start_and_finish_running_widget(em_proxying_ctx* ctx, void* arg) {
  ((widget*)arg)->ctx = ctx;
  finish_running_widget(arg);
}

// Main test functions

void test_proxy_async(void) {
  printf("Testing async proxying\n");

  int i = 0;
  widget w1, w2, w3;
  for (;;) {
    emscripten_proxy_async(proxy_queue, looper, do_run_widget, &w2);
  }
}

void test_proxy_sync(void) {
  printf("Testing sync proxying\n");

  int i = 0;
  widget w4, w5;
  init_widget(&w4, &i, 4);
  init_widget(&w5, &i, 5);

  // Proxy to looper.
  auto start = EM_ASM_DOUBLE(return performance.now());
  for (int i = 0; i < 50000; ++i) {
    emscripten_proxy_sync(proxy_queue, looper, do_run_widget, &w4);
  }
  EM_ASM(console.log('took1', performance.now()-$0), start);

  // Proxy to returner.
  start = EM_ASM_DOUBLE(return performance.now());
  for (int i = 0; i < 50000; ++i) {
    emscripten_proxy_sync(proxy_queue, returner, do_run_widget, &w5);
  }
  EM_ASM(console.log('took2', performance.now()-$0), start);
  assert(i == 5);
  assert(w5.done);
  assert(pthread_equal(w5.thread, returner));

  destroy_widget(&w4);
  destroy_widget(&w5);
}

typedef struct increment_to_arg {
  em_proxying_queue* queue;
  int* ip;
  int i;
} increment_to_arg;

void increment_to(void* arg_p) {
  increment_to_arg* arg = (increment_to_arg*)arg_p;

  // Try executing the queue; since the queue is already being executed, this
  // shouldn't do anything and *arg->ip should still be one less than arg->i
  // afterward.
  emscripten_proxy_execute_queue(arg->queue);

  assert(*arg->ip == arg->i - 1);
  *arg->ip = arg->i;
  free(arg);
}

void trivial_work(void* arg) {
  printf("work\n");
  (*(_Atomic int*)arg)++;
}

int main(int argc, char* argv[]) {
  main_thread = pthread_self();

  proxy_queue = em_proxying_queue_create();
  assert(proxy_queue != NULL);

  pthread_create(&looper, NULL, looper_main, NULL);
  pthread_create(&returner, NULL, returner_main, NULL);

  // `returner` can't process its queue until it starts up.
  while (!has_begun) {
    sched_yield();
  }

  test_proxy_async();
  //test_proxy_sync();

  should_quit = 1;
  pthread_join(looper, NULL);

  pthread_cancel(returner);
  pthread_join(returner, NULL);

  em_proxying_queue_destroy(proxy_queue);

  printf("done\n");
}