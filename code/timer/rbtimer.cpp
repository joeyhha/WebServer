#include "rbtimer.h"

void RBTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    //计算定时器过期时间
    TimeStamp expires = Clock::now() + MS(timeout);    
    // 将定时器插入红黑树中，以过期时间为键
    timerMap_.insert({expires, {id, expires, cb}});
}

void RBTimer::adjust(int id, int newExpires) {
    // // 查找指定ID的定时器
    auto iter = std::find_if(timerMap_.begin(), timerMap_.end(),
                             [id](const std::pair<TimeStamp, TimerNode>& pair) {
                                 return pair.second.id == id;
                             });
    if (iter != timerMap_.end()) {
        // 计算新的过期时间
        TimeStamp expires = Clock::now() + MS(newExpires);
        TimerNode node = iter->second;
        // 更新定时器的过期时间
        node.expires = expires;
        // 从红黑树中删除旧的定时器节点
        timerMap_.erase(iter);
        // 将更新后的定时器插入红黑树中
        timerMap_.insert({expires, node});
    }
}

void RBTimer::doWork(int id) {
    // 查找指定ID的定时器
    auto iter = std::find_if(timerMap_.begin(), timerMap_.end(),
                             [id](const std::pair<TimeStamp, TimerNode>& pair) {
                                 return pair.second.id == id;
                             });
    if (iter != timerMap_.end()) {
        TimerNode node = iter->second;
        // 执行定时器的回调函数
        node.cb();
        // 从红黑树中删除该定时器节点
        timerMap_.erase(iter);
    }
}

void RBTimer::clear() {
    // 清空红黑树中的所有定时器节点
    timerMap_.clear();
}

void RBTimer::tick() {
    TimeStamp now = Clock::now();
    while (!timerMap_.empty()) {
        // 获取红黑树中最早的定时器节点
        auto iter = timerMap_.begin();
        // 若定时器已过期
        if (iter->first <= now) {
            TimerNode node = iter->second;
            node.cb();
            // 从红黑树中删除该定时器节点
            timerMap_.erase(iter);
        // 若当前定时器未过期，则停止遍历
        } else {
            break;
        }
    }


}

int RBTimer::GetNextTick() {
    tick();

    int res = -1;

    if (!timerMap_.empty()) {
        TimeStamp nextExpires = timerMap_.begin()->first;
        TimeStamp now = Clock::now();
        res = std::chrono::duration_cast<MS>(nextExpires - now).count();
        if (res < 0) {
            res = 0;
        }
    }

    return res;
}