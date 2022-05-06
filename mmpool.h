//
// Created by zyz on 2022/5/6.
//

#ifndef MMPOOL_MMPOOL_H
#define MMPOOL_MMPOOL_H


#include<cstring>
#include<cstdlib>

#ifndef nginx_memory_pool
#define nginx_memory_pool


using u_char = unsigned char;
using ngx_uint_t = unsigned int;
struct ngx_pool_t;

//  小内存池（Block）头信息
typedef struct {
    u_char* last;                           //  可用内存起始
    u_char* end;                            //  内存末尾
    ngx_pool_t* next;                       //  小内存Block链表
    ngx_uint_t            failed;           //  分配内存是否成功
} ngx_pool_data_t;

//  大内存池（Block）头信息
struct ngx_pool_large_t {
    ngx_pool_large_t* next;     //   大内存Block的头信息链表
    void* alloc;                //   指向申请的大内存Block
};

typedef void(*ngx_pool_cleanup_pt)(void* data);     //  回调函数；负责清理外部资源
//  外部资源的头信息
struct ngx_pool_cleanup_t {
    ngx_pool_cleanup_pt   handler;      //  处理外部资源的回调函数
    void* data;                         //  传给handler的参数
    ngx_pool_cleanup_t* next;           //  外部资源头信息链表。
};


//  管理整个内存池的头信息
struct ngx_pool_t {
    ngx_pool_data_t       d;                //  小内存Block的头信息
    size_t                max;              //  大块内存和小块内存的分界线。p->max：一个小块Block块内最多能分配多大的内存。其大小受制于程序本身ngx_memalign开辟的大小，也受制于小内存定义的上限4095
    ngx_pool_t* current;                    //  指向第一块提供小块内存分配的小块内存Block地址
    ngx_pool_large_t* large;                //  大内存Block头信息的链表入口
    ngx_pool_cleanup_t* cleanup;            //  外部资源的头信息链表
};

//  32位 4   64位 8
//  小块内存考虑字节对齐时的单位
const int NGX_ALIGNMENT = sizeof(unsigned long);    /* platform word */

//  默认一个页面大小：4KB
const int ngx_pagesize = 4096;  //  1024B = 1KB
//  ngx小块内存block里可分配的最大空间。（也即不能超过一个页）
const int NGX_MAX_ALLOC_FROM_POOL = ngx_pagesize - 1;
//  默认创建的内存池大小为16K
const int NGX_DEFAULT_POOL_SIZE = 16 * 1024;
//  对齐为16的整数倍
const int NGX_POOL_ALIGNMENT = 16;

//  将d上调至a的倍数
#define ngx_align(d,a) ( ((d)+(a-1)) & ~(a-1) )
//  把指针p调整到a的临近倍数
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)
#define ngx_memset(buf, c, n)     (void) memset(buf, c, n)

//  ngx小块内存池最小的size调整成 NGX_POOL_ALIGNMENT 的倍数
//  保证至少能有一个ngx_pool_t头信息的大小 如 sizeof(ngx_pool_t)【15】 + 2*8 = 31    调整至32
const int NGX_MIN_POOL_SIZE = ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)), NGX_POOL_ALIGNMENT);



/*
* OOP 移植nginx内存池
*/
class ngx_mem_pool
{
public:
    //  构造函数，创建内存池。
    ngx_mem_pool(size_t size = NGX_DEFAULT_POOL_SIZE);
    //  析构函数，释放内存池
    ~ngx_mem_pool();
    //  考虑字节对齐，从内存池申请size大小的内存
    void* ngx_palloc(size_t size);
    //  不考虑字节对齐，从内存池申请size大小的内存
    void* ngx_pnalloc(size_t size);
    //  调用ngx_palloc，并初始化为0.
    void* ngx_pcalloc(size_t size);
    //  释放大块内存block。ngx不提供释放小块内存的接口。原因见博客
    void ngx_pfree(void* p);
    //  内存重置函数
    void ngx_reset_pool();
    //  添加回调清理操作函数
    ngx_pool_cleanup_t* ngx_pool_cleanup_add(size_t size);
private:
    //  指向nginx内存池的入口指针，一个内存池只有一个pool。即第一个创建的内存block里的ngx_pool_t。pool_的指向不会改变，始终是第一个，因为只有第一个有。会发生改变的是它指向的current，next之类的东西。
    ngx_pool_t* pool_;
    //  尝试从内存池中拿出size大小内存。内存池不够则从操作系统开辟。align=1意味着需要内存对齐
    void* ngx_palloc_small(size_t size, ngx_uint_t align);
    //  从操作系统开辟新的小块内存池。ngx_palloc_small调用ngx_palloc_block。ngx_palloc_block底层调用ngx_memalign。在unix平台下ngx_memalign就是ngx_alloc
    void* ngx_palloc_block(size_t size);
    //  从操作系统开辟大块内存，挂载到某个已有的大块头信息下。（或再从内存池申请一块用作大内存block头信息）
    void* ngx_palloc_large(size_t size);
    //  销毁内存池
    void ngx_destroy_pool();
    //  创建size大小的内存池  （每个小内存block的大小均为size）
    ngx_pool_t* ngx_create_pool(size_t size);
//    void* ngx_alloc(size_t size);  //  从操统malloc大块内存。ngx_palloc_large调用ngx_alloc。ngx_alloc调用malloc
};



#endif


#endif //MMPOOL_MMPOOL_H
