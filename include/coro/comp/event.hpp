/**
 * @file event.hpp
 * @author JiahuiWang
 * @brief lab4a
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once
#include <atomic>
#include <coroutine>

#include "coro/attribute.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/context.hpp"
#include "coro/detail/container.hpp"
#include "coro/detail/types.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4a, in this part you will build the basic coroutine
 * synchronization component - event by modifing event.hpp and event.cpp. Please ensure
 * you have read the document of lab4a.
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

namespace detail
{
// TODO[lab4a]: Add code that you don't want to use externally in namespace detail
}; // namespace detail

// TODO[lab4a]: This event is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function set() and wait()'s declaration same with example.
template<typename return_type = void>
class event
{
    // Just make compile success
    struct event_awaiter : detail::noop_awaiter
    {
        event* event_ptr;
        context* ctx;
        event_awaiter* next;
        std::coroutine_handle<> parent;

        event_awaiter(event* eptr, context* cur_ctx) : event_ptr(eptr), ctx(cur_ctx), next(nullptr){}
        
        bool await_ready() const noexcept {
            this->ctx->register_wait(1);
            if (event_ptr->is_set()){
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept{
            parent = handle;
            return event_ptr->add_awaiter(this);
        }

        auto await_resume() -> return_type{
            this->ctx->unregister_wait(1);
            return event_ptr->val;
        }
    };

    atomic<detail::awaiter_ptr> cur_state{nullptr};
    return_type val;
    friend struct event_awaiter;

public:
    bool is_set(){
        return cur_state.load() == this;
    }

    bool add_awaiter(event_awaiter* waiter){
        detail::awaiter_ptr old_value = nullptr;
        // 利用 cas 操作确保挂载操作的原子性
        do{
            old_value = cur_state.load(std::memory_order_acquire);
            if (old_value == this){
                waiter->next = nullptr;
                return false;
            }
            waiter->next = static_cast<event_awaiter*>(old_value);
        }
        while (!cur_state.compare_exchange_weak(old_value, waiter, std::memory_order_acq_rel));

        return true;
    }

    auto wait() noexcept -> event_awaiter{
        return event_awaiter{ this, linfo.ctx };
    } // return awaitable

    auto set(const return_type& value) noexcept -> void
    {
        val = value;
        detail::awaiter_ptr ex_state = cur_state.exchange(this);
        if (ex_state != this){
            event_awaiter* h = static_cast<event_awaiter*>(ex_state);
            while (h){
                auto next = h->next;
                h->ctx->submit_task(h->parent);
                h = next;
            }
        }
    }
};

template<>
class event<>{
    // Just make compile success
    struct event_awaiter : detail::noop_awaiter{
        event* event_ptr;
        context* ctx;
        event_awaiter* next;
        std::coroutine_handle<> parent;

        event_awaiter(event* eptr, context* cur_ctx) : event_ptr(eptr), ctx(cur_ctx), next(nullptr){}

        bool await_ready() const noexcept{
            this->ctx->register_wait(1);
            if (event_ptr->is_set()){
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept{
            parent = handle;
            return event_ptr->add_awaiter(this);
        }

        auto await_resume() -> void {
            this->ctx->unregister_wait(1);
            return;
        }
    };

    atomic<detail::awaiter_ptr> cur_state{ nullptr };
    friend struct event_awaiter;

public:
    event(bool initial_state = false) : cur_state(initial_state ? this : nullptr){}
    
    bool is_set(){
        return cur_state.load() == this;
    }

    bool add_awaiter(event_awaiter* waiter){
        detail::awaiter_ptr old_value = nullptr;
        // 利用 cas 操作确保挂载操作的原子性
        do{
            old_value = cur_state.load(std::memory_order_acquire);
            if (old_value == this){
                waiter->next = nullptr;
                return false;
            }
            waiter->next = static_cast<event_awaiter*>(old_value);
        }
        while (!cur_state.compare_exchange_weak(old_value, waiter, std::memory_order_acq_rel));

        return true;
    }

    auto wait() noexcept -> event_awaiter{
        return event_awaiter{ this, linfo.ctx };
    } // return awaitable

    auto set() noexcept -> void{
        detail::awaiter_ptr ex_state = cur_state.exchange(this);
        if (ex_state != this){
            event_awaiter* h = static_cast<event_awaiter*>(ex_state);
            while (h){
                auto next = h->next;
                h->ctx->submit_task(h->parent);
                h = next;
            }
        }
    }
};

/**
 * @brief RAII for event
 *
 */
class event_guard
{
    using guard_type = event<>;

public:
    event_guard(guard_type& ev) noexcept : m_ev(ev) {}
    ~event_guard() noexcept { m_ev.set(); }

private:
    guard_type& m_ev;
};

}; // namespace coro
