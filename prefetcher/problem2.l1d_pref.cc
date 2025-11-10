#include <cstdint>
#include <queue>
#include <unordered_set>

#include "cache.h"

// L1 data prefetcher
// idea
// buffer 128 prefetched block addresses (store 16 lsb of block addr)
// within last 512 accesses, how many hits?
//

#define MAX_PREFETCH_BUF_SIZE 128
#define INTERVAL 512
#define THRESHOLD 16

uint8_t m_distance;
std::queue<uint16_t> m_prefetch_buf;
std::unordered_set<uint16_t> m_prefetch_set;

uint64_t m_hits;
uint64_t m_time;

void CACHE::l1d_prefetcher_initialize() {
    cout << "CPU " << cpu << " L1D next line prefetcher" << endl;
    m_distance = 0;
    m_prefetch_set.reserve(MAX_PREFETCH_BUF_SIZE);
    m_hits = 0;
    m_time = 0;
}

// what do we want to know from history?
// add to history
//
// hits in interval
// was prefetched before
//

// called when a tag is checked in the cache
// this means (1) data is requested from cache, or (2) tag checked for coherence
// address
// instruction pointer
// cache hit (y/n)
// type (load, write, read for ownership, prefetch, translation)
//
//
//
// potential improvements:
// stride: not super important (blocks handle this?)
// track delta between addr issues from same ip
// aggresiveness (degree)
// distance
//
// track late/not late (on time / used prefetches), accurate/not accurate (used
// prefetches / all prefetches), polluting/not polluting, coverage (prefetch
// misses / all misses)
// can also track # of cycles over 100 L1 accesses. if # of cycles decreases
// when increasing aggressiveness, its good, otherwise, its bad
//
//
// if increasing aggresiveness does not help, and hits are very low, turn off?
//
//
// plan:
// identify several potentially useful metrics. write code to track them.
// see if they are useful (try all combinations).
//
//
// Timeliness is the percentage of useful prefetches that are timely (arrive
// before use). Track two values:
// 1. The number of useful prefetches. Maintain buffer of issued prefetches. On
// cache access, increment if accessed cache line is in buffer.
// 2. The number of timely prefetches. On cache access, increment if accessed
// cache line was prefetched.
//
// Accuracy is the percentage of useful prefetches. Track two values:
// 1. The number of useful prefetches. See above.
// 2. The number of prefetches. On prefetch issue, increment.
//
// Coverage is the percentage of potential misses that are avoided with
// prefetching. Track two values:
// 1. The number of misses. On cache miss, increment.
// 2. The number of misses avoided via prefetching. Maintain buffer of issued
// prefetches. On cache access, increment if accessed cache line is in buffer.
//
// Instructions per access is the average number of cycles per cache
// access. Track two values:
// 1. The number of cycles. On cycle, increment.
// 2. The number of cache accesses. On cache access, increment.
//
// Access coverage is the percentage of accesses that are prefetched. Track two
// values:
// 1. The number of accesses. On cache access, increment.
// 2. The number of prefetched accesses. Maintain buffer of issued prefetches.
// On cache access, increment if in buffer.
//
// Pollution is the percentage of demand misses caused by the prefetcher. Track
// two values:
// 1. The number of demand misses. On cache miss, increment.
// 2. The number of demand misses due to the prefetcher. Maintain a Bloom
// filter. On cache fill, set the bit corresponding to the evicted address to 1
// if the filled address was prefetched. Otherwise, set the bit to 0. The number
// of demand misses is the total number of 1s.
// we really would like to know if the evicted address was prefetched. if it
// was, it shouldn't count evict pre, fill pre: evicted a prefetched for a
// prefetched (don't care) evict, fill pre: evicted a normal for a prefetched
// (bad if normal hit again) evict pre, fill: evicted a prefetched for a normal
// (don't care) evict, fill: evicted a normal for a normal (don't care)
//
// we know if the fill is a prefetch or not
// we dont know if the evict is a prefetch or not
// suppose we did
//
// idek....

// Do not prefetch writes?. we want to focus on minimizing load misses
// writes are buffered already
//
// need to track stream direction (up or down)
// need to factor by stream (on IP or by block?)
//
// high acc: late -> increase, not late -> decrease/nothing
// med acc: late -> increase
// low acc: decrease
//
// distance should be exponential?
//

void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t ip,
                                   uint8_t cache_hit, uint8_t type) {
    uint64_t cl = addr >> LOG2_BLOCK_SIZE;
    bool is_already_prefetched = m_prefetch_set.count(cl);
    if (is_already_prefetched) {
        ++m_hits;
    }
    ++m_time;
    if (m_time == INTERVAL) {
        if (m_hits >= THRESHOLD) {
            if (m_distance < 128) ++m_distance;
        } else {
            if (m_distance > 0) --m_distance;
        }
    }
    if (is_already_prefetched) {
        return;
    }

    uint64_t pf_cl = cl + 1 + m_distance;
    uint64_t pf_addr = pf_cl << LOG2_BLOCK_SIZE;

    // Maintain queue of prefetched cache lines.
    m_prefetch_buf.push(pf_cl);
    m_prefetch_set.insert(pf_cl);
    if (m_prefetch_buf.size() > MAX_PREFETCH_BUF_SIZE) {
        m_prefetch_set.erase(m_prefetch_buf.front());
        m_prefetch_buf.pop();
    }

    prefetch_line(ip, addr, pf_addr, FILL_L1, 0);
}

// called when a miss is filled in the cache
// address
// set, way of fill (way is # ways if bypass)
// prefetch (if addr was generated by prefetcher)
// evicted_addr addr of evicted block
void CACHE::l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way,
                                      uint8_t prefetch, uint64_t evicted_addr,
                                      uint32_t metadata_in) {}

void CACHE::l1d_prefetcher_final_stats() {
    cout << "CPU " << cpu << " L1D next line prefetcher final stats" << endl;
}
