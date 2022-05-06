
#include<cstdio>
#include<cstdlib>
#include"mmpool.h"

typedef struct Data stData;
struct Data
{
    //     ָ
    char* ptr;
    FILE* pfile;
    char* ptr2;
};


typedef struct yData yData;
struct yData
{
    char* ptr;
    int x;
    int y;
};



void self_handler(void* p)
{
    printf("self_handler\n");
    stData* q = (stData*)p;
    printf("free ptr mem!\n");
    free(q->ptr);
    printf("free ptr mem!\n");
    free(q->ptr2);
    printf("close file!\n");
    fclose(q->pfile);
}


void self_handler_02(void* p)
{
    char* q = (char*)p;
    printf("self_handler_02\n");
    printf("free ptr mem!\n");
    free(q);
}



int main()
{

    ngx_mem_pool mem_pool(512);

    stData* p1 = static_cast<stData*>(mem_pool.ngx_palloc(sizeof(stData)));
    if (p1 == nullptr)
    {
        printf("ngx_palloc %ld bytes fail\n", sizeof(stData));
        return -1;
    }

    p1->ptr = static_cast<char*>(malloc(12));
    strcpy(p1->ptr, "hello world");
    p1->pfile = fopen("data.txt", "w");
    p1->ptr2 = static_cast<char*>(malloc(20));
    strcpy(p1->ptr2, "goodbye world");

    ngx_pool_cleanup_t* c1 = mem_pool.ngx_pool_cleanup_add(sizeof(stData));
    c1->handler = self_handler;
    memcpy(c1->data, p1, sizeof(stData));



    yData* p2 = static_cast<yData*>(mem_pool.ngx_palloc(512));
    if (p2 == nullptr)
    {
        printf("ngx_palloc 512 bytes fail...");
        return -1;
    }

    p2->ptr = static_cast<char*>(malloc(12));
    strcpy(p2->ptr, "hello world");


    ngx_pool_cleanup_t* c2 = mem_pool.ngx_pool_cleanup_add(0);
    c2->handler = self_handler_02;
    c2->data = p2->ptr;


    return 0;
}