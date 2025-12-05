#include <cstdint>

#include "cache.h"
#include "champsim.h"
#include "prefetcher/candidates.hpp"
#include "prefetcher/streams.hpp"

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

Streams<32> streams;
Candidates<32> candidates;

void CACHE::l1d_prefetcher_initialize() {
    cout << "CPU " << cpu << " L1D next line prefetcher" << endl;
}

// called when a tag is checked in the cache
// this means (1) data is requested from cache, or (2) tag checked for
// coherence address instruction pointer cache hit (y/n) type (load, write,
// read for ownership, prefetch, translation)
void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t ip,
                                   uint8_t cache_hit, uint8_t type) {
    uint64_t cache_line = addr >> LOG2_BLOCK_SIZE;

    // If there is an IP-based stream that prefetched the cache line
    // already, update the stream metrics and issue additional prefetches
    // from the stream.
    // TODO:

    // If there is a stream that prefetched the cache line already, update
    // the stream metrics and issue additional prefetches from the stream.
    auto pf_cache_lines = streams.prefetch(cache_line);
    for (auto pf_cache_line : pf_cache_lines)
        prefetch_line(ip, addr, pf_cache_line << LOG2_BLOCK_SIZE, FILL_L1, 0);
    if (pf_cache_lines.empty()) {
        // If there is an IP-based candidate that can train on the cache line,
        // train it. If the candidate is fully trained, promote it to a full
        // stream, update the stream and issue additional prefetches from the
        // stream.
        // TODO:

        // If there is a candidate that can train on the cache line, train it.
        // If the candiate is fully trained, promote it to a full stream, update
        // the stream and issue additional prefetches from the stream. auto
        auto hint = candidates.train(cache_line);
        if (hint.useful) {
            auto pf_cache_lines =
                streams.allocate_and_prefetch(hint.cache_line, hint.direction);
            for (auto pf_cache_line : pf_cache_lines)
                prefetch_line(ip, addr, pf_cache_line << LOG2_BLOCK_SIZE,
                              FILL_L1, 0);
        } else {
            auto direction = hint.direction ? 1 : -1;
            auto pf_cache_line = cache_line + 16 * direction;
            prefetch_line(ip, addr, pf_cache_line << LOG2_BLOCK_SIZE, FILL_L1,
                          0);
        }
    }

    // If the monitoring period is over, update the streams.
    streams.train();
}

// called when a miss is filled in the cache
// address
// set, way of fill (way is # ways if bypass)
// prefetch (if addr was generated by prefetcher)
// evicted_addr addr of evicted block
void CACHE::l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way,
                                      uint8_t prefetch, uint64_t evicted_addr,
                                      uint32_t metadata_in) {
    u64 cache_line = addr >> LOG2_BLOCK_SIZE;
    if (prefetch) streams.fill(cache_line);
}

void CACHE::l1d_prefetcher_final_stats() {
    cout << "CPU " << cpu << " L1D next line prefetcher final stats" << endl;
}
