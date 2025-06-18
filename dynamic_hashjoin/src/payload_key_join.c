/**
 * @file    payload_key_join.c
 * @brief  The implementation of hash join using dynamic payload as key.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>              /* CPU_ZERO, CPU_SET */
#include <pthread.h>            /* pthread_* */
#include <string.h>             /* memset */
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* memalign */
#include <sys/time.h>           /* gettimeofday */
#include <stdbool.h>
#include "no_partitioning_join.h"
#include "npj_params.h"         /* constant parameters */
#include "npj_types.h"          /* bucket_t, hashtable_t, bucket_buffer_t */
#include "rdtsc.h"              /* startTimer, stopTimer */
#include "lock.h"               /* lock, unlock */
#include "cpu_mapping.h"        /* get_cpu_id */

#include "../../gem5_dda/include/gem5/m5ops.h"

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

#include "barrier.h"            /* pthread_barrier_* */
#include "affinity.h"           /* pthread_attr_setaffinity_np */
#include "generator.h"          /* numa_localize() */

#ifdef JOIN_RESULT_MATERIALIZE
#include "tuple_buffer.h"       /* for materialization */
#endif

#ifndef BARRIER_ARRIVE
/** barrier wait macro */
#define BARRIER_ARRIVE(B,RV)                            \
    RV = pthread_barrier_wait(B);                       \
    if(RV !=0 && RV != PTHREAD_BARRIER_SERIAL_THREAD){  \
        printf("Couldn't wait on barrier\n");           \
        exit(EXIT_FAILURE);                             \
    }
#endif

#ifndef NEXT_POW_2
/** 
 *  compute the next number, greater than or equal to 32-bit unsigned v.
 *  taken from "bit twiddling hacks":
 *  http://graphics.stanford.edu/~seander/bithacks.html
 */
#define NEXT_POW_2(V)                           \
    do {                                        \
        V--;                                    \
        V |= V >> 1;                            \
        V |= V >> 2;                            \
        V |= V >> 4;                            \
        V |= V >> 8;                            \
        V |= V >> 16;                           \
        V++;                                    \
    } while(0)
#endif

#ifndef HASH
#define HASH(X, MASK, SKIP) (((X) & MASK) >> SKIP)
#endif

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                    \
    if(COND) { fprintf(stdout, "[DEBUG] "MSG, ## __VA_ARGS__); }
#else
#define DEBUGMSG(COND, MSG, ...) 
#endif

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize;  /* defined in generator.c */
extern int nthreads;      /* defined in generator.c */

/**
 * \ingroup NPO arguments to the threads
 */
typedef struct arg_t arg_t;

struct arg_t {
    int32_t             tid;
    hashtable_t *       ht;
    relation_t          relR;
    relation_t          relS;
    pthread_barrier_t * barrier;
    int64_t             num_results;
    int                 payload_idx;    /* index of payload to use as key */

    /* results of the thread */
    threadresult_t * threadresult;

#ifndef NO_TIMING
    /* stats about the thread */
    uint64_t timer1, timer2, timer3;
    struct timeval start, end;
#endif
};

/** 
 * @defgroup OverflowBuckets Buffer management for overflowing buckets.
 * Simple buffer management for overflow-buckets organized as a 
 * linked-list of bucket_buffer_t.
 * @{
 */

/** 
 * Initializes a new bucket_buffer_t for later use in allocating 
 * buckets when overflow occurs.
 * 
 * @param ppbuf [in,out] bucket buffer to be initialized 
 */
void 
init_bucket_buffer(bucket_buffer_t ** ppbuf)
{
    bucket_buffer_t * overflowbuf;
    overflowbuf = (bucket_buffer_t*) malloc(sizeof(bucket_buffer_t));
    overflowbuf->count = 0;
    overflowbuf->next  = NULL;

    *ppbuf = overflowbuf;
}

/** 
 * Returns a new bucket_t from the given bucket_buffer_t.
 * If the bucket_buffer_t does not have enough space, then allocates
 * a new bucket_buffer_t and adds to the list.
 *
 * @param result [out] the new bucket
 * @param buf [in,out] the pointer to the bucket_buffer_t pointer
 */
static inline void 
get_new_bucket(bucket_t ** result, bucket_buffer_t ** buf)
{
    if((*buf)->count < OVERFLOW_BUF_SIZE) {
        *result = (*buf)->buf + (*buf)->count;
        (*buf)->count ++;
    }
    else {
        /* need to allocate new buffer */
        bucket_buffer_t * new_buf = (bucket_buffer_t*) 
                                    malloc(sizeof(bucket_buffer_t));
        new_buf->count = 1;
        new_buf->next  = *buf;
        *buf    = new_buf;
        *result = new_buf->buf;
    }
}

/** De-allocates all the bucket_buffer_t */
void
free_bucket_buffer(bucket_buffer_t * buf)
{
    do {
        bucket_buffer_t * tmp = buf->next;
        free(buf);
        buf = tmp;
    } while(buf);
}

/** @} */

/** 
 * @defgroup NPO The No Partitioning Optimized Join Implementation
 * @{
 */

/** 
 * Allocates a hashtable of NUM_BUCKETS and inits everything to 0. 
 * 
 * @param ht pointer to a hashtable_t pointer
 */
void 
allocate_hashtable(hashtable_t ** ppht, uint32_t nbuckets, int list_p)
{
    // 声明一个指向hashtable_t的指针
    hashtable_t * ht;

    // 为hashtable_t结构体分配内存
    ht              = (hashtable_t*)malloc(sizeof(hashtable_t));
    // 设置hash表的桶数量
    ht->num_buckets = nbuckets;
    // 将桶数量调整为2的幂次方，这是为了优化哈希计算
    NEXT_POW_2((ht->num_buckets));

    /* 为hash表的桶分配内存，使用posix_memalign确保内存对齐到缓存行大小
     * 这样可以提高内存访问效率，减少缓存未命中
     */
    if (posix_memalign((void**)&ht->buckets, CACHE_LINE_SIZE,
                       ht->num_buckets * sizeof(bucket_t))){
        perror("Aligned allocation failed!\n");
        exit(EXIT_FAILURE);
    }

    /** 这是一个实验性功能，用于NUMA架构下的内存本地化
     * 如果启用了numalocalize，将内存分配到当前NUMA节点
     * 这样可以减少跨NUMA节点的内存访问，提高性能
     */
    if(numalocalize) {
        tuple_t * mem = (tuple_t *) ht->buckets;
        uint32_t ntuples = (ht->num_buckets*sizeof(bucket_t))/sizeof(tuple_t);
        numa_localize(mem, ntuples, nthreads);
    }

    // 将新分配的桶内存初始化为0
    memset(ht->buckets, 0, ht->num_buckets * sizeof(bucket_t));
    // 设置哈希函数的跳过位数，默认为0（模运算哈希）
    ht->skip_bits = 0; /* the default for modulo hash */
    // 计算哈希掩码，用于哈希计算
    ht->hash_mask = (ht->num_buckets - 1) >> list_p ;
    // 将创建的hash表指针赋值给输出参数
    *ppht = ht;
}

/** 
 * Releases memory allocated for the hashtable.
 * 
 * @param ht pointer to hashtable
 */
void 
destroy_hashtable(hashtable_t * ht)
{
    free(ht->buckets);
    free(ht);
}

/** 
 * Single-thread hashtable build method, ht is pre-allocated.
 * Uses specified payload index as key.
 * 
 * @param ht hastable to be built
 * @param rel the build relation
 * @param payload_idx index of payload to use as key
 */
void 
build_hashtable_st_payload(hashtable_t *ht, relation_t *rel)
{
    uint32_t i;
    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

    for(i=0; i < rel->num_tuples; i++){
        tuple_t * dest;
        bucket_t * curr, * nxt;
        /* Use specified payload as key for hashing */
        int64_t idx = HASH(rel->tuples[i].payload[0], hashmask, skipbits);

        /* copy the tuple to appropriate hash bucket */
        /* if full, follow nxt pointer to find correct place */
        curr = ht->buckets + idx;
        nxt  = curr->next;

        if(curr->count == BUCKET_SIZE) {
            if(!nxt || nxt->count == BUCKET_SIZE) {
                bucket_t * b;
                b = (bucket_t*) calloc(1, sizeof(bucket_t));
                curr->next = b;
                b->next = nxt;
                b->count = 1;
                dest = b->tuples;
            }
            else {
                dest = nxt->tuples + nxt->count;
                nxt->count ++;
            }
        }
        else {
            dest = curr->tuples + curr->count;
            curr->count ++;
        }
        rel->tuples[i].key = rel->tuples[i].payload[0];
        *dest = rel->tuples[i];
    }
}

/** 
 * Probes the hashtable for the given outer relation, returns num results. 
 * Uses specified payload index as key for probing.
 * 
 * @param ht hashtable to be probed
 * @param rel the probing outer relation
 * @param output chained tuple buffer to write join results
 * @param payload_idx index of payload to use as key
 * 
 * @return number of matching tuples
 */
hashtable_t * 
rehash_payload(hashtable_t *ht, void * output, int payload_idx)
{
    hashtable_t * new_ht;
    int64_t i, j;
    const int64_t hashmask = ht->hash_mask;
    const int64_t skipbits = ht->skip_bits;
    const int32_t nbuckets = ht->num_buckets;

    allocate_hashtable(&new_ht, nbuckets, 4);

    m5_checkpoint(0,0);
    m5_reset_stats(0,0);

    for (i = 0; i < nbuckets; i++) {
        bucket_t * c = ht->buckets + i;
        do {
            for (j = 0; j < c->count; j++) {
                tuple_t * dest;
                bucket_t * curr, * nxt;
                int64_t idx = HASH(c->tuples[j].payload[payload_idx], hashmask, skipbits);
                curr = new_ht->buckets + idx;
                nxt  = curr->next;
                if (curr->count == BUCKET_SIZE) {
                    if (!nxt || nxt->count == BUCKET_SIZE) {
                        bucket_t * b;
                        b = (bucket_t*) calloc(1, sizeof(bucket_t));
                        curr->next = b;
                        b->next = nxt;
                        b->count = 1;
                        dest = b->tuples;
                    }
                    else {
                        dest = nxt->tuples + nxt->count;
                        nxt->count ++;
                    }
                }
                else {
                    dest = curr->tuples + curr->count;
                    curr->count ++;
                }
                dest->key = b->tuples[j].payload[payload_idx];
                *dest = b->tuples[j];
            }
        } while(c);
    }
    m5_dump_stats(0,0);
    destroy_hashtable(ht);
    return new_ht;
}

/** print out the execution time statistics of the join */
static void 
print_timing(uint64_t total, uint64_t build, uint64_t part,
            uint64_t numtuples, int64_t result,
            struct timeval * start, struct timeval * end)
{
    double diff_usec = (((*end).tv_sec*1000000L + (*end).tv_usec)
                        - ((*start).tv_sec*1000000L+(*start).tv_usec));
    double cyclestuple = total;
    cyclestuple /= numtuples;
    fprintf(stdout, "RUNTIME TOTAL, BUILD, PART (cycles): \n");
    fprintf(stderr, "%llu \t %llu \t %llu ", 
            total, build, part);
    fprintf(stdout, "\n");
    fprintf(stdout, "TOTAL-TIME-USECS, TOTAL-TUPLES, CYCLES-PER-TUPLE: \n");
    fprintf(stdout, "%.4lf \t %llu \t ", diff_usec, result);
    fflush(stdout);
    fprintf(stderr, "%.4lf ", cyclestuple);
    fflush(stderr);
    fprintf(stdout, "\n");
}

/** Single-threaded join implementation using specified payload as key */
result_t *
NPO_st(relation_t *relR, relation_t *relS, int nthreads, int payload_idx)
{
    hashtable_t * ht;
    int64_t result = 0;
    result_t * joinresult;

#ifndef NO_TIMING
    struct timeval start, end;
    uint64_t timer1, timer2, timer3;
#endif

    uint32_t nbuckets = (relR->num_tuples / BUCKET_SIZE);
    allocate_hashtable(&ht, nbuckets, 4);

    joinresult = (result_t *) malloc(sizeof(result_t));
#ifdef JOIN_RESULT_MATERIALIZE
    joinresult->resultlist = (threadresult_t *) malloc(sizeof(threadresult_t));
#endif

#ifndef NO_TIMING
    gettimeofday(&start, NULL);
    startTimer(&timer1);
    startTimer(&timer2); 
    timer3 = 0; /* no partitioning */
#endif

    build_hashtable_st_payload(ht, relR);

#ifndef NO_TIMING
    stopTimer(&timer2); /* for build */
#endif

#ifdef JOIN_RESULT_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = chainedtuplebuffer_init();
#else
    void * chainedbuf = NULL;
#endif
    for (int i = 1; i < 32; i ++) {
        ht = rehash_payload(ht, chainedbuf, i);
    }

#ifdef JOIN_RESULT_MATERIALIZE
    threadresult_t * thrres = &(joinresult->resultlist[0]);/* single-thread */
    thrres->nresults = result;
    thrres->threadid = 0;
    thrres->results  = (void *) chainedbuf;
#endif

#ifndef NO_TIMING
    stopTimer(&timer1); /* over all */
    gettimeofday(&end, NULL);
    /* now print the timing results: */
    print_timing(timer1, timer2, timer3, relS->num_tuples, result, &start, &end);
#endif

    destroy_hashtable(ht);

    joinresult->totalresults = result;
    joinresult->nthreads     = 1;

    return joinresult;
}

/** @} */ 