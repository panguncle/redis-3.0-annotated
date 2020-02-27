/* Kqueue(2)-based ae.c module
 *
 * Copyright (C) 2009 Harish Mallipeddi - harish.mallipeddi@gmail.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

typedef struct aeApiState {
    int kqfd;
    struct kevent *events;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    state->events = zmalloc(sizeof(struct kevent) * eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }
    state->kqfd = kqueue();
    if (state->kqfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events, sizeof(struct kevent) * setsize);
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->kqfd);
    zfree(state->events);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent ke;

    if (mask & AE_READABLE) {
        // https://www.cnblogs.com/luminocean/p/5631336.html
        // https://www.jianshu.com/p/64aee4e7023c
        // https://www.jianshu.com/p/5cf67fab6d61
        /**
        struct kevent {
            uintptr_t       ident;          // identifier for this event，比如该事件关联的文件描述符
            int16_t         filter;         // filter for event，可以指定监听类型，如EVFILT_READ，EVFILT_WRITE，EVFILT_TIMER等
            uint16_t        flags;          // general flags ，可以指定事件操作类型，比如EV_ADD，EV_ENABLE， EV_DELETE等
            uint32_t        fflags;         // filter-specific flags
            intptr_t        data;           // filter-specific data
            void *          udata;         // opaque user data identifier，可以携带的任意数据
         };
         ident 是事件唯一的 key，在 socket 使用中，它是 socket 的 fd 句柄
         filter 是事件的类型，有 15 种，其中几种常用的是
            EVFILT_READ   socket 可读事件
            EVFILT_WRITE  socket 可写事件
            EVFILT_SIGNAL  unix 系统的各种信号
            EVFILT_TIMER  定时器事件
            EVFILT_USER  用户自定义的事件
          flags 操作方式，EV_ADD 添加，EV_DELETE 删除，EV_ENABLE 激活，EV_DISABLE 不激活
          fflags 第二种操作方式，NOTE_TRIGGER 立即激活等等
          data int 型的用户数据，socket 里面它是可读写的数据长度
          udata 指针类型的数据，你可以携带任何想携带的附加数据。比如对象

         EV_SET(&kev, ident, filter, flags, fflags, data, udata);

         // kqueue 可以监听 socket，文件变化，系统信号，定时器事件，用户自定义事件。
         int kevent(int kq,
            const struct kevent *changelist, // 监视列表
            int nchanges, // 长度
            struct kevent * eventlist, // kevent函数用于返回已经就绪的事件列表
            int nevents, // 长度
            const struct timespec * timeout); // 超时限制
         */
        // 对kevent类型变量, 为fd添加(EV_ADD), 读事件监听(EVFILT_READ),
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        // kevent() 是阻塞调用，等到有事件才返回。
        // Todo: kevent
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        // kevent 已经就绪的文件描述符数量
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent ke;

    if (mask & AE_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        // Todo: kevent
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
}

/**
 * 在指定的事件内, 阻塞并等待所有被aeCreateFileEvent函数设置为监听状态的套接字产生文件事件
 * 当至少有一个事件产生, 或者等待超时后, 返回
 * @param eventLoop
 * @param tvp
 * @return
 */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        // timespec和timeout是什么区别
        retval = kevent(state->kqfd, NULL, 0, state->events, eventLoop->setsize,
                        &timeout);
    } else {
        retval = kevent(state->kqfd, NULL, 0, state->events, eventLoop->setsize,
                        NULL);
    }

    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct kevent *e = state->events + j;

            // filter == 代表只有一个事件发生
            // 那么是不是不存在两个事件同时发生
            // Todo: 多路复用返回的是在什么时候返回呢, 有事件发生, 有多少事件发生呢?
            if (e->filter == EVFILT_READ) mask |= AE_READABLE;
            if (e->filter == EVFILT_WRITE) mask |= AE_WRITABLE;
            eventLoop->fired[j].fd = e->ident;
            eventLoop->fired[j].mask = mask;
        }
    }

    return numevents;
}

static char *aeApiName(void) {
    return "kqueue";
}
