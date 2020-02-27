## I/O Multiplex
### Reference
+ [kqueue & epoll](https://www.cnblogs.com/FG123/p/5256553.html)
+ [什么是kqueue和IO复用](https://www.cnblogs.com/luminocean/p/5631336.html)

### 阻塞 & 内核缓冲区
+ 缓冲区满，缓冲区空，缓冲区非空，缓冲区非满, 进行阻塞同步的根本

### kqueue
+ 注册一批socket描述符到 kqueue 以后，当其中的描述符状态发生变化时，kqueue 将一次性通知应用程序哪些描述符可读、可写或出错了

#### 相关API
+ kqueue() 生成一个内核事件队列，返回该队列的文件描述符。其它 API 通过该描述符操作这个 kqueue。
+ kevent() 提供向内核注册 / 反注册事件和返回就绪事件或错误事件。
+ struct kevent 就是kevent()操作的最基本的事件结构。
```c
struct kevent {
    uintptr_t       ident;          /* identifier for this event，比如该事件关联的文件描述符 */
    int16_t         filter;         /* filter for event，可以指定监听类型，如EVFILT_READ，EVFILT_WRITE，EVFILT_TIMER等 */
    uint16_t        flags;          /* general flags ，可以指定事件操作类型，比如EV_ADD，EV_ENABLE， EV_DELETE等 */
    uint32_t        fflags;         /* filter-specific flags */
    intptr_t        data;           /* filter-specific data */
    void *          udata;         /* opaque user data identifier，可以携带的任意数据 */
};
```
    + 在kqueue上, {ident, filter} 确定一个唯一的事件
    + ident: 事件id, 一般设置为文件描述符
    + filter: 可以将这个字段看成事件; 内核检测ident上注册的filter的状态, 状态发生了变化, 就发生了变化;
    + 与socket读写相关的filter:
      + EVFILT_READ: 
        + TCP监听socket: 如果在`完成的连接队列`中有数据，此事件将被通知。收到该通知的应用一般调用`accept()`，且可通过data获得`完成队列的节点个数`。 
        + 流或数据报socket: 当协议栈的 socket 层`接收缓冲区有数据`时，该事件会被通知，并且 data 被设置成`可读数据的字节数`.
      + EVFILT_WRIT: 当socket层的`写入缓冲区可写入时`，该事件将被通知；data 指示目前`缓冲区有多少字节空闲空间`
    + flags: 行为标志
      + EV_ADD: 指示加入事件到 kqueue
      + EV_DELETE: 指示将传入的事件从 kqueue 中移除
    + fflags: 过滤器标识值
      + EV_ENABLE: 过滤器事件可用，注册一个事件时，默认是可用的
      + EV_DISABLE: 过滤器事件不可用，当内部描述可读或可写时，将不通知应用程序 
    + EV_SET(&kev, ident, filter, flags, fflags, data, udata)
        - 用于初始化kevent结构的便利宏
    ```c
    kevent(int kq, 
        const struct kevent *changelist, // 监视列表
        int nchanges, // 长度
        struct kevent *eventlist, // kevent函数用于返回已经就绪的事件列表
        int nevents, // 长度
        const struct timespec *timeout); // 超时限制
    ```
   