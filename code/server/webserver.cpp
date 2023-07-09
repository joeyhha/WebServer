#include "webserver.h"

using namespace std;

// 初始化webserver对象
WebServer::WebServer(
    // SkipList  Heap  RB
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);         // 获取当前的工作路径
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);    // 得到资源根路径
    HttpConn::userCount = 0;                // 初始化用户数量（每一个连接进来的客户端被封装成一个http连接对象）
    HttpConn::srcDir = srcDir_;             // 初始化资源路径
    // 数据库连接池的初始化 视频暂时没讲
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 初始化事件模式
    InitEventMode_(trigMode);
    // 如果初始化成功，继续执行，否则关闭服务器，此时监听的fd已经加到epoller上
    if(!InitSocket_()) { isClose_ = true;}

    if(openLog) {
        // 初始化实例
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        // 如果server关闭
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        // 
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

/**********************************
    功能：析构
        关闭监听描述符
        设置webserver状态为关闭
        释放资源路径
        关闭数据库连接池
**********************************/
WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 设置监听和通信的文件描述符的模式
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;  // 监听事件可以检测到对方正常关闭
    // oneshot：避免一个事件被多个线程处理  rdhup检测文件描述符异常断开
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; 
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET; // 连接事件 一次事件只触发一次
        break;
    case 2:
        listenEvent_ |= EPOLLET; // 监听事件 一次事件只触发一次
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET; // 俩都设置成ET
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    // 判断是否是ET模式
    HttpConn::isET = (connEvent_ & EPOLLET);
}

/*
    功能：开启服务（主线程）
        调用epoll检测是否有事件到达
*/
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    // server没有关闭，打印日志
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    // 循环处理事件
    while(!isClose_) {
        if(timeoutMS_ > 0) { // timeoutMS_：60000
            // 得到下一次清除过期节点的时间
            timeMS = timer_->GetNextTick();
        }
        // 阻塞timeMS，得到有多少个触发事件
        int eventCnt = epoller_->Wait(timeMS);
       // 有多少个事件就循环多少次
        for(int i = 0; i < eventCnt; i++) {
            // 得到事件Fd
            int fd = epoller_->GetEventFd(i);
            // 得到事件
            uint32_t events = epoller_->GetEvents(i);
            // 是监听fd
            if(fd == listenFd_) {
                DealListen_(); // 处理监听操作，接受客户端连接
            }
            // 出现错误
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]); // 关闭连接
            }
            // 可读，默认可读，服务端读取客户端请求
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]); // 处理读操作
            }
            // 可写，服务端向客户端写响应
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); // 处理写操作
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

/************************************
    功能：向客户端发送错误
    参数：
        fd      接收错误信息的客户端fd
        info    错误信息
    调用：处理监听事件，发送客户端正忙
*************************************/
void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

/**************************************************
    功能：关闭连接，从epoll中移除fd
    参数：
        client  用户对象
    调用：
        主线程遇到EPOLLRDHUP | EPOLLHUP | EPOLLERR
        计时器回调函数
****************************************************/
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    // 打印日志
    LOG_INFO("Client[%d] quit!", client->GetFd());
    // 删除fd
    epoller_->DelFd(client->GetFd());
    // 关闭客户端
    client->Close();
}

/*  添加用户  */
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    // 断言 fd > 0 为假，则报错
    assert(fd > 0); 
    // 初始化http连接信息，users_[fd]是一个httpConn类型
    users_[fd].init(fd, addr);
    // 添加定时器，回调函数是关闭连接
    if(timeoutMS_ > 0) {
        // cb是用bind来绑定CloseConn_和它要的参数，但是CloseConn_是一个成员函数，所以要指定哪个类来调用它
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    // 添加文件描述符
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    // 设置非阻塞
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

/**************************
    功能：监听fd处理事件连接
**************************/
void WebServer::DealListen_() {
    struct sockaddr_in addr; // 保存连接的客户端信息
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        // 没有客户端的情况下， accept返回-1
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        // 添加用户
        AddClient_(fd, addr);
    // ET模式下accept一次没取完，系统不通知，所以需要循环取，否则有可能无法一次性连接上所有客户端
    } while(listenEvent_ & EPOLLET); 
}

/********************
    功能：处理读事件
    调用：主线程
*********************/
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    //延长客户端超时时间
    ExtentTime_(client);
    // 向线程池追加一个任务
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

/*************************************
    功能：处理写事件
    参数：
        clinet  连接的客户端对象
    输出：无
**************************************/
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    // 延长超时时间
    ExtentTime_(client);
    // 将写任务交给线程池
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}
/*************************
    功能：延长超时时间
    调用：客户端发生读写事件
**************************/
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

/*
    功能：处理读事件
    调用：作为线程池添加任务的回调函数
    是在子线程里执行（reactor模型）
*/
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno); // 读取客户端数据，数据保存在client的读缓冲区中
    // 如果没有读到数据，关闭连接
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    // 读到数据就进行处理业务逻辑（解析http请求）
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn* client) {
    // 如果业务逻辑处理成功（请求响应完成）
    if(client->process()) {
        // 修改客户端fd为监听可写
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else { // 否则继续监听读事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

// 向TCP写缓冲区写数据
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* 
    功能：初始化socket
    调用：server对象
 */
// 套接字通信的基本流程
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    // 规定端口号范围
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    // 协议族
    addr.sin_family = AF_INET;
    // 绑定IP地址，INADDR_ANY指绑定能绑定的任意地址
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 绑定
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 监听
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    // 判断是否有用户连接
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    // 设置非阻塞
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);

    // int flag = fcntl(fd, F_GETFD, 0);
    // flag = flag | O_NONBLOCK;
    // fcntl(fd, F_SETFL, flag);

    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


