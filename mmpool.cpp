//
// Created by zyz on 2022/5/6.
//

#include "mmpool.h"
#include <new>
#include<iostream>
// 创建size大小的内存池  （每个小内存block的大小均为size
ngx_pool_t* ngx_mem_pool::ngx_create_pool(size_t size)
{
    size = std::max((size_t)NGX_MIN_POOL_SIZE, size);
    pool_ = static_cast<ngx_pool_t*>(malloc(size));
    if (pool_ == nullptr) {
        return nullptr;
    }

    pool_->d.last = (u_char*)pool_ + sizeof(ngx_pool_t);
    pool_->d.end = (u_char*)pool_ + size;
    pool_->d.next = nullptr;
    pool_->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    pool_->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    pool_->current = pool_;
    pool_->large = nullptr;
    pool_->cleanup = nullptr;
    return pool_;
}


//  考虑字节对齐，从内存池申请size大小的内存
void* ngx_mem_pool::ngx_palloc(size_t size)
{
    if (size <= pool_->max) {
        return ngx_palloc_small(size, 1);
    }
    return ngx_palloc_large(size);
}


//  尝试从内存池中拿出size大小内存。内存池不够则从操作系统开辟。align=1意味着需要内存对齐
// 分配效率高，只做了指针的偏移
void* ngx_mem_pool::ngx_palloc_small(size_t size, ngx_uint_t align)
{
    u_char* m;
    ngx_pool_t* p;
    // 从第一个内存块的current指针指向的内存池进行分配
    p = pool_->current;
    do {
        m = p->d.last; // m指向可分配内存的起始地址
        if (align) {
            // 把m调整为NGX_ALIGNMENT整数倍
            m = static_cast<u_char*>(ngx_align_ptr(m, NGX_ALIGNMENT));  //  ??
        }
        if ((size_t)(p->d.end - m) >= size) {
            // 若可分配空间 >= 申请的空间
            // 偏移d.last指针，记录空闲空间的首地址
            p->d.last = m + size;
            return m;
        }
        // 当前内存块的空闲空间不够分配，若有下一个内存块则转向下一个内存块
        // 若没有，p会被置空，退出while
        p = p->d.next;
    } while (p);

    return ngx_palloc_block(size);
}


//  从操作系统开辟新的小块内存池。ngx_palloc_small调用ngx_palloc_block。ngx_palloc_block底层调用ngx_memalign。在unix平台下ngx_memalign就是ngx_alloc
void* ngx_mem_pool :: ngx_palloc_block(size_t size)
{
    u_char* m;
    size_t       psize;
    ngx_pool_t* p, * new_m;

    psize = (size_t)(pool_->d.end - (u_char*)pool_);

    m = static_cast<u_char*>(malloc(psize));
    if (m == nullptr) {
        return nullptr;
    }

    new_m = (ngx_pool_t*)m;  // 指向新开辟内存块的起始地址

    new_m->d.end = m + psize; // 指向新开辟内存块的末尾地址
    new_m->d.next = NULL;     // 下一块内存的地址为NULL
    new_m->d.failed = 0;   // 当前内存块分配空间失败的次数
// 指向头信息的尾部，而max，current、chain等只在第一个内存块有
    m += sizeof(ngx_pool_data_t);
    m = static_cast<u_char*>(ngx_align_ptr(m, NGX_ALIGNMENT));
    new_m->d.last = m + size;    // last指向当前块空闲空间的起始地址
// 由于每次都是从pool->current开始分配空间
    // 若执行到这里，除了new这个内存块分配成功，其他的内存块全部分配失败
    for (p = pool_->current; p->d.next; p = p->d.next) {
        // 对所有的内存块的failed都++，直到该内存块分配失败的次数大于4了
        // 就表示该内存块的剩余空间很小了，不能再分配空间了
        // 就修改current指针，下次从current开始分配空间，再次分配的时候可以不用遍历前面的内存块
        // 前面的内存块的failed大小肯定大于等于后面的failed大小
        if (p->d.failed++ > 4) {
            pool_->current = p->d.next;
        }
    }
    // 连接可分配空间的首个内存块 和 新开辟的内存块
    p->d.next = new_m;

    return m;
}


//  从操作系统开辟大块内存，挂载到某个已有的大块头信息下。（或再从内存池申请一块用作大内存block头信息）
void* ngx_mem_pool::ngx_palloc_large(size_t size)
{
    void* p;
    ngx_uint_t         n;
    ngx_pool_large_t* large;

    p = malloc(size);
    if (p == nullptr) {
        return nullptr;
    }

    n = 0;
// 循环遍历存储大块内存信息的链表
    for (large = pool_->large; large; large = large->next) {
        // 当大块内存被ngx_pfree时，alloc为NULL
        // 遍历链表，若大块内存的首地址为空，则把当前malloc的内存地址写入alloc
        if (large->alloc == nullptr) {
            large->alloc = p;
            return p;
        }
// 遍历4次后，若还没有找到被释放过的大块内存对应的信息
// 为了提高效率，直接在小块内存中申请空间保存大块内存的信息
        if (n++ > 3) {
            break;
        }
    }
// 通过指针偏移在小块内存池上分配存放大块内存*next和*alloc的空间
    large = static_cast<ngx_pool_large_t*>(ngx_palloc_small(sizeof(ngx_pool_large_t), 1));
    if (large == nullptr) {
        // 如果在小块内存上分配存储*next和*alloc空间失败，则无法记录大块内存
        // 释放大块内存p
        free(p);
        return nullptr;
    }

    large->alloc = p;   // alloc指向大块内存的首地址
    large->next = pool_->large;  // 这两句采用头插法，将新内存块的记录信息存放于以large为头结点的链表中
    pool_->large = large;

    return p;
}






// 释放p指向的大块内存
void ngx_mem_pool::ngx_pfree(void* p)
{
    ngx_pool_large_t* l;
    for (l = pool_->large; l; l = l->next) {
        // 遍历存储大块内存信息的链表，找到p对应的大块内存
        if (p == l->alloc) {
            // 释放大块内存，但不释放存储信息的内存空间
            free(l->alloc);
            l->alloc = nullptr;
            return ;
        }
    }
}



void* ngx_mem_pool::ngx_pnalloc(size_t size)
{
    if (size <= pool_->max) {
        return ngx_palloc_small(size, 0);// // 不考虑内存对齐
    }
    return ngx_palloc_large(size);
}

void* ngx_mem_pool :: ngx_pcalloc(size_t size)
{
    void *p = ngx_palloc(size);// 考虑内存对齐
    if (p) {    // 可以初始化内存为0
        ngx_memzero(p, size);
    }
    return p;
}


/*
就用了last和end两个指着标识空闲的空间，是无法将已经使用的空间合理归还到内存池的，
只是会重置内存池。同时还存储了指向大内存块large和清理函数cleanup的头信息

考虑到nginx的效率，小块内存分配高效，同时也不回收内存
*/
/*
nginx本质是http服务器，通常处理的是短链接，间接性提供服务，需要的内存不大，所以不回收内存，重置即可。

客户端发起一个requests请求后，nginx服务器收到请求会返回response响应，若在keep-alive时间内没有收到客户的再次请求，

nginx服务器会主动断开连接，此时会reset内存池。下一次客户端请求再到来时，可以复用内存池。

如果是处理长链接，只要客户端还在线，服务器的资源就无法释放，直到系统资源耗尽。
长链接一般使用SGI STL内存池的方式进行内存的开辟和释放，而这种方式分配和回收空间的效率就比nginx低
*/
void ngx_mem_pool::ngx_reset_pool()
{
// 遍历cleanup链表（存放的时释放前需要调用的函数），可释放外部占用的资源
    for (ngx_pool_cleanup_t* c = pool_->cleanup; c; c = c->next)
    {
        if (c->handler&&c->data) {
            c->handler(c->data);
            c->handler = nullptr;
            c->data = nullptr;
        }
    }

    // 由于需要重置小块内存，而大块内存的控制信息在小块内存中保存
    // 所以需要先释放大块内存，在重置小块内存
    for (ngx_pool_large_t* l = pool_->large; l; l = l->next) {
        if (l->alloc) {
            free(l->alloc);
        }
    }

    // 遍历小块内存的链表，重置last、failed、current、chain、large等管理信息
    for (ngx_pool_t* p = pool_; p; p = p->d.next)
    {
        // 由于只有第一个内存块有除了ngx_pool_data_t以外的管理信息，别的内存块只有ngx_pool_data_t的信息
        // 不会出错，但是会浪费空间
        p->d.last = (u_char*)p + sizeof(ngx_pool_data_t);
        p->d.failed = 0;
    }


    pool_->current = pool_;
    pool_->large = nullptr;
}



void ngx_mem_pool::ngx_destroy_pool()
{
    ngx_pool_t* p, * n;
    ngx_pool_large_t* l;
    ngx_pool_cleanup_t* c;


    for (c = pool_->cleanup; c; c = c->next) {
        if (c->handler) {
            c->handler(c->data);
        }
    }


    for (l = pool_->large; l; l = l->next) {
        if (l->alloc) {
            free(l->alloc);
        }
    }


    for (p = pool_, n = pool_->d.next; /* void */; p = n, n = n->d.next) {
        free(p);
        if (n == nullptr) {
            break;
        }
    }
}
//  添加回调清理操作函数
// size表示p->cleanup->data指针的大小
// pool_->cleanup指向含有清理函数信息的结构体
// ngx_pool_cleanup_add返回 含有清理函数信息的结构体 的指针
ngx_pool_cleanup_t* ngx_mem_pool::ngx_pool_cleanup_add(size_t size)
{
    ngx_pool_cleanup_t* c;
// 开辟清理函数的结构体，实际上也是存放在内存池的小块内存
    c = static_cast<ngx_pool_cleanup_t*>(ngx_palloc(sizeof(ngx_pool_cleanup_t)));
    if (c == nullptr) {
        return nullptr;
    }

    if (size) {
        // 为c->data申请size的空间
        c->data = ngx_palloc(size);
        if (c->data == nullptr) {
            return nullptr;
        }
    }
    else {
        c->data = nullptr;
    }
    // 采用头插法，将当前结构体串在pool->cleanup后
    c->handler = nullptr;
    c->next = pool_->cleanup;

    pool_->cleanup = c;

    return c;
}


ngx_mem_pool::ngx_mem_pool(size_t size)
{
    pool_ = ngx_create_pool(size);
    if (pool_ == nullptr) {
        throw std::bad_alloc();
    }
}

ngx_mem_pool::~ngx_mem_pool()
{
    std::cout << "~ngx_mem_pool" << std::endl;
    ngx_destroy_pool();
}