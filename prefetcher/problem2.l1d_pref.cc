#include <algorithm>
#include <array>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "cache.h"
#include "champsim.h"
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

saturating_counter<0, 511> num_access{0};

template <typename T>
class Optional {
    T m_value;
    bool m_has_value;

   public:
    Optional(T&& value) : m_value(value), m_has_value(true) {}
    Optional() : m_has_value(false) {}
    bool has_value() const { return m_has_value; }
    T value() const {
        assert(has_value());
        return m_value;
    }
};

struct Hint {
    u64 last_cache_line;
    bool direction;
    bool strong;
};

template <std::size_t N>
class Streams {
    using Stream = std::size_t;

    struct Issue {
        u64 cache_line;
        Optional<Stream> stream;
    };

    class IssueQueue {
        std::unordered_map<u64, Issue> m_issued;
        std::queue<u64> m_issued_queue;

       public:
        // TODO: this
        void push(u64 cache_line, Stream stream) {}
        Optional<Stream> find(u64 cache_line) {}
        void invalidate(Stream stream) {
            for (auto& [_, issue] : m_issued)
                if (issue.stream == stream) issue.stream = {};
        }
    };

    // Stream eviction.
    std::array<saturating_counter<0, 3>, N>
        m_useful;  // Usefulness of the stream. Incremented for each period
                   // where the stream is useful. Decrement otherwise.

    std::array<bool, N> m_allocated;  // Whether the stream is allocated.

    // Stream state.
    std::array<u64, N> m_last_cache_line;  // Last cache line.
    std::array<bool, N> m_direction;  // Whether the stream has increasing cache
                                      // line addresses or decreasing. Used to
                                      // prefetch in the correct direction.

    // Stream throttling.
    std::array<saturating_counter<0, 2>, N>
        m_distance;  // Prefetcher distance. Real values are 4, 16, and 64
    std::array<saturating_counter<0, 2>, N>
        m_degree;  // Prefetcher degree. Real values are 1, 2 and 4.

    // TODO: comment these
    IssueQueue m_issued;

    std::array<saturating_counter<0, 511>, N>
        m_num_issued;  // Number of prefetches issued by
                       // stream within a time interval.
    std::array<saturating_counter<0, 511>, N>
        m_num_useful;  // Number of useful prefetches issued by stream within a
                       // time interval. A prefetch is useful if the prefetched
                       // line is accessed in cache regardless of whether the
                       // prefetched line has made it to cache.

    std::array<saturating_counter<0, 511>, N>
        m_num_timely;  // Number of timely prefetches issued by stream within a
                       // time interval. A prefetch is timely if the prefetch
                       // line is a cache hit.

    // mark a stream as deallocated
    void deallocate(Stream stream) {
        m_useful[stream] = 0;
        m_allocated[stream] = false;
        m_last_cache_line[stream] = 0;
        m_direction[stream] = 0;
        m_distance[stream] = 0;
        m_degree[stream] = 0;
        m_issued.invalidate(stream);
        m_num_issued[stream] = 0;
        m_num_useful[stream] = 0;
        m_num_timely[stream] = 0;
    }

    // allocate a stream with cache line and direction
    Stream allocate(u64 cache_line, bool direction) {
        Stream stream;
        auto it = std::find(m_allocated.begin(), m_allocated.end(), 0);
        if (it != m_allocated.end()) {
            stream = std::distance(m_distance.begin(), it);
        } else {
            // TODO: Randomize index.
            auto deallocate_it =
                std::min_element(m_useful.begin(), m_useful.end());
            stream = std::distance(m_useful.begin(), deallocate_it);
            deallocate(stream);
        }
        m_allocated[stream] = 0;
        m_useful[stream] = 1;
        m_direction[stream] = direction;
        m_last_cache_line[stream] = cache_line;
        return stream;
    }

    // find a stream with cache line and direction, allocating if needed
    Stream find_or_allocate(u64 cache_line, bool direction) {
        auto stream = m_issued.find(cache_line);
        if (stream.has_value() && m_direction[stream.value()] == direction)
            return stream.value();
        return allocate(cache_line, direction);
    }

    // issue prefetches from stream TODO
    void prefetch(Stream stream, u64 cache_line) {
        auto distance = 1 << (2 * (m_distance[stream] + 1));
        auto degree = 1 << m_degree[stream];
        auto direction = m_direction[stream] ? 1 : -1;

        for (int i = 1; i <= degree; ++i) {
            u64 pf_cache_line = cache_line + (distance + i) * direction;
            if (m_issued.find(pf_cache_line).has_value()) continue;

            prefetch_line(ip, addr, pf_cache_line << LOG2_BLOCK_SIZE, FILL_L1,
                          0);

            m_issued.push(cache_line, stream);
            ++m_num_issued[stream];
            m_last_cache_line[stream] = pf_cache_line;
        }
    }

   public:
    // Issue prefetches from stream.
    // returns true iff stream issued prefetches
    bool prefetch(u64 cache_line) {
        auto stream = m_issued.find(cache_line);
        if (!stream.has_value()) return false;
        ++m_num_useful[stream.value()];
        prefetch(stream.value(), cache_line);
        return true;
    }

    // allocate a new stream based on hint
    void allocate_and_prefetch(u64 cache_line, bool direction) {
        auto stream = find_or_allocate(cache_line, direction);
        prefetch(stream, cache_line);
    }

    // Update streams on period end. TODO
    void train() {}
};

struct StreamCandidate {};

template <std::size_t N>
class Candidates {
    using Candidate = std::size_t;

    // Candidate eviction.
    std::array<bool, N> m_lru;
    std::array<bool, N> m_allocated;  // Whether the candidate is allocated.

    // Candidate state.
    std::array<u64, N> m_cache_line;  // Current cache line.
    std::array<bool, N>
        m_direction;  // Direction of nearby cache line accesses
                      // relative to the first cache line (within
                      // +-16 cache lines).
    std::array<saturating_counter<0, 3>, N>
        m_num_correct;  // Number of times the direction has been correct.

    Optional<Candidate> find(u64 cache_line) {}

    void dellocate(Candidate candidate) {
        // TODO: dellocate
    }

    Candidate allocate(u64 cache_line) {
        Candidate candidate;
        auto it = std::find(m_allocated.begin(), m_allocated.end(), 0);
        if (it != m_allocated.end()) {
            candidate = std::distance(m_allocated.begin(), it);
        } else {
            // TODO: evict something weak, do it.
        }
        // TODO: allocate candidate
    }

    Candidate find_or_allocate(u64 cache_line) {
        auto candidate = find(cache_line);
        if (candidate.has_value()) return candidate.value();
        return allocate(cache_line);
    }

    void train(Candidate candidate, u64 cache_line) {}

   public:
    Hint train(u64 cache_line) {
        auto candidate = find_or_allocate(cache_line);
        train(candidate, cache_line);
        // TODO: if candidate is strong, deallocate it, return strong hint.
    }
};

Streams<32> streams;
std::array<StreamCandidate, 32> stream_candidates;

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
    // A cache line was accessed.
    ++num_access;
    uint64_t cache_line = addr >> LOG2_BLOCK_SIZE;

    // If there is an IP-based stream that prefetched the cache line already,
    // update the stream metrics and issue additional prefetches from the
    // stream.
    // TODO:

    // If there is a stream that prefetched the cache line already, update the
    // stream metrics and issue additional prefetches from the stream.
    auto is_prefetch = streams.prefetch(cache_line);
    if (is_prefetch) goto monitor;

    // If there is an IP-based candidate that can train on the cache line, train
    // it. If the candidate is fully trained, promote it to a full stream,
    // update the stream and issue additional prefetches from the stream.
    // TODO:

    // If there is a candidate that can train on the cache line, train it. If
    // the candiate is fully trained, promote it to a full stream, update the
    // stream and issue additional prefetches from the stream.
    // auto hint_or_allocate = canidates.train(cache_line)
    // if hint_or_allocate is allocate: streams.allocate(opt_candidate);
    // streams.prefetch(cache_line); otherwise use hint

    for (auto& candidate : stream_candidates) {
        // If there is a deallocated candidate, use it.
    handle_deallocated:
        if (!candidate.allocated) {
            candidate.allocated = true;
            candidate.lru = true;
            candidate.cache_line = cache_line;
            candidate.num_correct = 0;
            candidate.direction = 0;
            goto next_line_prefetch_forward;
        }

        // If there is a direction signal for the candidate, use it.
        if ((candidate.cache_line != cache_line &&
             candidate.cache_line - 16 <= cache_line &&
             cache_line <= candidate.cache_line + 16)) {
            // Touch the candidate.
            candidate.lru = true;

            // Compute the direction.
            bool direction = cache_line > candidate.cache_line;

            // If the candidate has no learned direction, set it.
            if (candidate.num_correct == 0) candidate.direction = direction;

            // If the direction does not match the learned direction, reallocate
            // the candidate.
            if (candidate.direction != direction) {
                candidate.allocated = false;
                goto handle_deallocated;
            }

            // Otherwise, the direction matches the learned direction.
            ++candidate.num_correct;

            // If the candidate cannot be promoted yet, use the learned
            // direction as a hint to the next line prefetcher.
            if (candidate.num_correct != candidate.num_correct.max()) {
                if (direction) {
                    goto next_line_prefetch_forward;
                } else {
                    goto next_line_prefetch_backward;
                }
            }

            // TODO: Find a stream, allocate it.
            // Use current cache line, direction.
            streams.allocate_and_prefetch(cache_line, direction);
        }

        // TODO: handle LRU candidate
    }

next_line_prefetch_forward:
    // Fallback to a next-line prefetcher with distance 16.
    prefetch_line(ip, addr, (cache_line + 1 + 16) << LOG2_BLOCK_SIZE, FILL_L1,
                  0);
    goto monitor;

next_line_prefetch_backward:
    // Fallback to a next-line prefetcher with distance 16.
    prefetch_line(ip, addr, (cache_line - 1 - 16) << LOG2_BLOCK_SIZE, FILL_L1,
                  0);
    goto monitor;

monitor:
    // If the monitoring period is over, update the streams.
    if (num_access == num_access.max()) {
        num_access = 0;
        streams.train();
    }
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
