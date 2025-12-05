#pragma once

#include <algorithm>
#include <array>
#include <queue>
#include <unordered_map>

#include "prefetcher/int.hpp"
#include "prefetcher/optional.hpp"
#include "prefetcher/saturating_counter.hpp"

// When timeliness is below this value, increase distance.
#define TIMELINESS_BOOST_THRESHOLD 0.4

// When accuracy is above this value, increase degree.
#define ACCURACY_BOOST_THRESHOLD 0.8
// When accuracy is below this value, decrease degree and distance.
#define ACCURACY_THROTTLE_THRESHOLD 0.4

#define ISSUE_QUEUE_SIZE 512

template <usize N>
class Streams {
    using Stream = usize;

    class IssueQueue {
        std::unordered_map<u64, bool> m_filled;
        std::unordered_map<u64, Optional<Stream>> m_issued;
        std::queue<u64> m_issued_queue;

       public:
        void push(u64 cache_line, Stream stream) {
            if (m_issued_queue.size() == ISSUE_QUEUE_SIZE) {
                m_issued.erase(m_issued_queue.front());
                m_filled.erase(m_issued_queue.front());
                m_issued_queue.pop();
            }
            m_issued_queue.push(cache_line);
            m_filled[cache_line] = false;
            m_issued[cache_line] = stream;
        }

        Optional<Stream> find(u64 cache_line) {
            if (!m_issued.count(cache_line)) return {};
            return {m_issued[cache_line]};
        }

        void fill(u64 cache_line) {
            if (!m_filled.count(cache_line)) return;
            m_filled[cache_line] = true;
        }

        bool is_filled(u64 cache_line) {
            if (!m_filled.count(cache_line)) return false;
            return m_filled[cache_line];
        }

        void invalidate(Stream stream) {
            for (auto& kv : m_issued)
                if (kv.second.has_value() && kv.second.value() == stream)
                    kv.second = {};
        }
    };

    // Stream eviction.
    std::array<saturating_counter<0, 3>, N>
        m_useful{};  // Usefulness of the stream. Incremented for each period
                     // where the stream is useful. Decrement otherwise.

    std::array<bool, N> m_allocated{};  // Whether the stream is allocated.

    // Stream state.
    std::array<u64, N> m_last_cache_line{};  // Last cache line.
    std::array<bool, N>
        m_direction{};  // Whether the stream has increasing cache
                        // line addresses or decreasing. Used to
                        // prefetch in the correct direction.

    // Stream throttling.
    std::array<saturating_counter<0, 2>, N>
        m_distance{};  // Prefetcher distance. Real values are 4, 16, and 64
    std::array<saturating_counter<0, 2>, N>
        m_degree{};  // Prefetcher degree. Real values are 1, 2 and 4.

    // TODO: Comment.
    IssueQueue m_issued{};

    std::array<saturating_counter<0, 511>, N>
        m_num_issued{};  // Number of prefetches issued by
                         // stream within a time interval.
    std::array<saturating_counter<0, 511>, N>
        m_num_useful{};  // Number of useful prefetches issued by stream within
                         // a time interval. A prefetch is useful if the
                         // prefetched line is accessed in cache regardless of
                         // whether the prefetched line has made it to cache.

    std::array<saturating_counter<0, 511>, N>
        m_num_timely{};  // Number of timely prefetches issued by stream within
                         // a time interval. A prefetch is timely if the
                         // prefetch line is a cache hit.

    // TODO: Comment.
    saturating_counter<0, 511> m_num_access{};

    // mark a stream as deallocated
    void deallocate(Stream stream) {
        m_allocated[stream] = false;
        m_issued.invalidate(stream);
    }

    // allocate a stream with cache line and direction
    Stream allocate(u64 cache_line, bool direction) {
        Stream stream;
        auto it = std::find(m_allocated.begin(), m_allocated.end(), 0);
        if (it != m_allocated.end()) {
            stream = std::distance(m_allocated.begin(), it);
        } else {
            // TODO: Randomize index.
            auto deallocate_it =
                std::min_element(m_useful.begin(), m_useful.end());
            stream = std::distance(m_useful.begin(), deallocate_it);
            deallocate(stream);
        }

        assert(!m_allocated[stream]);
        m_useful[stream] = 0;
        m_allocated[stream] = 0;
        m_useful[stream] = 1;
        m_last_cache_line[stream] = 0;
        m_direction[stream] = direction;
        m_distance[stream] = 0;
        m_degree[stream] = 0;
        m_last_cache_line[stream] = cache_line;
        m_num_issued[stream] = 0;
        m_num_useful[stream] = 0;
        m_num_timely[stream] = 0;

        return stream;
    }

    // find a stream with cache line and direction, allocating if needed
    Stream find_or_allocate(u64 cache_line, bool direction) {
        auto stream = m_issued.find(cache_line);
        if (stream.has_value() && m_allocated[stream.value()] &&
            m_direction[stream.value()] == direction)
            return stream.value();
        return allocate(cache_line, direction);
    }

    // issue prefetches from stream TODO
    std::vector<u64> prefetch(Stream stream, u64 cache_line) {
        std::vector<u64> pf_cache_lines;

        auto distance = 1 << (2 * (m_distance[stream] + 1));
        auto degree = 1 << m_degree[stream];
        auto direction = m_direction[stream] ? 1 : -1;

        for (int i = 1; i <= degree; ++i) {
            u64 pf_cache_line = cache_line + (distance + i) * direction;
            if (m_issued.find(pf_cache_line).has_value()) continue;
            pf_cache_lines.push_back(pf_cache_line);
            m_issued.push(cache_line, stream);
            ++m_num_issued[stream];
            m_last_cache_line[stream] = pf_cache_line;
        }

        return pf_cache_lines;
    }

   public:
    // Issue prefetches from stream.
    // returns true iff stream issued prefetches
    [[nodiscard]] std::vector<u64> prefetch(u64 cache_line) {
        auto stream = m_issued.find(cache_line);
        if (!stream.has_value()) return {};
        ++m_num_useful[stream.value()];
        if (m_issued.is_filled(cache_line)) ++m_num_timely[stream.value()];
        return prefetch(stream.value(), cache_line);
    }

    // allocate a new stream based on hint
    [[nodiscard]] std::vector<u64> allocate_and_prefetch(u64 cache_line,
                                                         bool direction) {
        auto stream = find_or_allocate(cache_line, direction);
        return prefetch(stream, cache_line);
    }

    void train() {
        ++m_num_access;
        if (m_num_access != m_num_access.max()) return;

        m_num_access = 0;
        for (Stream stream = 0; stream < N; ++stream) {
            auto& num_useful = m_num_useful[stream];
            auto& num_timely = m_num_timely[stream];
            auto& num_issued = m_num_issued[stream];
            double timeliness =
                num_useful != 0 ? (double)num_timely / num_useful : 0.0;
            double accuracy =
                num_issued != 0 ? (double)num_useful / num_issued : 0.0;

            if (timeliness <= TIMELINESS_BOOST_THRESHOLD) ++m_distance[stream];

            if (accuracy <= ACCURACY_THROTTLE_THRESHOLD) {
                --m_degree[stream];
                --m_distance[stream];
            } else if (accuracy >= ACCURACY_BOOST_THRESHOLD) {
                ++m_degree[stream];
            }

            if (accuracy >= ACCURACY_THROTTLE_THRESHOLD &&
                (int)num_useful >= (int)m_num_access.max() / (int)(2 * N)) {
                ++m_useful[stream];
            } else {
                --m_useful[stream];
            }

            // TODO: Should we use the issue queue to maintain these?
            num_useful = 0;
            num_timely = 0;
            num_issued = 0;
        }
    }

    void fill(u64 cache_line) { m_issued.fill(cache_line); }
};
