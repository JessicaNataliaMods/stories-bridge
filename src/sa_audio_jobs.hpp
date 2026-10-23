#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace lcs {
// Opening, retuning and closing WinMM wave devices costs milliseconds per call. The SA audio hooks run on the
// game thread, so they only decide what should sound and hand the device work to this single worker thread.
// The worker also runs `tick` every `period` (used to retune looping voices without touching the game thread).
class AudioWorker {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    std::function<void()> tick_;
    std::chrono::milliseconds period_;
    bool stop_=false;
    std::thread thread_;
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while(!stop_) {
            wake_.wait_for(lock,period_,[&]{return stop_ || !jobs_.empty();});
            if(stop_)break;
            std::deque<std::function<void()>> batch;
            batch.swap(jobs_);
            lock.unlock();
            for(auto& job:batch) {try {job();} catch(...) {}}
            if(tick_) {try {tick_();} catch(...) {}}
            lock.lock();
        }
    }
public:
    AudioWorker(std::function<void()> tick,std::chrono::milliseconds period):tick_(std::move(tick)),period_(period),thread_([this]{run();}) {}
    ~AudioWorker() {stop();}
    void post(std::function<void()> job) {
        {std::lock_guard<std::mutex> lock(mutex_);if(stop_)return;jobs_.push_back(std::move(job));}
        wake_.notify_one();
    }
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);if(stop_)return;stop_=true;}
        wake_.notify_all();
        if(thread_.joinable())thread_.join();
    }
};
}
