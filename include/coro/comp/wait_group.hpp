/**
 * @file wait_group.hpp
 * @author JiahuiWang
 * @brief lab4c
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <coroutine>

#include "coro/detail/types.hpp"
#include "coro/context.hpp"
namespace coro
{
/**
 * @brief Welcome to tinycoro lab4c, in this part you will build the basic coroutine
 * synchronization component��wait_group by modifing wait_group.hpp and wait_group.cpp.
 * Please ensure you have read the document of lab4c.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 *
 * @note lab4 and lab5 are free designed lab, leave the interfaces that the test case will use,
 * and then, enjoy yourself!
 */

class context;

// TODO[lab4c]: This wait_group is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class wait_group
{
    struct event_awaiter {
        wait_group* event_ptr;
        context* ctx;
        event_awaiter* next;
        std::coroutine_handle<> parent;

        event_awaiter(wait_group* eptr, context* cur_ctx) : event_ptr(eptr), ctx(cur_ctx), next(nullptr){}

        bool await_ready() const noexcept{
            this->ctx->register_wait(1);
            if (event_ptr->is_zero()){
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept{
            parent = handle;
            return event_ptr->add_awaiter(this);
        }

        auto await_resume() -> void{
            this->ctx->unregister_wait(1);
            return;
        }
    };

    std::atomic<detail::awaiter_ptr> cur_state;
    std::atomic<int> ref_count;
    friend struct event_awaiter;
    
public:
    bool is_zero(){
        return ref_count.load() == 0;
    }

    explicit wait_group(int count = 0) noexcept : ref_count(count), cur_state(nullptr){}

    auto add(int count) noexcept -> void{
        ref_count.fetch_add(count, memory_order_acq_rel);
    };

    bool add_awaiter(event_awaiter* waiter){
        while (1){
            if (is_zero()){
                return false;
            }
            detail::awaiter_ptr old_val = cur_state.load();
            waiter->next = static_cast<event_awaiter*>(old_val);

            if (cur_state.compare_exchange_weak(old_val, waiter, memory_order_acq_rel, memory_order_relaxed)){
                return true;
            }
        }
    }

    auto done() noexcept -> void{
        int old = ref_count.fetch_sub(1, memory_order_acq_rel);
        if (old == 1){
            detail::awaiter_ptr lhead = cur_state.exchange(nullptr);
            if (lhead != nullptr){
                event_awaiter* h = static_cast<event_awaiter*>(lhead);
                while (h){
                    auto next = h->next;
                    h->ctx->submit_task(h->parent);
                    h = next;
                }
            }
        }
    };

    auto wait() noexcept -> event_awaiter{
        return event_awaiter{ this, linfo.ctx };
    };
};

}; // namespace coro
