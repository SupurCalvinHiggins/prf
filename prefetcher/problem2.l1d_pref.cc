#include <array>
#include <cstdint>

#include "cache.h"
#include "saturating_counter.h"

using namespace cnt;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

// Idea: Do not prefetch writes. We want to focus on minimizing load misses
// writes are buffered.

// We should combine these with IP-based prefetcher. Also need a global fallback
// prefetcher.

// Idea: we should keep more history (e.g. old cache lines, bitset of hit
// lines) so we can estimate the performance of current, higher and lower
// aggression levels, and then switch to the better one.

// Timeliness   : num_timely / num_useful
// Accuracy     : num_useful / num_issued
//
// Too far ahead, too many prefetched           : Timely, not accurate
// Too far ahead, just right prefetched         : Timely, accurate
// Too far ahead, too few prefetched            : Timely, accurate
// Just right distance, too many prefetched     : Timely, not accurate
// Just right distance, just right prefetched   : Timely, accurate
// Just right distance, too few prefetched      : Timely, accurate
// Too far behind, too many prefetched          : Not timely, not accurate
// Too far behind, just right prefetched        : Not timely, accurate
// Too far behind, too few prefetched           : Not timely, accurate
//
// If not accurate, decrease degree
// If not timely, increase distance
// If accurate, increase degree (OK-ish if we have to revert)
// If timely, decrease distance (kind of bad if we have to revert)

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

struct PrefetchStream {
    // Stream eviction.
    saturating_counter<0, 3> useful{
        0};  // Usefulness of the stream. Incremented for each period where
             // the stream is useful. Decrement otherwise.

    bool allocated;  // Whether the stream is allocated.

    // Stream state.
    u64 last_cache_line;  // Last cache line.
    bool
        direction;  // Whether the stream has increasing cache line addresses
                    // or decreasing. Used to prefetch in the correct direction.

    // Stream throttling.
    saturating_counter<0, 2> distance{
        0};  // Prefetcher distance. Real values are 4, 16, and 64
    saturating_counter<0, 2> degree{
        0};  // Prefetcher degree. Real values are 1, 2 and 4.

    saturating_counter<0, 2> last_distance{0};  // Last prefetcher distance.
    saturating_counter<0, 2> last_degree{0};    // Last prefetcher degree.
    bool is_issued(u64 cache_line) const noexcept {
        u64 real_distance[3] = {4, 16, 64};
        u64 real_degree[3] = {1, 2, 4};
        auto start = last_cache_line + real_distance[distance.value()];
        auto end = last_cache_line + real_distance[distance.value()] +
                   real_degree[degree.value()];
        return cache_line >= start && cache_line < end;
    };

    saturating_counter<0, 512> num_issued{0};  // Number of prefetches issued by
                                               // stream within a time interval.
    saturating_counter<0, 512> num_useful{
        0};  // Number of useful prefetches issued by stream within a
             // time interval. A prefetch is useful if the prefetched
             // line is accessed in cache regardless of whether the
             // prefetched line has made it to cache.

    saturating_counter<0, 512> num_timely{
        0};  // Number of timely prefetches issued by stream within a
             // time interval. A prefetch is timely if the prefetch line
             // is a cache hit.

    // on_cache_fill: called with cache line on cache fill
    // on_cache_access: called with cache line, y/n hit, type, on tag check
    // on_period_end:
};

struct PrefetchStreamCandidate {
    // Candidate eviction.
    bool lru;
    bool allocated;  // Whether the candidate is allocated.

    // Candidate state.
    u64 first_cache_line;     // First cache line.
    i16 cache_line_delta[2];  // Offset of nearby cache line accesses relative
                              // to the first cache line (within +-16 cache
                              // lines). Used to train direction.
};

std::array<PrefetchStream, 32> m_prefetch_streams;
std::array<PrefetchStreamCandidate, 32> m_prefetch_stream_candidates;

void CACHE::l1d_prefetcher_initialize() {
    cout << "CPU " << cpu << " L1D next line prefetcher" << endl;
}

// called when a tag is checked in the cache
// this means (1) data is requested from cache, or (2) tag checked for coherence
// address
// instruction pointer
// cache hit (y/n)
// type (load, write, read for ownership, prefetch, translation)
void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t ip,
                                   uint8_t cache_hit, uint8_t type) {
    uint64_t cache_line = addr >> LOG2_BLOCK_SIZE;

    for (auto& stream : m_prefetch_streams) {
        if (stream.is_issued(cache_line)) {
            // stream issued cache line
            // update state and issue more
        }
    }

    // check candiates for hit within range
    // only if no hit from any stream

    // maintain global access count, update state of streams on threshold

    uint64_t pf_cl = cl + 1;
    uint64_t pf_addr = pf_cl << LOG2_BLOCK_SIZE;
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
