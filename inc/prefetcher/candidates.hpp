#pragma once

#include <algorithm>
#include <array>

#include "prefetcher/int.hpp"
#include "prefetcher/optional.hpp"
#include "prefetcher/saturating_counter.hpp"

struct Hint {
    u64 cache_line;
    bool direction;
    bool useful;

    Hint(u64 cache_line, bool direction, bool useful)
        : cache_line(cache_line), direction(direction), useful(useful) {}
};

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

    Optional<Candidate> find(u64 cache_line) {
        for (Candidate candidate = 0; candidate < N; ++candidate)
            if ((m_allocated[candidate] &&
                 m_cache_line[candidate] != cache_line &&
                 m_cache_line[candidate] - 16 <= cache_line &&
                 cache_line <= m_cache_line[candidate] + 16))
                return candidate;
        return {};
    }

    void deallocate(Candidate candidate) { m_allocated[candidate] = false; }

    Candidate allocate(u64 cache_line) {
        Candidate candidate;
        auto it = std::find(m_allocated.begin(), m_allocated.end(), 0);
        if (it != m_allocated.end()) {
            candidate = std::distance(m_allocated.begin(), it);
        } else {
            // TODO: Randomize eviction.
            auto deallocate_it = std::find(m_lru.begin(), m_lru.end(), 0);
            if (deallocate_it != m_lru.end()) {
                candidate = std::distance(m_lru.begin(), deallocate_it);
            } else {
                std::fill(m_lru.begin(), m_lru.end(), 0);
                candidate = std::rand() % N;
            }
            m_allocated[candidate] = false;
        }

        assert(m_allocated[candidate] == false);
        m_allocated[candidate] = true;
        m_lru[candidate] = true;
        m_cache_line[candidate] = cache_line;
        m_num_correct[candidate] = 0;
        m_direction[candidate] = 0;

        return candidate;
    }

    Candidate find_or_allocate(u64 cache_line) {
        auto candidate = find(cache_line);
        if (candidate.has_value()) return candidate.value();
        return allocate(cache_line);
    }

    Candidate train(Candidate candidate, u64 cache_line) {
        // Touch the candidate
        m_lru[candidate] = true;

        // Compute the direction.
        bool direction = cache_line > m_cache_line[candidate];

        // If the candidate has no learned direction, set it.
        if (m_num_correct[candidate] == 0) m_direction[candidate] = direction;

        // TODO: this is not the right place for this
        // If the direction does not match the learned direction,
        // reallocate the candidate.
        if (m_direction[candidate] != direction) {
            deallocate(candidate);
            candidate = allocate(cache_line);
            return candidate;
        }

        // Otherwise, the direction matches the learned direction.
        ++m_num_correct[candidate];
        return candidate;
    }

   public:
    // train candidate on cache line, return prefetch hint. if hint is strong,
    // deallocate candidiate
    Hint train(u64 cache_line) {
        auto candidate = find_or_allocate(cache_line);
        candidate = train(candidate, cache_line);
        bool useful =
            m_num_correct[candidate] == m_num_correct[candidate].max();
        auto hint =
            Hint(m_cache_line[candidate], m_direction[candidate], useful);
        if (useful) deallocate(candidate);
        return hint;
    }
};
