// SPDX-License-Identifier: MIT
#include <relacy/relacy.hpp>

// Model the moodycamel explicit-producer block reuse handshake with two slots.
// The initial block is full and each consumer owns one previously claimed slot.
template <bool Acquire>
struct slot_reuse : rl::test_suite<slot_reuse<Acquire>, 3,
    Acquire ? rl::test_result_success : rl::test_result_data_race> {
    rl::atomic<bool> empty[2];
    rl::var<int> slots[2];

    void before() {
        for (unsigned i = 0; i < 2; ++i) {
            slots[i]($) = 1;
            empty[i].store(false, rl::mo_relaxed, $);
        }
    }

    void thread(unsigned index) {
        if (index < 2) {
            RL_ASSERT(slots[index]($) == 1);
            empty[index].store(true, rl::mo_release, $);
        } else if (empty[0].load(rl::mo_relaxed, $) &&
                   empty[1].load(rl::mo_relaxed, $)) {
            if constexpr (Acquire) {
                rl::atomic_thread_fence(rl::mo_acquire, $);
            }
            slots[0]($) = 2;
            slots[1]($) = 2;
        }
    }
};

int main() {
    rl::test_params with_fence;
    with_fence.search_type = rl::fair_full_search_scheduler_type;
    rl::test_params without_fence;
    without_fence.search_type = rl::fair_full_search_scheduler_type;
    const bool synchronized = rl::simulate<slot_reuse<true>>(with_fence);
    const bool detected_race = rl::simulate<slot_reuse<false>>(without_fence);
    return synchronized && detected_race ? 0 : 1;
}
