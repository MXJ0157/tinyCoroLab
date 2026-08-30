/**
 * @file mutex.hpp
 * @author JiahuiWang
 * @brief lab4d
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <type_traits>

#include "coro/comp/mutex_guard.hpp"
#include "coro/detail/types.hpp"
#include "coro/context.hpp"
namespace coro
{
/**
 * @brief Welcome to tinycoro lab4d, in this part you will build the basic coroutine
 * synchronization component----mutex by modifing mutex.hpp and mutex.cpp.
 * Please ensure you have read the document of lab4d.
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

// TODO[lab4d]: This mutex is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class mutex
{
public:
    struct mutex_awaiter{
        mutex& mtx;
        context& ctx;
        mutex_awaiter* next;
        std::coroutine_handle<> parent;

        mutex_awaiter(mutex& m, context& c) noexcept : mtx(m), ctx(c), next(nullptr){}

        bool await_ready(){
            this->ctx.register_wait(1);
            return mtx.try_lock();
        }

        bool await_suspend(std::coroutine_handle<> handle){
            parent = handle;
            return this->mtx.lock_or_add(this);
        }

        auto await_resume() -> void{
            this->ctx.unregister_wait(1);
            return;
        }

        //阻塞在mutex上的协程被唤醒之后:提交到任务队列中执行
        //阻塞在condition_variable上的协程被唤醒之后,先拿到锁(如果拿不到再次阻塞在mute上,被唤醒之后已经拿到锁了),然后再次检查条件是否满足
        virtual auto resume() -> void{
            ctx.submit_task(parent);
        }
    };
    // Just make lock_guard() compile success
    struct guard_awaiter : mutex_awaiter
    {
        guard_awaiter(mutex& m, context& c) noexcept : mutex_awaiter(m,c){}
        auto  await_resume() -> detail::lock_guard<mutex>{
            mutex_awaiter::await_resume();
            return detail::lock_guard<mutex>(mtx);
        }
    };

    friend struct guard_awaiter;

private:
    //0:未加锁, 1: 已经加锁但无等待, 其他: 已经加锁并且有其他协程等待
    const detail::awaiter_ptr UNLOCKED = reinterpret_cast<detail::awaiter_ptr>(1);
    const detail::awaiter_ptr LOCKED_NO_WAIT = reinterpret_cast<detail::awaiter_ptr>(0);
    std::atomic<detail::awaiter_ptr> cur_state{ UNLOCKED };

public:
    mutex() noexcept {}
    ~mutex() noexcept {}

    //加锁成功或者添加到mutex的阻塞队列中
    bool lock_or_add(mutex_awaiter* waiter){
        //1: 已经加锁但无等待, 其他: 已经加锁并且有其他协程等待
        detail::awaiter_ptr old_value = nullptr;
        while (1){
            if (try_lock()){
                return false;
            }
            old_value = cur_state.load();
            if (old_value != UNLOCKED){
                waiter->next = static_cast<mutex_awaiter*>(old_value);
                if (cur_state.compare_exchange_weak(old_value, waiter, std::memory_order_acq_rel)){
                    return true;
                }
            }
        }
    }

    auto try_lock() noexcept -> bool{
        auto nonLocked = UNLOCKED;
        return cur_state.compare_exchange_strong(nonLocked, LOCKED_NO_WAIT, std::memory_order_acq_rel);
    }

    auto lock() noexcept -> mutex_awaiter{
        return { *this , local_context()};
    };

    auto unlock() noexcept -> void{
        detail::awaiter_ptr old_value = nullptr, next = nullptr;
        while (1){
            old_value = LOCKED_NO_WAIT;
            if (cur_state.compare_exchange_weak(old_value, UNLOCKED, std::memory_order_acq_rel)){
                return;
            }
            old_value = cur_state.load();
            if (old_value != LOCKED_NO_WAIT){
                next = static_cast<mutex_awaiter*>(old_value)->next;
                if (cur_state.compare_exchange_weak(old_value, next, std::memory_order_acq_rel)){
                    auto h = static_cast<mutex_awaiter*>(old_value);
                    h->resume();
                    return;
                }
            }
        }
    }

    auto lock_guard() noexcept -> guard_awaiter { return {*this, local_context()}; };
};

}; // namespace coro
