#include "helper.h"
#include <deque>
#define PROFILING 1
#if USING_FUNCTION
#define ELEMENT std::function<void*(int)>
#else
#define ELEMENT uint32_t
#endif
uint32_t constexpr NIL = static_cast<uint32_t>(-1);

#include <atomic>
#include <cassert>
#include <cstddef> // offsetof
#include <limits>
#include <memory>
#include <new> // std::hardware_destructive_interference_size
#include <stdexcept>

#ifndef __cpp_aligned_new
#ifdef _WIN32
#include <malloc.h> // _aligned_malloc
#else
#include <stdlib.h> // posix_memalign
#endif
#endif

namespace rigtorp {
namespace mpmc {
#if defined(__cpp_lib_hardware_interference_size) && !defined(__APPLE__)
static constexpr size_t hardwareInterferenceSize =
    std::hardware_destructive_interference_size;
#else
static constexpr size_t hardwareInterferenceSize = 64;
#endif

#if defined(__cpp_aligned_new)
template <typename T> using AlignedAllocator = std::allocator<T>;
#else
template <typename T> struct AlignedAllocator {
  using value_type = T;

  T *allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
#ifdef _WIN32
    auto *p = static_cast<T *>(_aligned_malloc(sizeof(T) * n, alignof(T)));
    if (p == nullptr) {
      throw std::bad_alloc();
    }
#else
    T *p;
    if (posix_memalign(reinterpret_cast<void **>(&p), alignof(T),
                       sizeof(T) * n) != 0) {
      throw std::bad_alloc();
    }
#endif
    return p;
  }

  void deallocate(T *p, std::size_t) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
  }
};
#endif

template <typename T> struct Slot {
  ~Slot() noexcept {
    if (turn & 1) {
      destroy();
    }
  }

  template <typename... Args> void construct(Args &&...args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                  "T must be nothrow constructible with Args&&...");
    new (&storage) T(std::forward<Args>(args)...);
  }

  void destroy() noexcept {
    static_assert(std::is_nothrow_destructible<T>::value,
                  "T must be nothrow destructible");
    reinterpret_cast<T *>(&storage)->~T();
  }

  T &&move() noexcept { return reinterpret_cast<T &&>(storage); }

  // Align to avoid false sharing between adjacent slots
  alignas(hardwareInterferenceSize) std::atomic<size_t> turn = {0};
  typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
};

template <typename T, typename Allocator = AlignedAllocator<Slot<T>>>
class Queue {
private:
  static_assert(std::is_nothrow_copy_assignable<T>::value ||
                    std::is_nothrow_move_assignable<T>::value,
                "T must be nothrow copy or move assignable");

  static_assert(std::is_nothrow_destructible<T>::value,
                "T must be nothrow destructible");

public:
  explicit Queue(const size_t capacity,
                 const Allocator &allocator = Allocator())
      : capacity_(capacity), allocator_(allocator), head_(0), tail_(0) {
    if (capacity_ < 1) {
      throw std::invalid_argument("capacity < 1");
    }
    // Allocate one extra slot to prevent false sharing on the last slot
    slots_ = allocator_.allocate(capacity_ + 1);
    // Allocators are not required to honor alignment for over-aligned types
    // (see http://eel.is/c++draft/allocator.requirements#10) so we verify
    // alignment here
    if (reinterpret_cast<size_t>(slots_) % alignof(Slot<T>) != 0) {
      allocator_.deallocate(slots_, capacity_ + 1);
      throw std::bad_alloc();
    }
    for (size_t i = 0; i < capacity_; ++i) {
      new (&slots_[i]) Slot<T>();
    }
    static_assert(
        alignof(Slot<T>) == hardwareInterferenceSize,
        "Slot must be aligned to cache line boundary to prevent false sharing");
    static_assert(sizeof(Slot<T>) % hardwareInterferenceSize == 0,
                  "Slot size must be a multiple of cache line size to prevent "
                  "false sharing between adjacent slots");
    static_assert(sizeof(Queue) % hardwareInterferenceSize == 0,
                  "Queue size must be a multiple of cache line size to "
                  "prevent false sharing between adjacent queues");
    static_assert(
        offsetof(Queue, tail_) - offsetof(Queue, head_) ==
            static_cast<std::ptrdiff_t>(hardwareInterferenceSize),
        "head and tail must be a cache line apart to prevent false sharing");
  }

  ~Queue() noexcept {
    for (size_t i = 0; i < capacity_; ++i) {
      slots_[i].~Slot();
    }
    allocator_.deallocate(slots_, capacity_ + 1);
  }

  // non-copyable and non-movable
  Queue(const Queue &) = delete;
  Queue &operator=(const Queue &) = delete;

  template <typename... Args> void emplace(Args &&...args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                  "T must be nothrow constructible with Args&&...");
    auto const head = head_.fetch_add(1);
    auto &slot = slots_[idx(head)];
    while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire))
      ;
    slot.construct(std::forward<Args>(args)...);
    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
  }

  template <typename... Args> bool try_emplace(Args &&...args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                  "T must be nothrow constructible with Args&&...");
    auto head = head_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(head)];
      if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire)) {
        if (head_.compare_exchange_strong(head, head + 1)) {
          slot.construct(std::forward<Args>(args)...);
          slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
          return true;
        }
      } else {
        auto const prevHead = head;
        head = head_.load(std::memory_order_acquire);
        if (head == prevHead) {
          return false;
        }
      }
    }
  }

  void push(const T &v) noexcept {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    emplace(v);
  }

  template <typename P,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T, P &&>::value>::type>
  void push(P &&v) noexcept {
    emplace(std::forward<P>(v));
  }

  bool try_push(const T &v) noexcept {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    return try_emplace(v);
  }

  template <typename P,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T, P &&>::value>::type>
  bool try_push(P &&v) noexcept {
    return try_emplace(std::forward<P>(v));
  }

  void pop(T &v) noexcept {
    auto const tail = tail_.fetch_add(1);
    auto &slot = slots_[idx(tail)];
    while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire))
      ;
    v = slot.move();
    slot.destroy();
    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
  }

  bool try_pop(T &v) noexcept {
    auto tail = tail_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(tail)];
      if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire)) {
        if (tail_.compare_exchange_strong(tail, tail + 1)) {
          v = slot.move();
          slot.destroy();
          slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
          return true;
        }
      } else {
        auto const prevTail = tail;
        tail = tail_.load(std::memory_order_acquire);
        if (tail == prevTail) {
          return false;
        }
      }
    }
  }

  /// Returns the number of elements in the queue.
  /// The size can be negative when the queue is empty and there is at least one
  /// reader waiting. Since this is a concurrent queue the size is only a best
  /// effort guess until all reader and writer threads have been joined.
  ptrdiff_t size() const noexcept {
    // TODO: How can we deal with wrapped queue on 32bit?
    return static_cast<ptrdiff_t>(head_.load(std::memory_order_relaxed) -
                                  tail_.load(std::memory_order_relaxed));
  }

  /// Returns true if the queue is empty.
  /// Since this is a concurrent queue this is only a best effort guess
  /// until all reader and writer threads have been joined.
  bool empty() const noexcept { return size() <= 0; }

private:
  constexpr size_t idx(size_t i) const noexcept { return i % capacity_; }

  constexpr size_t turn(size_t i) const noexcept { return i / capacity_; }

private:
  const size_t capacity_;
  Slot<T> *slots_;
#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
  Allocator allocator_ [[no_unique_address]];
#else
  Allocator allocator_;
#endif

  // Align to avoid false sharing between head_ and tail_
  alignas(hardwareInterferenceSize) std::atomic<size_t> head_;
  alignas(hardwareInterferenceSize) std::atomic<size_t> tail_;
};
} // namespace mpmc

template <typename T,
          typename Allocator = mpmc::AlignedAllocator<mpmc::Slot<T>>>
using MPMCQueue = mpmc::Queue<T, Allocator>;

}

template<typename T>
class queue {
  std::deque<T> content;
  size_t capacity;

  std::mutex mutex;
  std::condition_variable not_empty;
  std::condition_variable not_full;

  queue(const queue &) = delete;
  queue(queue &&) = delete;
  queue &operator = (const queue &) = delete;
  queue &operator = (queue &&) = delete;

 public:
  queue(size_t capacity): capacity(capacity) {}

  void push(T &&item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      not_full.wait(lk, [this]() { return content.size() < capacity; });
      content.push_back(std::move(item));
    }
    not_empty.notify_one();
  }

  bool try_push(T &&item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      if (content.size() == capacity)
        return false;
      content.push_back(std::move(item));
    }
    not_empty.notify_one();
    return true;
  }

  void pop(T &item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      not_empty.wait(lk, [this]() { return !content.empty(); });
      item = std::move(content.front());
      content.pop_front();
    }
    not_full.notify_one();
  }

  bool try_pop(T &item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      if (content.empty())
        return false;
      item = std::move(content.front());
      content.pop_front();
    }
    not_full.notify_one();
    return true;
  }
};

#ifndef ZIB_WAIT_MPSC_QUEUE_HPP_
#define ZIB_WAIT_MPSC_QUEUE_HPP_

#include <assert.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace zib {

    namespace wait_details {

        /* Shamelssly takend from
         * https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
         * as in some c++ libraries it doesn't exists
         */
#ifdef __cpp_lib_hardware_interference_size
        using std::hardware_constructive_interference_size;
        using std::hardware_destructive_interference_size;
#else
        // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
        // ...
        constexpr std::size_t hardware_constructive_interference_size = 64;
        constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

        template <typename Dec, typename F>
        concept Deconstructor = requires(const Dec _dec, F* _ptr)
        {
            {
                _dec(_ptr)
            }
            noexcept->std::same_as<void>;
            {
                Dec { }
            }
            noexcept->std::same_as<Dec>;
        };

        template <typename T>
        struct deconstruct_noop {
                void
                operator()(T*) const noexcept {};
        };

        static constexpr std::size_t kDefaultMPSCSize                 = 4096;
        static constexpr std::size_t kDefaultMPSCAllocationBufferSize = 16;

    }   // namespace wait_details

    template <
        typename T,
        wait_details::Deconstructor<T> F          = wait_details::deconstruct_noop<T>,
        std::size_t                    BufferSize = wait_details::kDefaultMPSCSize,
        std::size_t AllocationSize                = wait_details::kDefaultMPSCAllocationBufferSize>
    class wait_mpsc_queue {

        private:

            static constexpr auto kEmpty = std::numeric_limits<std::size_t>::max();

            static constexpr auto kAlignment = wait_details::hardware_destructive_interference_size;

            struct alignas(kAlignment) node {

                    node() : count_(kEmpty) { }

                    T data_;

                    std::atomic<std::uint64_t> count_;
            };

            struct alignas(kAlignment) node_buffer {

                    node_buffer() : read_head_(0), next_(nullptr), elements_{}, write_head_(0) { }

                    std::size_t read_head_ alignas(kAlignment);

                    node_buffer* next_ alignas(kAlignment);

                    node elements_[BufferSize];

                    std::size_t write_head_ alignas(kAlignment);
            };

            struct alignas(kAlignment) allocation_pool {

                    std::atomic<std::uint64_t> read_count_ alignas(kAlignment);

                    std::atomic<std::uint64_t> write_count_ alignas(kAlignment);

                    struct alignas(kAlignment) aligned_ptr {
                            node_buffer* ptr_;
                    };

                    aligned_ptr items_[AllocationSize];

                    void
                    push(node_buffer* _ptr)
                    {
                        auto write_idx = write_count_.load(std::memory_order_relaxed);

                        if (write_idx + 1 == read_count_.load(std::memory_order_relaxed))
                        {
                            delete _ptr;
                            return;
                        }

                        _ptr->~node_buffer();
                        new (_ptr) node_buffer();

                        items_[write_idx].ptr_ = _ptr;

                        if (write_idx + 1 != AllocationSize)
                        {
                            write_count_.store(write_idx + 1, std::memory_order_release);
                        }
                        else
                        {

                            write_count_.store(0, std::memory_order_release);
                        }
                    }

                    node_buffer*
                    pop()
                    {
                        auto read_idx = read_count_.load(std::memory_order_relaxed);
                        if (read_idx == write_count_.load(std::memory_order_acquire))
                        {
                            return new node_buffer;
                        }

                        auto tmp = items_[read_idx].ptr_;

                        if (read_idx + 1 != AllocationSize)
                        {

                            read_count_.store(read_idx + 1, std::memory_order_release);
                        }
                        else
                        {

                            read_count_.store(0, std::memory_order_release);
                        }

                        return tmp;
                    }

                    node_buffer*
                    drain()
                    {
                        auto read_idx = read_count_.load(std::memory_order_relaxed);
                        if (read_idx == write_count_.load(std::memory_order_relaxed))
                        {
                            return nullptr;
                        }

                        auto tmp = items_[read_idx].ptr_;

                        if (read_idx + 1 != AllocationSize)
                        {

                            read_count_.store(read_idx + 1, std::memory_order_relaxed);
                        }
                        else
                        {

                            read_count_.store(0, std::memory_order_relaxed);
                        }

                        return tmp;
                    }
            };

        public:

            using value_type         = T;
            using deconstructor_type = F;

            wait_mpsc_queue(std::uint64_t _num_threads)
                : heads_(_num_threads), lowest_seen_(0), sleeping_(false), tails_(_num_threads),
                  up_to_(0), buffers_(_num_threads)
            {
                for (std::size_t i = 0; i < _num_threads; ++i)
                {

                    auto* buf = new node_buffer;
                    heads_[i] = buf;
                    tails_[i] = buf;
                }
            }

            ~wait_mpsc_queue()
            {
                deconstructor_type t;

                for (auto h : heads_)
                {
                    while (h)
                    {

                        for (std::size_t i = h->read_head_; i < BufferSize; ++i)
                        {
                            if (h->elements_[i].count_.load() != kEmpty)
                            {
                                t(&h->elements_[i].data_);
                            }
                            else
                            {
                                break;
                            }
                        }

                        auto tmp = h->next_;
                        delete h;
                        h = tmp;
                    }
                }

                for (auto& q : buffers_)
                {
                    node_buffer* to_delete = nullptr;
                    while ((to_delete = q.drain()))
                    {
                        delete to_delete;
                    }
                }
            }

            void
            enqueue(T _data, std::uint16_t _t_id) noexcept
            {
                auto* buffer = tails_[_t_id];
                if (buffer->write_head_ == BufferSize - 1)
                {
                    tails_[_t_id] = buffers_[_t_id].pop();
                    buffer->next_ = tails_[_t_id];
                    assert(tails_[_t_id]);
                }

                auto cur = up_to_.fetch_add(1, std::memory_order_release);

                buffer->elements_[buffer->write_head_].data_ = _data;

                buffer->elements_[buffer->write_head_++].count_.store(
                    cur,
                    std::memory_order_release);

                if (sleeping_.load(std::memory_order_acquire) == true) { up_to_.notify_one(); }
            }

            T
            dequeue() noexcept
            {
                while (true)
                {
                    std::int64_t prev_index = -2;
                    while (true)
                    {
                        auto         min_count = kEmpty;
                        std::int64_t min_index = -1;

                        for (std::uint64_t i = 0; i < heads_.size(); ++i)
                        {
                            assert(heads_[i]->read_head_ < BufferSize);

                            auto count = heads_[i]->elements_[heads_[i]->read_head_].count_.load(
                                std::memory_order_acquire);

                            if (count < min_count)
                            {
                                min_count = count;
                                min_index = i;
                                if (min_count == lowest_seen_)
                                {
                                    prev_index = i;
                                    break;
                                }
                            }
                        }

                        if (min_index == -1 && prev_index == min_index)
                        {
                            if (up_to_.load(std::memory_order_relaxed) == lowest_seen_)
                            {
                                sleeping_.store(true, std::memory_order_release);
                                up_to_.wait(lowest_seen_, std::memory_order_acquire);
                                sleeping_.store(false, std::memory_order_relaxed);
                            }
                            break;
                        }

                        if (prev_index == min_index)
                        {
                            auto data =
                                heads_[min_index]->elements_[heads_[min_index]->read_head_++].data_;

                            if (heads_[min_index]->read_head_ == BufferSize)
                            {

                                auto tmp          = heads_[min_index];
                                heads_[min_index] = tmp->next_;

                                assert(heads_[min_index]);

                                buffers_[min_index].push(tmp);
                            }

                            if (lowest_seen_ == min_count) { lowest_seen_++; }

                            return data;
                        }

                        prev_index = min_index;
                    }
                }
            }

        private:

            std::vector<node_buffer*> heads_ alignas(kAlignment);
            std::size_t               lowest_seen_;

            std::atomic<bool> sleeping_ alignas(kAlignment);

            std::vector<node_buffer*>  tails_ alignas(kAlignment);
            std::atomic<std::uint64_t> up_to_ alignas(kAlignment);

            std::vector<allocation_pool> buffers_ alignas(kAlignment);
            char                         padding_[kAlignment - sizeof(buffers_)];
    };

}   // namespace zib

#endif /* ZIB_WAIT_MPSC_QUEUE_HPP_ */

using Queue = atomic_queue::AtomicQueue2<ELEMENT, 65536/8, true, true, false, false>; // Use heap-allocated buffer.
Queue q;//{1000};
//zib::wait_mpsc_queue<ELEMENT, zib::wait_details::deconstruct_noop<ELEMENT>, 4096*128, 16> q2(2);
//queue<ELEMENT> q{1000};
//rigtorp::MPMCQueue<ELEMENT> q{1000};
void* yieldPtr = (void*)-2;
std::thread thread1;
//std::atomic<int> doing{0};
std::atomic<bool> yielding{false};
std::atomic<uint64_t> total_size{0};
uint64_t ret{0};
double yieldsCount{0};
double start1 = 0.0;
constexpr uint64_t T = 2;
constexpr uint64_t CALLS = 50000;
constexpr uint64_t YIELDS = 100;
constexpr uint64_t CALLS_PER = CALLS / T;
constexpr uint64_t YIELDS_PER = YIELDS / T;
constexpr uint64_t YIELD_FREQ = CALLS_PER / YIELDS_PER;
thread_local int t_id = 0;
std::atomic<int> t_idinc{0};
KEEP_IN_MODULE void loop_atomic() {
	while(1) {
        #if PROFILING
        if (start1 == 0.0) start1 = EM_ASM_DOUBLE(return performance.now());
        #endif
        while(1) {
		ELEMENT evt;
        auto v = yielding.load();
        if (v) {
        #if PROFILING
            yieldsCount++;
        #endif
            yielding = false;
            EM_ASM(Module.channel.port2.postMessage(""););
            return;
        }
        evt = q.pop();
        //evt = q2.dequeue(); total_size--;
        //emscripten_thread_sleep(1);
        {
            #if USING_FUNCTION
            auto ptr = evt(1);
            #else
            auto f = (void*(*)())evt;
            auto ptr = f();
            #endif
            #if PROFILING
            if (ret++ == CALLS) {
                //auto end = EM_ASM_DOUBLE(return performance.now()) - start1;
                start1 = EM_ASM_DOUBLE({console.log('time:', performance.now() - $0, $1); return performance.now()}, start1, yieldsCount);
                //printf("sync  void (%" PRIu64 ") : time %f %f\n", r, end, start1);
                //start1 = EM_ASM_DOUBLE(return performance.now());
                ret = 0;
                yieldsCount = 0;
            }
            #endif
		}
        //printf("size of queue %d\n", eventQueue1.size_approx());
	    }
	}
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
    yielding.store(true);
}