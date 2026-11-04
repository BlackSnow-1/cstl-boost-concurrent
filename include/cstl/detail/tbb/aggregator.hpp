/*
    Copyright (c) 2005-2022 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


// CSTL port (2026-11-04): standard atomic polling/backoff, no ITT runtime.
#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
namespace cstl::detail::tbb_port {
template <class Atomic, class Value>
void spin_wait_while_eq(const Atomic& value, Value expected) noexcept {
    unsigned n = 0;
    while (value.load(std::memory_order_acquire) == expected) {
        if (++n % 64 == 0) std::this_thread::yield();
        else std::atomic_signal_fence(std::memory_order_seq_cst);
    }
}
template <class Atomic, class Value>
void spin_wait_until_eq(const Atomic& value, Value expected) noexcept {
    unsigned n = 0;
    while (value.load(std::memory_order_acquire) != expected) {
        if (++n % 64 == 0) std::this_thread::yield();
        else std::atomic_signal_fence(std::memory_order_seq_cst);
    }
}
// Base class for aggregated operation
template <typename Derived>
class aggregated_operation {
public:
    // Zero value means "wait" status, all other values are "user" specified values and
    // are defined into the scope of a class which uses "status"
    std::atomic<uintptr_t> status;

    std::atomic<Derived*> next;
    aggregated_operation() : status{}, next(nullptr) {}
}; // class aggregated_operation

// Aggregator base class
/* An aggregator for collecting operations coming from multiple sources and executing
   them serially on a single thread.  OperationType must be derived from
   aggregated_operation. The parameter HandlerType is a functor that will be passed the
   list of operations and is expected to handle each operation appropriately, setting the
   status of each operation to non-zero. */
template <typename OperationType>
class aggregator_generic {
public:
    aggregator_generic() : pending_operations(nullptr), handler_busy(false) {}

    // Execute an operation
    /* Places an operation into the waitlist (pending_operations), and either handles the list,
       or waits for the operation to complete, or returns.
       The long_life_time parameter specifies the life time of the given operation object.
       Operations with long_life_time == true may be accessed after execution.
       A "short" life time operation (long_life_time == false) can be destroyed
       during execution, and so any access to it after it was put into the waitlist,
       including status check, is invalid. As a consequence, waiting for completion
       of such operation causes undefined behavior. */
    template <typename HandlerType>
    void execute( OperationType* op, HandlerType& handle_operations, bool long_life_time = true ) {
        // op->status should be read before inserting the operation into the
        // aggregator waitlist since it can become invalid after executing a
        // handler (if the operation has 'short' life time.)
        const uintptr_t status = op->status.load(std::memory_order_relaxed);

        // ITT note: &(op->status) tag is used to cover accesses to this op node. This
        // thread has created the operation, and now releases it so that the handler
        // thread may handle the associated operation w/o triggering a race condition;
        // thus this tag will be acquired just before the operation is handled in the
        // handle_operations functor.

        // insert the operation in the queue.
        OperationType* res = pending_operations.load(std::memory_order_relaxed);
        do {
            op->next.store(res, std::memory_order_relaxed);
        } while (!pending_operations.compare_exchange_strong(res, op));
        if (!res) { // first in the list; handle the operations
            // ITT note: &pending_operations tag covers access to the handler_busy flag,
            // which this waiting handler thread will try to set before entering
            // handle_operations.

            start_handle_operations(handle_operations);
            // The operation with 'short' life time can already be destroyed
            if (long_life_time)
                assert(op->status.load(std::memory_order_relaxed));
        }
        // Not first; wait for op to be ready
        else if (!status) { // operation is blocking here.
            assert(long_life_time);

            spin_wait_while_eq(op->status, uintptr_t(0));
        }
   }

private:
    // Trigger the handling of operations when the handler is free
    template <typename HandlerType>
    void start_handle_operations( HandlerType& handle_operations ) {
        OperationType* op_list;

        // ITT note: &handler_busy tag covers access to pending_operations as it is passed
        // between active and waiting handlers.  Below, the waiting handler waits until
        // the active handler releases, and the waiting handler acquires &handler_busy as
        // it becomes the active_handler. The release point is at the end of this
        // function, when all operations in pending_operations have been handled by the
        // owner of this aggregator.

        // get the handler_busy:
        // only one thread can possibly spin here at a time
        spin_wait_until_eq(handler_busy, uintptr_t(0));

        // acquire fence not necessary here due to causality rule and surrounding atomics
        handler_busy.store(1, std::memory_order_relaxed);

        // ITT note: &pending_operations tag covers access to the handler_busy flag
        // itself. Capturing the state of the pending_operations signifies that
        // handler_busy has been set and a new active handler will now process that list's
        // operations.

        // grab pending_operations
        op_list = pending_operations.exchange(nullptr);

        // handle all the operations
        handle_operations(op_list);

        // release the handler
        handler_busy.store(0, std::memory_order_release);
    }

    // An atomically updated list (aka mailbox) of pending operations
    std::atomic<OperationType*> pending_operations;
    // Controls threads access to handle_operations
    std::atomic<uintptr_t> handler_busy;
}; // class aggregator_generic

template <typename HandlerType, typename OperationType>
class aggregator : public aggregator_generic<OperationType> {
    HandlerType handle_operations;
public:
    aggregator() = default;

    void initialize_handler( HandlerType h ) { handle_operations = h; }

    void execute(OperationType* op) {
        aggregator_generic<OperationType>::execute(op, handle_operations);
    }
}; // class aggregator

// the most-compatible friend declaration (vs, gcc, icc) is
// template<class U, class V> friend class aggregating_functor;
template <typename AggregatingClass, typename OperationList>
class aggregating_functor {
    AggregatingClass* my_object{nullptr};
public:
    aggregating_functor() = default;
    aggregating_functor( AggregatingClass* object ) : my_object(object) {
        assert(my_object);
    }

    void operator()( OperationList* op_list ) {
        assert(my_object);
        my_object->handle_operations(op_list);
    }
}; // class aggregating_functor



} // namespace cstl::detail::tbb_port
