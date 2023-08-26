#include <sched.h>              /* CPU_ZERO, CPU_SET */
#include <pthread.h>            /* pthread_* */
#include <stdlib.h>             /* malloc, posix_memalign */
#include <sys/time.h>           /* gettimeofday */
#include <stdio.h>              /* printf */
#include <smmintrin.h>          /* simd only for 32-bit keys – SSE4.1 */
#include <immintrin.h>
#include <string.h>
#include <cmath>
#include <algorithm>
#include <unistd.h>
#include <iostream>

#include "types.h"

// #include "Utils.h"
// // #include "parallel_radix_join.h"
#include "prj_params.h"         /* constant parameters */
#include "task_queue.h"         /* task_queue_* */
#include "cpu_mapping.h"        /* get_cpu_id */
#include "rdtsc.h"              /* startTimer, stopTimer */
// #ifdef PERF_COUNTERS
// #include "perf_counters.h"      /* PCM_x */
// #endif

#include "barrier.h"            /* pthread_barrier_* */
// #include "affinity.h"           /* pthread_attr_setaffinity_np */
#include "generator.h"          /* numa_localize() */
// #ifdef VTUNE_PROFILE
// #include "ittnotify.h"
// #endif

// // include hash functions
// #include "hashFunctions.hpp"

using namespace std;

/** \internal */

#ifndef BARRIER_ARRIVE
/** barrier wait macro */
#define BARRIER_ARRIVE(B,RV)                            \
    RV = pthread_barrier_wait(B);                       \
    if(RV !=0 && RV != PTHREAD_BARRIER_SERIAL_THREAD){  \
        printf("Couldn't wait on barrier\n");           \
        exit(EXIT_FAILURE);                             \
    }
#endif

/** checks malloc() result */
#ifndef MALLOC_CHECK
#define MALLOC_CHECK(M)                                                 \
    if(!M){                                                             \
        printf("[ERROR] MALLOC_CHECK: %s : %d\n", __FILE__, __LINE__);  \
        perror(": malloc() failed!\n");                                 \
        exit(EXIT_FAILURE);                                             \
    }
#endif

/* #define RADIX_HASH(V)  ((V>>7)^(V>>13)^(V>>21)^V) */
#define HASH_BIT_MODULO(K, MASK, NBITS) (((K) & MASK) >> NBITS)

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

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

#ifdef SYNCSTATS
#define SYNC_TIMERS_START(A, TID)               \
    do {                                        \
        uint64_t tnow;                          \
        startTimer(&tnow);                      \
        A->localtimer.sync1[0]      = tnow;     \
        A->localtimer.sync1[1]      = tnow;     \
        A->localtimer.sync3         = tnow;     \
        A->localtimer.sync4         = tnow;     \
        A->localtimer.finish_time   = tnow;     \
        if(TID == 0) {                          \
            A->globaltimer->sync1[0]    = tnow; \
            A->globaltimer->sync1[1]    = tnow; \
            A->globaltimer->sync3       = tnow; \
            A->globaltimer->sync4       = tnow; \
            A->globaltimer->finish_time = tnow; \
        }                                       \
    } while(0)

#define SYNC_TIMER_STOP(T) stopTimer(T)
#define SYNC_GLOBAL_STOP(T, TID) if(TID==0){ stopTimer(T); }
#else
#define SYNC_TIMERS_START(A, TID)
#define SYNC_TIMER_STOP(T)
#define SYNC_GLOBAL_STOP(T, TID)
#endif

/** Debug msg logging method */
#define DEBUG 0
#if DEBUG
#define DEBUGMSG(COND, MSG, ...)                                    \
    if(COND) { fprintf(stdout, "[DEBUG] " MSG, ## __VA_ARGS__); }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif

/** An experimental feature to allocate input relations numa-local */
static int numalocalize = 0;
static uint64_t FANOUT_PASS1;
/* num-parts at pass-1 */
static uint64_t FANOUT_PASS2;

/**
 * Put an odd number of cache lines between partitions in pass-2:
 * Here we put 3 cache lines.
 */
static uint64_t SMALL_PADDING_TUPLES;
static uint64_t PADDING_TUPLES;

/** @warning This padding must be allocated at the end of relation */
static uint64_t RELATION_PADDING;

typedef struct arg_t  arg_t;
typedef struct part_t part_t;
typedef struct synctimer_t synctimer_t;
typedef struct join_result_t join_result_t;
typedef join_result_t (*JoinFunction)(const relation_t * const,
                                const relation_t * const,
                                relation_t * const, uint64_t);

#ifdef SYNCSTATS
/** holds syncronization timing stats if configured with --enable-syncstats */
struct synctimer_t {
    /** Barrier for computation of thread-local histogram */
    uint64_t sync1[3]; /* for rel R and for rel S */
    /** Barrier for end of radix partit. pass-1 */
    uint64_t sync3;
    /** Barrier before join (build-probe) begins */
    uint64_t sync4;
    /** Finish time */
    uint64_t finish_time;
};
#endif

/** holds the arguments passed to each thread */
struct arg_t {
    uint32_t ** histR;
    tuple_t *  relR;
    tuple_t *  tmpR;
    uint32_t ** histS;
    tuple_t *  relS;
    tuple_t *  tmpS;

    uint32_t numR;
    uint32_t numS;
    uint32_t totalR;
    uint32_t totalS;
    int      ratio_holes;

    task_queue_t *      join_queue;
    task_queue_t *      part_queue;
#if SKEW_HANDLING
    task_queue_t *      skew_queue;
    task_t **           skewtask;
#endif
    pthread_barrier_t * barrier;
    JoinFunction        join_function;
    uint64_t result;
    uint32_t my_tid;
    unsigned int     nthreads;
  uint64_t checksum;

    /* stats about the thread */
    int32_t        parts_processed;
    uint64_t       timer1, timer2, timer3;
    struct timeval start, end;
  struct timeval part_end;
#ifdef SYNCSTATS
    /** Thread local timers : */
    synctimer_t localtimer;
    /** Global synchronization timers, only filled in by thread-0 */
    synctimer_t * globaltimer;
#endif
} __attribute__((aligned(CACHE_LINE_SIZE)));

/** holds arguments passed for partitioning */
struct part_t {
    tuple_t *  rel;
    tuple_t *  tmp;
    uint32_t ** hist;
    uint32_t *  output;
    arg_t   *  thrargs;
    uint32_t   num_tuples;
    uint32_t   total_tuples;
    int32_t    R;
    uint32_t   D;
    int        relidx;  /* 0: R, 1: S */
    uint32_t   padding;
} __attribute__((aligned(CACHE_LINE_SIZE)));

static void *
alloc_aligned(size_t size)
{
    void * ret;
    int rv;
    rv = posix_memalign((void**)&ret, CACHE_LINE_SIZE, size);

    if (rv) {
        perror("alloc_aligned() failed: out of memory");
        return 0;
    }

    return ret;
}

/** \endinternal */

/**
 * @defgroup Radix Radix Join Implementation Variants
 * @{
 */

static unsigned int cnt_power(uint32_t N)
{
  unsigned int res = 0;
  while (N)
  {
    N >>= 1;
    res++;
  }
  return res-1;
}

static
join_result_t
pull_data(const relation_t * const R,
      const relation_t * const S,
      relation_t * const tmpR)
{
  const uint64_t numR = R->num_tuples;
  const uint64_t numS = S->num_tuples;
  uint64_t matches = 0;
  uint64_t checksum = 0;
  const tuple_t * tupleR = R->tuples;
  const tuple_t * tupleS = S->tuples;

  //uint64_t build_time, probe_time;

//  startTimer(&build_time);
  for (uint64_t i = 0; i < numR; ++i)
    checksum += tupleR[i].key; //+ tupleR[i].payload;
//  stopTimer(&build_time);
//  printf("build time = %llu\n", build_time);

//  startTimer(&probe_time);
  for (uint64_t i = 0; i < numS; ++i)
  {
    checksum += tupleS[i].key; //+ tupleS[i].payload;
  }
//  stopTimer(&probe_time);
//  printf("probe time = %llu\n", probe_time);

  join_result_t res = {matches, checksum, 0,0,0};
  return res;
}

// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-Wunused-parameter"

template<bool is_checksum, int NUM_RADIX_BITS>
static
join_result_t
array_join(const relation_t * const R,
       const relation_t * const S,
       relation_t * const tmpR, uint64_t totalNumR)
{
  const uint64_t numR = R->num_tuples;
  const uint64_t numS = S->num_tuples;
    uint64_t range = (totalNumR * R->ratio_holes)>>NUM_RADIX_BITS;
  uint64_t N = range;
  uint64_t matches = 0;
  uint64_t checksum = 0;
  const tuple_t * tupleR = R->tuples;
  const tuple_t * tupleS = S->tuples;
  NEXT_POW_2(N);

  const uint64_t MASK = (N-1) << NUM_RADIX_BITS;

  value_t * array = (value_t *) malloc(sizeof(value_t) * (range +1));
  memset(array, 0, sizeof(value_t) * (range+1));

  for (uint64_t i=0; i < numR; ++i)
  {
    uint64_t idx = HASH_BIT_MODULO(tupleR[i].key, MASK, NUM_RADIX_BITS);
    array[idx] = tupleR[i].payload;
  }

  for (uint64_t i=0; i < numS; ++i)
  {
    uint64_t idx = HASH_BIT_MODULO(tupleS[i].key, MASK, NUM_RADIX_BITS);
    if (array[idx])
    {
      ++matches;
      if (is_checksum)
        checksum += array[idx] + tupleS[i].payload;
    }
  }

  free(array);

  join_result_t res = {matches, checksum, 0,0,0};
  return res;
}

// #pragma GCC diagnostic pop
/** computes and returns the histogram size for join */
static
inline
uint32_t
get_hist_size(uint32_t relSize) __attribute__((always_inline));

static
inline
uint32_t
get_hist_size(uint32_t relSize)
{
    NEXT_POW_2(relSize);
    relSize >>= 2;
    if(relSize < 4) relSize = 4;
    return relSize;
}

/** software prefetching function */
static
inline
void
prefetch(void * addr) __attribute__((always_inline));

static
inline
void
prefetch(void * addr)
{
    /* #ifdef __x86_64__ */
    __asm__ __volatile__ ("prefetcht0 %0" :: "m" (*(uint32_t*)addr));
    /* _mm_prefetch(addr, _MM_HINT_T0); */
    /* #endif */
}

/**
 * Radix clustering algorithm (originally described by Manegold et al)
 * The algorithm mimics the 2-pass radix clustering algorithm from
 * Kim et al. The difference is that it does not compute
 * prefix-sum, instead the sum (offset in the code) is computed iteratively.
 *
 * @warning This method puts padding between clusters, see
 * radix_cluster_nopadding for the one without padding.
 *
 * @param outRel [out] result of the partitioning
 * @param inRel [in] input relation
 * @param hist [out] number of tuples in each partition
 * @param R cluster bits
 * @param D radix bits per pass
 * @returns tuples per partition.
 */
static
void
radix_cluster(relation_t * outRel,
              relation_t * inRel,
              uint32_t * hist,
              unsigned int R,
              unsigned int D)
{
    uint32_t i;
    uint32_t M = ((1u << D) - 1u) << R;
    uint32_t offset;
    uint32_t fanOut = 1u << D;

    /* the following are fixed size when D is same for all the passes,
       and can be re-used from call to call. Allocating in this function
       just in case D differs from call to call. */
    uint32_t dst[fanOut];

    /* count tuples per cluster */
    for( i=0; i < inRel->num_tuples; i++ ){
        uint32_t idx = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        hist[idx]++;
    }
    offset = 0;
    /* determine the start and end of each cluster depending on the counts. */
    for ( i=0; i < fanOut; i++ ) {
        /* dst[i]      = outRel->tuples + offset; */
        /* determine the beginning of each partitioning by adding some
           padding to avoid L1 conflict misses during scatter. */
        dst[i] = offset + i * SMALL_PADDING_TUPLES;
        offset += hist[i];
    }

    /* copy tuples to their corresponding clusters at appropriate offsets */
    for( i=0; i < inRel->num_tuples; i++ ){
        uint32_t idx   = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        outRel->tuples[ dst[idx] ] = inRel->tuples[i];
        ++dst[idx];
    }
}

// /**
//  * Radix clustering algorithm which does not put padding in between
//  * clusters. This is used only by single threaded radix join implementation RJ.
//  *
//  * @param outRel
//  * @param inRel
//  * @param hist
//  * @param R
//  * @param D
//  */
// static
// void
// radix_cluster_nopadding(relation_t *outRel, relation_t *inRel, unsigned int R, unsigned int D)
// {
//     tuple_t ** dst;
//     tuple_t * input;
//     /* tuple_t ** dst_end; */
//     uint32_t * tuples_per_cluster;
//     uint32_t i;
//     uint32_t offset;
//     const uint32_t M = ((1u << D) - 1) << R;
//     const uint32_t fanOut = 1u << D;
//     const uint32_t ntuples = inRel->num_tuples;

//     tuples_per_cluster = (uint32_t*)calloc(fanOut, sizeof(uint32_t));
//     /* the following are fixed size when D is same for all the passes,
//        and can be re-used from call to call. Allocating in this function
//        just in case D differs from call to call. */
//     dst     = (tuple_t**)malloc(sizeof(tuple_t*)*fanOut);
//     /* dst_end = (tuple_t**)malloc(sizeof(tuple_t*)*fanOut); */

//     input = inRel->tuples;
//     /* count tuples per cluster */
//     for( i=0; i < ntuples; i++ ){
//         uint32_t idx = (uint32_t)(HASH_BIT_MODULO(input->key, M, R));
//         tuples_per_cluster[idx]++;
//         input++;
//     }

//     offset = 0;
//     /* determine the start and end of each cluster depending on the counts. */
//     for ( i=0; i < fanOut; i++ ) {
//         dst[i]      = outRel->tuples + offset;
//         offset     += tuples_per_cluster[i];
//         /* dst_end[i]  = outRel->tuples + offset; */
//     }

//     input = inRel->tuples;
//     /* copy tuples to their corresponding clusters at appropriate offsets */
//     for( i=0; i < ntuples; i++ ){
//         uint32_t idx   = (uint32_t)(HASH_BIT_MODULO(input->key, M, R));
//         *dst[idx] = *input;
//         ++dst[idx];
//         input++;
//         /* we pre-compute the start and end of each cluster, so the following
//            check is unnecessary */
//         /* if(++dst[idx] >= dst_end[idx]) */
//         /*     REALLOCATE(dst[idx], dst_end[idx]); */
//     }

//     /* clean up temp */
//     /* free(dst_end); */
//     free(dst);
//     free(tuples_per_cluster);
// }


/**
 * This function implements the radix clustering of a given input
 * relations. The relations to be clustered are defined in task_t and after
 * clustering, each partition pair is added to the join_queue to be joined.
 *
 * @param task description of the relation to be partitioned
 * @param join_queue task queue to add join tasks after clustering
 */
static
void serial_radix_partition(task_t * const task,
                            task_queue_t * join_queue,
                            const int R, const int D)
{
    uint32_t offsetR = 0, offsetS = 0;
    const size_t fanOut = 1u << D;  /*(NUM_RADIX_BITS / NUM_PASSES);*/
    uint32_t * outputR, * outputS;

    outputR = (uint32_t*)calloc(fanOut+1, sizeof(uint32_t));
    outputS = (uint32_t*)calloc(fanOut+1, sizeof(uint32_t));
    /* TODO: measure the effect of memset() */
    /* memset(outputR, 0, fanOut * sizeof(int32_t)); */
    radix_cluster(&task->tmpR, &task->relR, outputR, R, D);

    /* memset(outputS, 0, fanOut * sizeof(int32_t)); */
    radix_cluster(&task->tmpS, &task->relS, outputS, R, D);

    /* task_t t; */
    for(unsigned int i = 0; i < fanOut; i++) {
        if(outputR[i] > 0 && outputS[i] > 0) {
            task_t * t = task_queue_get_slot_atomic(join_queue);
            t->relR.num_tuples = outputR[i];
            t->relR.tuples = task->tmpR.tuples + offsetR
                             + i * SMALL_PADDING_TUPLES;
            t->relR.ratio_holes = task->relR.ratio_holes;
            t->tmpR.tuples = task->relR.tuples + offsetR
                             + i * SMALL_PADDING_TUPLES;
            t->tmpR.ratio_holes = task->relR.ratio_holes;
            offsetR += outputR[i];

            t->relS.num_tuples = outputS[i];
            t->relS.tuples = task->tmpS.tuples + offsetS
                             + i * SMALL_PADDING_TUPLES;
            t->tmpS.tuples = task->relS.tuples + offsetS
                             + i * SMALL_PADDING_TUPLES;
            offsetS += outputS[i];

            /* task_queue_copy_atomic(join_queue, &t); */
            task_queue_add_atomic(join_queue, t);
        }
        else {
            offsetR += outputR[i];
            offsetS += outputS[i];
        }
    }
    free(outputR);
    free(outputS);
}

/**
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms.
 *
 * @param part description of the relation to be partitioned
 */
static
void
parallel_radix_partition(part_t * const part)
{
    const tuple_t* rel    = part->rel;
    uint32_t ** hist   = part->hist;
    uint32_t * output = part->output;

    const uint32_t my_tid     = part->thrargs->my_tid;
    const uint32_t nthreads   = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;

    const int32_t  R       = part->R;
    const int32_t  D       = part->D;
    const uint32_t fanOut  = 1u << D;
    const uint32_t MASK    = (fanOut - 1) << R;
    const uint32_t padding = part->padding;

    uint32_t sum = 0;
    uint32_t i, j;
    int rv;

    uint32_t dst[fanOut+1];

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    uint32_t * my_hist = hist[my_tid];

    for(i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        my_hist[idx] ++;
    }

    /* compute local prefix sum on hist */
    for(i = 0; i < fanOut; i++){
        sum += my_hist[i];
        my_hist[i] = sum;
    }

    SYNC_TIMER_STOP(&part->thrargs->localtimer.sync1[part->relidx]);
    /* wait at a barrier until each thread complete histograms */
    BARRIER_ARRIVE(part->thrargs->barrier, rv);
    /* barrier global sync point-1 */
    SYNC_GLOBAL_STOP(&part->thrargs->globaltimer->sync1[part->relidx], my_tid);

    /* determine the start and end of each cluster */
    for(i = 0; i < my_tid; i++) {
        for(j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for(i = my_tid; i < nthreads; i++) {
        for(j = 1; j < fanOut; j++)
            output[j] += hist[i][j-1];
    }

    for(i = 0; i < fanOut; i++ ) {
        output[i] += i * padding; //PADDING_TUPLES;
        dst[i] = output[i];
    }
    output[fanOut] = part->total_tuples + fanOut * padding; //PADDING_TUPLES;

    tuple_t * tmp = part->tmp;

    /* Copy tuples to their corresponding clusters */
    for(i = 0; i < num_tuples; i++ ){
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        tmp[dst[idx]] = rel[i];
        ++dst[idx];
    }
}

/**
 * @defgroup SoftwareManagedBuffer Optimized Partitioning Using SW-buffers
 * @{
 */
typedef union {
    struct {
        tuple_t tuples[CACHE_LINE_SIZE/sizeof(tuple_t)];
    } tuples;
    struct {
        tuple_t tuples[CACHE_LINE_SIZE/sizeof(tuple_t) - 1];
        uint32_t slot;
    } data;
} cacheline_t;

#define TUPLESPERCACHELINE (CACHE_LINE_SIZE/sizeof(tuple_t))

/**
 * Makes a non-temporal write of 64 bytes from src to dst.
 * Uses vectorized non-temporal stores if available, falls
 * back to assignment copy.
 *
 * @param dst
 * @param src
 *
 * @return
 */
static inline void
store_nontemp_64B(void * dst, void * src)
{
#ifdef __AVX__
    register __m256i * d1 = (__m256i*) dst;
    register __m256i s1 = *((__m256i*) src);
    register __m256i * d2 = d1+1;
    register __m256i s2 = *(((__m256i*) src)+1);

    _mm256_stream_si256(d1, s1);
    _mm256_stream_si256(d2, s2);

#elif defined(__SSE2__)

    register __m128i * d1 = (__m128i*) dst;
    register __m128i * d2 = d1+1;
    register __m128i * d3 = d1+2;
    register __m128i * d4 = d1+3;
    register __m128i s1 = *(__m128i*) src;
    register __m128i s2 = *((__m128i*)src + 1);
    register __m128i s3 = *((__m128i*)src + 2);
    register __m128i s4 = *((__m128i*)src + 3);

    _mm_stream_si128 (d1, s1);
    _mm_stream_si128 (d2, s2);
    _mm_stream_si128 (d3, s3);
    _mm_stream_si128 (d4, s4);

#else
    /* just copy with assignment */
    *(cacheline_t *)dst = *(cacheline_t *)src;

#endif

}

// /**
//  * This function implements the parallel radix partitioning of a given input
//  * relation. Parallel partitioning is done by histogram-based relation
//  * re-ordering as described by Kim et al. Parallel partitioning method is
//  * commonly used by all parallel radix join algorithms. However this
//  * implementation is further optimized to benefit from write-combining and
//  * non-temporal writes.
//  *
//  * @param part description of the relation to be partitioned
//  */
// static
// void
// parallel_radix_partition_optimized(part_t * const part)
// {
//     const tuple_t * restrict rel    = part->rel;
//     uint32_t **               hist   = part->hist;
//     uint32_t *       restrict output = part->output;

//     const uint32_t my_tid     = part->thrargs->my_tid;
//     const uint32_t nthreads   = part->thrargs->nthreads;
//     const uint32_t num_tuples = part->num_tuples;

//     const int32_t  R       = part->R;
//     const int32_t  D       = part->D;
//     const uint32_t fanOut  = 1u << D;
//     const uint32_t MASK    = (fanOut - 1) << R;
//     const uint32_t padding = part->padding;

//     uint32_t sum = 0;
//     uint32_t i, j;
//     int rv;

//     /* compute local histogram for the assigned region of rel */
//     /* compute histogram */
//     uint32_t * my_hist = hist[my_tid];

//     for(i = 0; i < num_tuples; i++) {
//         uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
//         my_hist[idx] ++;
//     }

//     /* compute local prefix sum on hist */
//     for(i = 0; i < fanOut; i++){
//         sum += my_hist[i];
//         my_hist[i] = sum;
//     }

//     SYNC_TIMER_STOP(&part->thrargs->localtimer.sync1[part->relidx]);
//     /* wait at a barrier until each thread complete histograms */
//     BARRIER_ARRIVE(part->thrargs->barrier, rv);
//     /* barrier global sync point-1 */
//     SYNC_GLOBAL_STOP(&part->thrargs->globaltimer->sync1[part->relidx], my_tid);

//     /* determine the start and end of each cluster */
//     for(i = 0; i < my_tid; i++) {
//         for(j = 0; j < fanOut; j++)
//             output[j] += hist[i][j];
//     }
//     for(i = my_tid; i < nthreads; i++) {
//         for(j = 1; j < fanOut; j++)
//             output[j] += hist[i][j-1];
//     }

//     /* uint32_t pre; /\* nr of tuples to cache-alignment *\/ */
//     tuple_t * restrict tmp = part->tmp;
//     /* software write-combining buffer */
//     cacheline_t *buffer;
//   posix_memalign((void**)(&buffer), CACHE_LINE_SIZE, sizeof(cacheline_t) * fanOut);

//     for(i = 0; i < fanOut; i++ ) {
//         uint32_t off = output[i] + i * padding;
//         /* pre        = (off + TUPLESPERCACHELINE) & ~(TUPLESPERCACHELINE-1); */
//         /* pre       -= off; */
//         output[i]  = off;
//         buffer[i].data.slot = off;
//     }
//     output[fanOut] = part->total_tuples + fanOut * padding;

//     /* Copy tuples to their corresponding clusters */
//     for(i = 0; i < num_tuples; i++ ){
//         uint32_t  idx     = HASH_BIT_MODULO(rel[i].key, MASK, R);
//         uint32_t  slot    = buffer[idx].data.slot;
//         tuple_t * tup     = (tuple_t *)(buffer + idx);
//         uint32_t  slotMod = (slot) & (TUPLESPERCACHELINE - 1);
//         tup[slotMod]      = rel[i];

//         if(slotMod == (TUPLESPERCACHELINE-1)){
//             /* write out 64-Bytes with non-temporal store */
//             store_nontemp_64B((tmp+slot-(TUPLESPERCACHELINE-1)), (buffer+idx));
//             /* writes += TUPLESPERCACHELINE; */
//         }

//         buffer[idx].data.slot = slot+1;
//     }
//     /* _mm_sfence (); */

//     BARRIER_ARRIVE(part->thrargs->barrier, rv);


//     /* write out the remainders in the buffer */
//     for(i = 0; i < fanOut; i++ ) {
//         uint32_t slot  = buffer[i].data.slot;
//         uint32_t sz    = (slot) & (TUPLESPERCACHELINE - 1);
//         slot          -= sz;
//     uint32_t startPos = (slot < output[i]) ? (output[i] - slot) : 0;
//         for(uint32_t j = startPos; j < sz; j++) {
//             tmp[slot+j]  = buffer[i].data.tuples[j];
//         }
//     }

//   free(buffer);
// }

// /** @} */

// *
//  * The main thread of parallel radix join. It does partitioning in parallel with
//  * other threads and during the join phase, picks up join tasks from the task
//  * queue and calls appropriate JoinFunction to compute the join task.
//  *
//  * @param param
//  *
//  * @return

template<unsigned int NUM_RADIX_BITS, unsigned int NUM_PASSES>
static
void *
prj_thread(void * param)
{
    arg_t * args   = (arg_t*) param;
    int32_t my_tid = args->my_tid;

    const size_t  fanOut = 1u << (NUM_RADIX_BITS / NUM_PASSES);
    const unsigned int R = (NUM_RADIX_BITS / NUM_PASSES);
    const int D = (NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));


    uint64_t results = 0;
    int rv;

    part_t part;
    task_t * task;
    task_queue_t * part_queue;
    task_queue_t * join_queue;
#if SKEW_HANDLING
    task_queue_t * skew_queue;
    const int thresh1 = MAX((1<<D), (1<<R)) * THRESHOLD1(args->nthreads);
#endif

    uint32_t * outputR = (uint32_t *) calloc((fanOut+1), sizeof(int32_t));
    uint32_t * outputS = (uint32_t *) calloc((fanOut+1), sizeof(int32_t));
    MALLOC_CHECK((outputR && outputS));

    part_queue = args->part_queue;
    join_queue = args->join_queue;
#if SKEW_HANDLING
    skew_queue = args->skew_queue;
#endif

    args->histR[my_tid] = (uint32_t *) calloc(fanOut, sizeof(int32_t));
    args->histS[my_tid] = (uint32_t *) calloc(fanOut, sizeof(int32_t));

    /* in the first pass, partitioning is done together by all threads */

    args->parts_processed = 0;

#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_initPerformanceMonitor(NULL, NULL);
        PCM_start();
    }
#endif

    /* wait at a barrier until each thread starts and then start the timer */
    BARRIER_ARRIVE(args->barrier, rv);

    /* if monitoring synchronization stats */
    SYNC_TIMERS_START(args, my_tid);

#ifndef NO_TIMING
    if(my_tid == 0){
        /* thread-0 checkpoints the time */
        gettimeofday(&args->start, NULL);
        startTimer(&args->timer1);
        startTimer(&args->timer2);
        startTimer(&args->timer3);
    }
#endif

#ifdef ALGO_TIME
  if (my_tid == 0) {
    gettimeofday(&args->start, NULL);
  }
#endif

#ifdef VTUNE_PROFILE
  if (my_tid == 0) {
    __itt_resume();
  }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    /********** 1st pass of multi-pass partitioning ************/
    part.R       = 0;
    part.D       = NUM_RADIX_BITS / NUM_PASSES;
    part.thrargs = args;
    part.padding = PADDING_TUPLES;

    /* 1. partitioning for relation R */
    part.rel          = args->relR;
    part.tmp          = args->tmpR;
    part.hist         = args->histR;
    part.output       = outputR;
    part.num_tuples   = args->numR;
    part.total_tuples = args->totalR;
    part.relidx       = 0;

#ifdef USE_SWWC_OPTIMIZED_PART
    parallel_radix_partition_optimized(&part);
#else
    parallel_radix_partition(&part);
#endif

    /* 1. partitioning for relation S */
    part.rel          = args->relS;
    part.tmp          = args->tmpS;
    part.hist         = args->histS;
    part.output       = outputS;
    part.num_tuples   = args->numS;
    part.total_tuples = args->totalS;
    part.relidx       = 1;

#ifdef USE_SWWC_OPTIMIZED_PART
    parallel_radix_partition_optimized(&part);
#else
    parallel_radix_partition(&part);
#endif

    /* wait at a barrier until each thread copies out */
    BARRIER_ARRIVE(args->barrier, rv);

    /********** end of 1st partitioning phase ******************/

    /* 3. first thread creates partitioning tasks for 2nd pass */
    if(my_tid == 0) {
    unsigned int parts_in_one_numa = fanOut / args->nthreads;
    for(unsigned int part_idx = 0; part_idx < parts_in_one_numa; part_idx++) {
      for (unsigned int inner = 0; inner < args->nthreads; ++inner) {
        unsigned int i = part_idx + inner * parts_in_one_numa;
        uint32_t ntupR = outputR[i+1] - outputR[i] - PADDING_TUPLES;
        uint32_t ntupS = outputS[i+1] - outputS[i] - PADDING_TUPLES;

#if SKEW_HANDLING
        if(ntupR > thresh1 || ntupS > thresh1){
          DEBUGMSG(1, "Adding to skew_queue= R:%d, S:%d\n", ntupR, ntupS);

          task_t * t = task_queue_get_slot(skew_queue);

          t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
          t->relR.tuples = args->tmpR + outputR[i];
          t->tmpR.tuples = args->relR + outputR[i];

          t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
          t->relS.tuples = args->tmpS + outputS[i];
          t->tmpS.tuples = args->relS + outputS[i];

          task_queue_add(skew_queue, t);
        }
        else
#endif
          if(ntupR > 0 && ntupS > 0) {
            task_t * t = task_queue_get_slot(part_queue);

            t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
            t->relR.tuples = args->tmpR + outputR[i];
                        t->relR.ratio_holes = args->ratio_holes;
            t->tmpR.tuples = args->relR + outputR[i] ;
                        t->tmpR.ratio_holes = args->ratio_holes;

            t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
            t->relS.tuples = args->tmpS + outputS[i];
            t->tmpS.tuples = args->relS + outputS[i];

            task_queue_add(part_queue, t);
          }
      }

      /* debug partitioning task queue */
      DEBUGMSG(1, "Pass-2: # partitioning tasks = %d\n", part_queue->count);
    }
    for (unsigned int i = parts_in_one_numa * args->nthreads; i < fanOut; ++i) {
      uint32_t ntupR = outputR[i+1] - outputR[i] - PADDING_TUPLES;
      uint32_t ntupS = outputS[i+1] - outputS[i] - PADDING_TUPLES;
      if(ntupR > 0 && ntupS > 0) {
        task_t * t = task_queue_get_slot(part_queue);

        t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
        t->relR.tuples = args->tmpR + outputR[i];
                t->relR.ratio_holes = args->ratio_holes;
        t->tmpR.tuples = args->relR + outputR[i];
                t->tmpR.ratio_holes = args->ratio_holes;

        t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
        t->relS.tuples = args->tmpS + outputS[i];
        t->tmpS.tuples = args->relS + outputS[i];

        task_queue_add(part_queue, t);
      }
    }
  }

  SYNC_TIMER_STOP(&args->localtimer.sync3);
  /* wait at a barrier until first thread adds all partitioning tasks */
  BARRIER_ARRIVE(args->barrier, rv);
  /* global barrier sync point-3 */
  SYNC_GLOBAL_STOP(&args->globaltimer->sync3, my_tid);

  /************ 2nd pass of multi-pass partitioning ********************/
    /* 4. now each thread further partitions and add to join task queue **/

    if (NUM_PASSES==1) {
        /* If the partitioning is single pass we directly add tasks from pass-1 */
        task_queue_t *swap = join_queue;
        join_queue = part_queue;
        /* part_queue is used as a temporary queue for handling skewed parts */
        part_queue = swap;
    }
    else if (NUM_PASSES == 2) {
        while ((task = task_queue_get_atomic(part_queue))) {
            serial_radix_partition(task, join_queue, R, D);
        }
    }

    BARRIER_ARRIVE(args->barrier, rv);
  if (my_tid == 0)
    gettimeofday(&args->part_end, NULL);
#ifdef VTUNE_PROFILE
  if (my_tid == 0) {
    __itt_pause();
    usleep(50000);
  }
#endif

    BARRIER_ARRIVE(args->barrier, rv);
#if SKEW_HANDLING
    /* Partitioning pass-2 for skewed relations */
    part.R         = R;
    part.D         = D;
    part.thrargs   = args;
    part.padding   = SMALL_PADDING_TUPLES;

    while(1) {
        if(my_tid == 0) {
            *args->skewtask = task_queue_get_atomic(skew_queue);
        }
        BARRIER_ARRIVE(args->barrier, rv);
        if( *args->skewtask == NULL)
            break;

        DEBUGMSG((my_tid==0), "Got skew task = R: %d, S: %d\n",
                 (*args->skewtask)->relR.num_tuples,
                 (*args->skewtask)->relS.num_tuples);

        int32_t numperthr = (*args->skewtask)->relR.num_tuples / args->nthreads;
        const int fanOut2 = (1 << D);

        free(outputR);
        free(outputS);

        outputR = (int32_t*) calloc(fanOut2 + 1, sizeof(int32_t));
        outputS = (int32_t*) calloc(fanOut2 + 1, sizeof(int32_t));

        free(args->histR[my_tid]);
        free(args->histS[my_tid]);

        args->histR[my_tid] = (int32_t*) calloc(fanOut2, sizeof(int32_t));
        args->histS[my_tid] = (int32_t*) calloc(fanOut2, sizeof(int32_t));

        /* wait until each thread allocates memory */
        BARRIER_ARRIVE(args->barrier, rv);

        /* 1. partitioning for relation R */
        part.rel          = (*args->skewtask)->relR.tuples + my_tid * numperthr;
        part.tmp          = (*args->skewtask)->tmpR.tuples;
        part.hist         = args->histR;
        part.output       = outputR;
        part.num_tuples   = (my_tid == (args->nthreads-1)) ?
                            ((*args->skewtask)->relR.num_tuples - my_tid * numperthr)
                            : numperthr;
        part.total_tuples = (*args->skewtask)->relR.num_tuples;
        part.relidx       = 2; /* meaning this is pass-2, no syncstats */
        parallel_radix_partition(&part);

        numperthr = (*args->skewtask)->relS.num_tuples / args->nthreads;
        /* 2. partitioning for relation S */
        part.rel          = (*args->skewtask)->relS.tuples + my_tid * numperthr;
        part.tmp          = (*args->skewtask)->tmpS.tuples;
        part.hist         = args->histS;
        part.output       = outputS;
        part.num_tuples   = (my_tid == (args->nthreads-1)) ?
                            ((*args->skewtask)->relS.num_tuples - my_tid * numperthr)
                            : numperthr;
        part.total_tuples = (*args->skewtask)->relS.num_tuples;
        part.relidx       = 2; /* meaning this is pass-2, no syncstats */
        parallel_radix_partition(&part);

        /* wait at a barrier until each thread copies out */
        BARRIER_ARRIVE(args->barrier, rv);

        /* first thread adds join tasks */
        if(my_tid == 0) {
            const int THR1 = THRESHOLD1(args->nthreads);

            for(i = 0; i < fanOut2; i++) {
                int32_t ntupR = outputR[i+1] - outputR[i] - SMALL_PADDING_TUPLES;
                int32_t ntupS = outputS[i+1] - outputS[i] - SMALL_PADDING_TUPLES;
                if(ntupR > THR1 || ntupS > THR1){

                    DEBUGMSG(1, "Large join task = R: %d, S: %d\n", ntupR, ntupS);

                    /* use part_queue temporarily */
                    for(int k=0; k < args->nthreads; k++) {
                        int ns = (k == args->nthreads-1)
                                 ? (ntupS - k*(ntupS/args->nthreads))
                                 : (ntupS/args->nthreads);
                        task_t * t = task_queue_get_slot(part_queue);

                        t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                        t->relR.tuples = (*args->skewtask)->tmpR.tuples + outputR[i];
                        t->tmpR.tuples = (*args->skewtask)->relR.tuples + outputR[i];

                        t->relS.num_tuples = t->tmpS.num_tuples = ns; //ntupS;
                        t->relS.tuples = (*args->skewtask)->tmpS.tuples + outputS[i] //;
                                         + k*(ntupS/args->nthreads);
                        t->tmpS.tuples = (*args->skewtask)->relS.tuples + outputS[i] //;
                                         + k*(ntupS/args->nthreads);

                        task_queue_add(part_queue, t);
                    }
                }
                else
                if(ntupR > 0 && ntupS > 0) {
                    task_t * t = task_queue_get_slot(join_queue);

                    t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                    t->relR.tuples = (*args->skewtask)->tmpR.tuples + outputR[i];
                    t->tmpR.tuples = (*args->skewtask)->relR.tuples + outputR[i];

                    t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
                    t->relS.tuples = (*args->skewtask)->tmpS.tuples + outputS[i];
                    t->tmpS.tuples = (*args->skewtask)->relS.tuples + outputS[i];

                    task_queue_add(join_queue, t);

                    DEBUGMSG(1, "Join added = R: %d, S: %d\n",
                           t->relR.num_tuples, t->relS.num_tuples);
                }
            }

        }
    }

    /* add large join tasks in part_queue to the front of the join queue */
    if(my_tid == 0) {
        while((task = task_queue_get_atomic(part_queue)))
            task_queue_add(join_queue, task);
    }

#endif

    free(outputR);
    free(outputS);

    SYNC_TIMER_STOP(&args->localtimer.sync4);
    /* wait at a barrier until all threads add all join tasks */
    BARRIER_ARRIVE(args->barrier, rv);
#ifdef VTUNE_PROFILE
  if (my_tid == 0) {
    __itt_resume();
  }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    BARRIER_ARRIVE(args->barrier, rv);
    /* global barrier sync point-4 */
    SYNC_GLOBAL_STOP(&args->globaltimer->sync4, my_tid);

#ifndef NO_TIMING
    if(my_tid == 0) stopTimer(&args->timer3);/* partitioning finished */
#endif

    DEBUGMSG((my_tid == 0), "Number of join tasks = %d\n", join_queue->count);

#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_stop();
        PCM_log("======= Partitioning phase profiling results ======\n");
        PCM_printResults();
        PCM_start();
    }
    /* Just to make sure we get consistent performance numbers */
    BARRIER_ARRIVE(args->barrier, rv);
#endif
  uint64_t checksum = 0;
  join_result_t jres;
    while((task = task_queue_get_atomic(join_queue))){
        /* do the actual join. join method differs for different algorithms,
           i.e. bucket chaining, histogram-based, histogram-based with simd &
           prefetching  */
        jres = args->join_function(&task->relR, &task->relS, &task->tmpR,args->totalR);
    results += jres.matches;
    checksum += jres.checksum;

        args->parts_processed ++;
    }

    args->result = results;
  args->checksum = checksum;
    /* this thread is finished */
    SYNC_TIMER_STOP(&args->localtimer.finish_time);

#ifdef ALGO_TIME
    BARRIER_ARRIVE(args->barrier, rv);
  if (my_tid == 0) {
    gettimeofday(&args->end, NULL);
#ifdef VTUNE_PROFILE
    __itt_pause();
#endif
  }
    BARRIER_ARRIVE(args->barrier, rv);
#endif
#ifndef NO_TIMING
    /* this is for just reliable timing of finish time */
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        /* Actually with this setup we're not timing build */
        stopTimer(&args->timer2);/* build finished */
        stopTimer(&args->timer1);/* probe finished */
        gettimeofday(&args->end, NULL);
    }
#endif

    /* global finish time */
    SYNC_GLOBAL_STOP(&args->globaltimer->finish_time, my_tid);

#ifdef PERF_COUNTERS
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("=========== Build+Probe profiling results =========\n");
        PCM_printResults();
        PCM_log("===================================================\n");
        PCM_cleanup();
    }
    /* Just to make sure we get consistent performance numbers */
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    return 0;
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


/**
 * The template function for different joins: Basically each parallel radix join
 * has a initialization step, partitioning step and build-probe steps. All our
 * parallel radix implementations have exactly the same initialization and
 * partitioning steps. Difference is only in the build-probe step. Here are all
 * the parallel radix join implemetations and their Join (build-probe) functions:
 *
 * - PRO,  Parallel Radix Join Optimized --> bucket_chaining_join()
 * - PRH,  Parallel Radix Join Histogram-based --> histogram_join()
 * - PRHO, Parallel Radix Histogram-based Optimized -> histogram_optimized_join()
 */
template<int NUM_RADIX_BITS , int NUM_PASSES >
static
join_result_t
join_init_run(relation_t * relR, relation_t * relS, JoinFunction jf,unsigned int nthreads)
{
    int  rv;
    pthread_t tid[nthreads];
    pthread_attr_t attr;
    pthread_barrier_t barrier;
    cpu_set_t set;
    arg_t args[nthreads];

    uint32_t ** histR, ** histS;
    tuple_t * tmpRelR, * tmpRelS;
    int32_t numperthr[2];
    uint64_t result = 0;
  uint64_t checksum = 0;

    task_queue_t * part_queue, * join_queue;
#if SKEW_HANDLING
    task_queue_t * skew_queue;
    task_t * skewtask = NULL;
    skew_queue = task_queue_init(FANOUT_PASS1);
#endif
    part_queue = task_queue_init(FANOUT_PASS1);
    join_queue = task_queue_init((1<<NUM_RADIX_BITS));


    /* allocate temporary space for partitioning */
// #ifdef HUGE_PAGE
//     tmpRelR = (tuple_t*) Utils::malloc_huge(relR->num_tuples * sizeof(tuple_t) +
//                                        RELATION_PADDING);
//     tmpRelS = (tuple_t*) Utils::malloc_huge(relS->num_tuples * sizeof(tuple_t) +
//                                        RELATION_PADDING);
// #else
    tmpRelR = (tuple_t*) alloc_aligned(relR->num_tuples * sizeof(tuple_t) +
                                       RELATION_PADDING);
    tmpRelS = (tuple_t*) alloc_aligned(relS->num_tuples * sizeof(tuple_t) +
                                       RELATION_PADDING);
// #endif
    MALLOC_CHECK((tmpRelR && tmpRelS));
    /** Not an elegant way of passing whether we will numa-localize, but this
        feature is experimental anyway. */

    // if (numalocalize) {
    //     numa_localize(tmpRelR, relR->num_tuples + RELATION_PADDING/sizeof(tuple_t), nthreads);
    //     numa_localize(tmpRelS, relS->num_tuples + RELATION_PADDING/sizeof(tuple_t), nthreads);
    // }

    /* allocate histograms arrays, actual allocation is local to threads */
    histR = (uint32_t**) alloc_aligned(nthreads * sizeof(uint32_t*));
    histS = (uint32_t**) alloc_aligned(nthreads * sizeof(uint32_t*));
    MALLOC_CHECK((histR && histS));

    rv = pthread_barrier_init(&barrier, NULL, nthreads);
    if(rv != 0){
        printf("[ERROR] Couldn't create the barrier\n");
        exit(EXIT_FAILURE);
    }

    pthread_attr_init(&attr);


#ifdef SYNCSTATS
    /* thread-0 keeps track of synchronization stats */
    args[0].globaltimer = (synctimer_t*) malloc(sizeof(synctimer_t));
#endif

    /* first assign chunks of relR & relS for each thread */
    numperthr[0] = ((relR->num_tuples / TUPLESPERCACHELINE) / nthreads) * TUPLESPERCACHELINE;
    numperthr[1] = ((relS->num_tuples / TUPLESPERCACHELINE) / nthreads) * TUPLESPERCACHELINE;
    size_t left_over_R = (relR->num_tuples / TUPLESPERCACHELINE) % nthreads;
    size_t left_over_S = (relS->num_tuples / TUPLESPERCACHELINE) % nthreads;

    cout << "Creating Threads" << endl;
    for(unsigned int i = 0; i < nthreads; i++){
        cout << "Bolo" << endl;
        int cpu_idx = get_cpu_id(i);
        cout << "Hello" << endl;
        DEBUGMSG(1, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);

        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);

        size_t small_paddingR = std::min((size_t)i, left_over_R) * TUPLESPERCACHELINE;
        args[i].relR = relR->tuples + i * numperthr[0] + small_paddingR;
        args[i].tmpR = tmpRelR;
        args[i].histR = histR;
        args[i].ratio_holes = relR->ratio_holes;

        size_t small_paddingS = std::min((size_t)i, left_over_S) * TUPLESPERCACHELINE;
        args[i].relS = relS->tuples + i * numperthr[1] + small_paddingS;
        args[i].tmpS = tmpRelS;
        args[i].histS = histS;

        args[i].numR = numperthr[0] + ((i < ((relR->num_tuples/TUPLESPERCACHELINE) % nthreads)) ?
               TUPLESPERCACHELINE: ((i==nthreads-1)?relR->num_tuples%TUPLESPERCACHELINE:0));
        args[i].numS = numperthr[1] + ((i < ((relS->num_tuples/TUPLESPERCACHELINE) % nthreads)) ?
               TUPLESPERCACHELINE: ((i==nthreads-1)?relS->num_tuples%TUPLESPERCACHELINE:0));
        args[i].totalR = relR->num_tuples;
        args[i].totalS = relS->num_tuples;

        args[i].my_tid = i;
        args[i].part_queue = part_queue;
        args[i].join_queue = join_queue;
#if SKEW_HANDLING
        args[i].skew_queue = skew_queue;
        args[i].skewtask   = &skewtask;
#endif
        args[i].barrier = &barrier;
        args[i].join_function = jf;
        args[i].nthreads = nthreads;

        cout << "Launching Threads" << endl;
        rv = pthread_create(&tid[i], &attr, prj_thread<NUM_RADIX_BITS, NUM_PASSES>, (void*)&args[i]);
        if (rv){
            printf("[ERROR] return code from pthread_create() is %d\n", rv);
            exit(-1);
        }
    }

    cout << "Created Threads" << endl;

    /* wait for threads to finish */
    for(unsigned int i = 0; i < nthreads; i++){
        pthread_join(tid[i], NULL);
        result += args[i].result;
        checksum += args[i].checksum;
    }

  uint64_t time_usec = 0;
  uint64_t part_usec = 0;
  uint64_t join_usec = 0;
#ifdef ALGO_TIME
  time_usec = args[0].end.tv_sec * 1000000LLU + args[0].end.tv_usec - args[0].start.tv_sec * 1000000LLU - args[0].start.tv_usec;
  part_usec = diff_usec(&(args[0].start), &(args[0].part_end));
  join_usec = time_usec - part_usec;
#endif

#ifdef SYNCSTATS
/* #define ABSDIFF(X,Y) (((X) > (Y)) ? ((X)-(Y)) : ((Y)-(X))) */
    fprintf(stdout, "TID JTASKS T1.1 T1.1-IDLE T1.2 T1.2-IDLE "\
            "T3 T3-IDLE T4 T4-IDLE T5 T5-IDLE\n");
    for(i = 0; i < nthreads; i++){
        synctimer_t * glob = args[0].globaltimer;
        synctimer_t * local = & args[i].localtimer;
        fprintf(stdout,
                "%d %d %llu %llu %llu %llu %llu %llu %llu %llu "\
                "%llu %llu\n",
                (i+1), args[i].parts_processed, local->sync1[0],
                glob->sync1[0] - local->sync1[0],
                local->sync1[1] - glob->sync1[0],
                glob->sync1[1] - local->sync1[1],
                local->sync3 - glob->sync1[1],
                glob->sync3 - local->sync3,
                local->sync4 - glob->sync3,
                glob->sync4 - local->sync4,
                local->finish_time - glob->sync4,
                glob->finish_time - local->finish_time);
    }
#endif

#ifndef NO_TIMING
    /* now print the timing results: */
    print_timing(args[0].timer1, args[0].timer2, args[0].timer3,
                relS->num_tuples, result,
                &args[0].start, &args[0].end);
#endif

    /* clean up */
    for(unsigned int i = 0; i < nthreads; i++) {
        free(histR[i]);
        free(histS[i]);
    }
    free(histR);
    free(histS);
    task_queue_free(part_queue);
    task_queue_free(join_queue);
#if SKEW_HANDLING
    task_queue_free(skew_queue);
#endif

    free(tmpRelR);
    free(tmpRelS);

#ifdef SYNCSTATS
    free(args[0].globaltimer);
#endif

    join_result_t ret_result = {result, checksum, time_usec, part_usec, join_usec};
    return ret_result;
}

template<bool is_checksum, int NUM_RADIX_BITS, int NUM_PASSES>
join_result_t
PRAiS(relation_t *relR, relation_t *relS, unsigned int nthreads)
{
    FANOUT_PASS1 = (1u << (NUM_RADIX_BITS/NUM_PASSES));
    FANOUT_PASS2 = (1u << (NUM_RADIX_BITS-(NUM_RADIX_BITS/NUM_PASSES)));

    SMALL_PADDING_TUPLES = (3 * CACHE_LINE_SIZE/sizeof(tuple_t));
    PADDING_TUPLES = (SMALL_PADDING_TUPLES*(FANOUT_PASS2+1));

    RELATION_PADDING = (PADDING_TUPLES*FANOUT_PASS1*sizeof(tuple_t));

    return join_init_run<NUM_RADIX_BITS, NUM_PASSES>(relR, relS, array_join<is_checksum, NUM_RADIX_BITS>, nthreads);
}

// template join_result_t PRAiS<true, 1, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 2, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 3, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 4, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 5, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 6, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 7, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 8, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 9, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 10, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 11, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 12, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 13, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 14, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 15, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 16, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 17, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 18, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 8, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 9, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 10, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 11, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 12, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 13, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 14, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 15, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 16, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 17, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<true, 18, 2>(relation_t *relR, relation_t *relS,unsigned int nthreads);
// template join_result_t PRAiS<false, 14, 1>(relation_t *relR, relation_t *relS,unsigned int nthreads);

int main() {
  int nthreads = 2;
  int r_size   = 12800000;
  int s_size   = 12800000;

  cout << "Hello" << endl;

  relation_t relR;
  relation_t relS;

  relR.tuples = (tuple_t*) alloc_aligned(r_size * sizeof(tuple_t));
  relS.tuples = (tuple_t*) alloc_aligned(s_size * sizeof(tuple_t));

  create_relation_pk(&relR, r_size);

  create_relation_fk(&relS, s_size, r_size);

  cout << "In HERE" << endl;

  PRAiS<true, 10, 1>(&relR, &relS, nthreads);

  return 0;
}

