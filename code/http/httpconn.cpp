#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir; // 资源目录
std::atomic<int> HttpConn::userCount; // atomic：设置userCount为原子变量，保证执行操作时不会被其他线程干扰
bool HttpConn::isET;

HttpConn::HttpConn() { 
    fd_ = -1;
    addr_ = { 0 };
    isClose_ = true;
};

HttpConn::~HttpConn() { 
    Close(); 
};

// 初始化http连接
void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;
    addr_ = addr;
    fd_ = fd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

// 关闭http连接
void HttpConn::Close() {
    // 内存释放
    response_.UnmapFile();
    if(isClose_ == false){
        isClose_ = true; 
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const {
    return fd_;
};

struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}

int HttpConn::GetPort() const {
    return addr_.sin_port;
}

ssize_t HttpConn::read(int* saveErrno) { 
    ssize_t len = -1;
    // 是ET模式，则循环将内容读出
    do {
        len = readBuff_.ReadFd(fd_, saveErrno); // 将数据读到readbuff封装的缓冲区
        if (len <= 0) {
            break;
        }
    } while (isET); // 是ET模式则循环读取，否则只读一次
    return len; // 返回读到的字节数，如果是ET模式并且循环读了，最后返回的不是完整的字节数
}

ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 分散写
        len = writev(fd_, iov_, iovCnt_);
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; } /* 传输结束 */
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            writeBuff_.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240);// 若是ET模式 或者要写入的字节数大于一次分散写最大字节，循环
    return len;
}
// 一个连接对应一对请求和响应
bool HttpConn::process() {
    // 初始化请求
    request_.Init(); 
    // 若可读字节数小于等于0，返回失败
    if(readBuff_.ReadableBytes() <= 0) {
        return false;
    }
    // 若解析数据成功
    else if(request_.parse(readBuff_)) {
        // 记录日志
        LOG_DEBUG("%s", request_.path().c_str());
        // 初始化响应，200代表正常响应
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    // 否则初始化错误响应，400 Bad Request
    } else {
        response_.Init(srcDir, request_.path(), false, 400);
    }

    response_.MakeResponse(writeBuff_);
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    /* 文件 */
    if(response_.FileLen() > 0  && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;
}
