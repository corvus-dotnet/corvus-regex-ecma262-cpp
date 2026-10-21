// A thread with a stack of a given size, which std::thread cannot give: an engine that recurses as deep as a pattern
// nests needs more than a platform's default.
#ifndef CRE_CPP_DATASET_THREADS_HPP
#define CRE_CPP_DATASET_THREADS_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

namespace dataset {

class thread {
  public:
    thread(std::function<void()> work, std::size_t stack) : work_(std::make_unique<std::function<void()>>(std::move(work))) {
#ifdef _WIN32
        handle_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, static_cast<unsigned>(stack), run, work_.get(),
                                                           STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
        if (!handle_) throw std::runtime_error("cannot start a thread");
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack);
        int rc = pthread_create(&handle_, &attr, run, work_.get());
        pthread_attr_destroy(&attr);
        if (rc != 0) throw std::runtime_error("cannot start a thread");
#endif
    }

    thread(thread&&) = default;

    void join() {
#ifdef _WIN32
        WaitForSingleObject(handle_, INFINITE);
        CloseHandle(handle_);
#else
        pthread_join(handle_, nullptr);
#endif
    }

  private:
#ifdef _WIN32
    static unsigned __stdcall run(void* p) {
        (*static_cast<std::function<void()>*>(p))();
        return 0;
    }
    HANDLE handle_ = nullptr;
#else
    static void* run(void* p) {
        (*static_cast<std::function<void()>*>(p))();
        return nullptr;
    }
    pthread_t handle_{};
#endif
    std::unique_ptr<std::function<void()>> work_;
};

}  // namespace dataset

#endif
