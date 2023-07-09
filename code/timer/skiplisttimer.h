#ifndef SKIP_LIST_TIMER_H
#define SKIP_LIST_TIMER_H

#include <unordered_map>
#include <functional>
#include <chrono>
#include <random>
#include <cstring>
using namespace std;

typedef function<void()> TimeoutCallBack;
typedef chrono::high_resolution_clock Clock;
typedef chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

const int MAX_LEVEL = 8;

struct SkipListNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    SkipListNode** next;

    SkipListNode(int id, TimeStamp expires, int level)
        : id(id), expires(expires), next(new SkipListNode*[level]) {
        memset(next, 0, level * sizeof(SkipListNode*));
    }

    ~SkipListNode() {
        delete[] next;
    }
};

class SkipListTimer {
public:
    SkipListTimer() {
        head_ = new SkipListNode(-1, Clock::now(), MAX_LEVEL);
    }

    ~SkipListTimer() {
        clear();
        delete head_;
    }

    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    int GetNextTick();

private:
    int randomLevel();

    SkipListNode* createNode(int id, TimeStamp expires, int level);

    void deleteNode(SkipListNode* node);

    SkipListNode* findPrevNode(int id);

    SkipListNode* head_;
    unordered_map<int, SkipListNode*> nodes_;
};


#endif // SKIP_LIST_TIMER_H