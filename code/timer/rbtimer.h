#ifndef RB_TIMER_H
#define RB_TIMER_H

#include <algorithm>
#include <map>
#include <functional>
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& t) const {
        return expires < t.expires;
    }
};

class RBTimer {
public:
    RBTimer() = default;
    ~RBTimer() { clear(); }

    void add(int id, int timeout, const TimeoutCallBack& cb);

    void adjust(int id, int newExpires);

    void doWork(int id);

    void clear();

    void tick();
    
    int GetNextTick();

private:
    std::map<TimeStamp, TimerNode> timerMap_;
};

#endif // RB_TIMER_H
