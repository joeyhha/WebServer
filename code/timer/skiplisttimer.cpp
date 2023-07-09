#include "skiplisttimer.h"
#include <random>

int SkipListTimer::randomLevel() {
    int level = 1;
    static default_random_engine generator(time(0));
    static uniform_real_distribution<double> distribution(0.0, 1.0);
    while (distribution(generator) < 0.5 && level < MAX_LEVEL) {
        level++;
    }
    return level;
}

SkipListNode* SkipListTimer::createNode(int id, TimeStamp expires, int level) {
    return new SkipListNode(id, expires, level);
}

void SkipListTimer::deleteNode(SkipListNode* node) {
    if (node) {
        delete node;
    }
}

SkipListNode* SkipListTimer::findPrevNode(int id) {
    SkipListNode* p = head_;
    for (int i = MAX_LEVEL - 1; i >= 0; i--) {
        while (p->next[i] && p->next[i]->id < id) {
            p = p->next[i];
        }
    }
    return p;
}

/***********************************
    节点延时
    将指定节点的延时时间延长为timeout
    向下修复跳表性质
************************************/
void SkipListTimer::adjust(int id, int timeout) {
    SkipListNode* prev = findPrevNode(id);
    // 找到指定节点的前一个节点
    if (prev->next[0] && prev->next[0]->id == id) {
        SkipListNode* cur = prev->next[0];
        cur->expires = Clock::now() + MS(timeout);
        // 向下修复跳表性质
        

    }
}

void SkipListTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    TimeStamp expires = Clock::now() + MS(timeout);
    int level = randomLevel();
    SkipListNode* newNode = createNode(id, expires, level);

    for (int i = 0; i < level; i++) {
        newNode->next[i] = head_->next[i];
        head_->next[i] = newNode;
    }

    nodes_[id] = newNode;
}

void SkipListTimer::doWork(int id) {
    SkipListNode* prev = findPrevNode(id);
    if (prev->next[0] && prev->next[0]->id == id) {
        SkipListNode* node = prev->next[0];
        node->cb();
        deleteNode(node);
        prev->next[0] = node->next[0];
        nodes_.erase(id);
    }
}

void SkipListTimer::clear() {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        deleteNode(it->second);
    }
    nodes_.clear();
}

void SkipListTimer::tick() {
    SkipListNode* p = head_->next[0];
    TimeStamp now = Clock::now();
    while (p && p->expires <= now) {
        p->cb();
        SkipListNode* tmp = p;
        p = p->next[0];
        deleteNode(tmp);
        nodes_.erase(tmp->id);
    }
}

int SkipListTimer::GetNextTick() {
    tick();
    if (!head_->next[0]) {
        return -1;
    }
    TimeStamp now = Clock::now();
    int nextTick = chrono::duration_cast<MS>(head_->next[0]->expires - now).count();
    return (nextTick > 0) ? nextTick : 0;
}