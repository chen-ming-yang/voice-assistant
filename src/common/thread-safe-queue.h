// thread-safe-queue.h — Lock-based bounded queue for inter-thread communication
#ifndef VOICE_ASSISTANT_THREAD_SAFE_QUEUE_H_
#define VOICE_ASSISTANT_THREAD_SAFE_QUEUE_H_

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class ThreadSafeQueue {
 public:
  void Push(T item) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      q_.push(std::move(item));
    }
    cv_.notify_one();
  }

  // Blocks until an item is available or the queue is shut down.
  // Returns false if shut down and queue is empty.
  bool Pop(T &item) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return !q_.empty() || done_; });
    if (q_.empty()) return false;
    item = std::move(q_.front());
    q_.pop();
    return true;
  }

  // Signal that no more items will be pushed. Unblocks waiting Pop().
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      done_ = true;
    }
    cv_.notify_all();
  }

 private:
  std::queue<T> q_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
};

#endif  // VOICE_ASSISTANT_THREAD_SAFE_QUEUE_H_
