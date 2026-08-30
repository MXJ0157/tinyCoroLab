/**
 * @file condition_variable.hpp
 * @author JiahuiWang
 * @brief lab5b
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <functional>

#include "coro/attribute.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/spinlock.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5b, in this part you will build the basic coroutine
 * synchronization component����condition_variable by modifing condition_variable.hpp
 * and condition_variable.cpp. Please ensure you have read the document of lab5b.
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

using cond_type = std::function<bool()>;

class condition_variable;
using cond_var = condition_variable;

// TODO[lab5b]: This condition_variable is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class condition_variable final
{
    struct cv_awaiter: public mutex::mutex_awaiter{
        cond_var& cv;
        cond_type* cond;

        cv_awaiter(mutex& m, context& c, cond_var& cv_var, cond_type* condition = nullptr) noexcept :
            mutex_awaiter(m,c), cv(cv_var), cond(condition){}

        bool await_ready() {
            this->ctx.register_wait(1);
            if (cond == nullptr)
                return false;
            return (*cond)();
        }

        bool await_suspend(std::coroutine_handle<> handle){
            parent = handle;
            this->cv.add_awaiter(this);
            mtx.unlock();
            return true;
        }

        auto await_resume() -> void{
            this->ctx.unregister_wait(1);
            return;
        }

        auto wake_up() -> void{
            if (!mtx.lock_or_add(this)){
                //加锁成功,再次检查条件是否满足
                resume();
            }
            //加锁失败,awaiter已经挂在mutex的阻塞队列上,被唤醒之后已经拥有锁了,调用resume虚函数,执行下面的条件检查
        }

        auto resume() -> void override{
            //再次检查条件是否满足
            if (cond && !(*cond)()){
                //条件不满足,释放锁,加入cv_awaiter的阻塞队列中
                cv.add_awaiter(this);
                mtx.unlock();
                return;
            }
            //没有条件谓词或者条件谓词满足: 加入到任务队列中
            mutex::mutex_awaiter::resume();
        }
    };

    friend struct guard_awaiter;
    std::atomic<detail::awaiter_ptr> cur_state{ nullptr };

public:
    condition_variable() noexcept  = default;
    ~condition_variable() noexcept = default;

    CORO_NO_COPY_MOVE(condition_variable);

    bool add_awaiter(cv_awaiter* waiter){
        //1: 已经加锁但无等待, 其他: 已经加锁并且有其他协程等待
        detail::awaiter_ptr old_value = nullptr;
        while (1){
            old_value = cur_state.load();
            waiter->next = static_cast<cv_awaiter*>(old_value);
            if (cur_state.compare_exchange_weak(old_value, waiter, std::memory_order_acq_rel)){
                return true;
            }
        }
    }

    auto wait(mutex& mtx) noexcept -> cv_awaiter{
        return cv_awaiter{ mtx, local_context(), *this,  nullptr };
    }

    auto wait(mutex& mtx, cond_type&& cond) noexcept -> cv_awaiter{
        return cv_awaiter{ mtx, local_context(), *this, &cond };
    }

    auto wait(mutex& mtx, cond_type& cond) noexcept -> cv_awaiter{
        return cv_awaiter{ mtx, local_context(), *this, &cond };
    }

    auto notify_one() noexcept -> void{
        detail::awaiter_ptr old_value = nullptr, next = nullptr;
        while (1){
            old_value = cur_state.load();
            if(old_value)
            {
                next = static_cast<cv_awaiter*>(old_value)->next;
                if (cur_state.compare_exchange_weak(old_value, next, std::memory_order_acq_rel)){
                    auto h = static_cast<cv_awaiter*>(old_value);
                    h->wake_up();
                    return;
                }
            }
            else{
                return;
            }
        }
    };

    auto notify_all() noexcept -> void{
        detail::awaiter_ptr head = cur_state.exchange(nullptr);
        if (head != nullptr){
            auto h = static_cast<cv_awaiter*>(head);
            while (h){
                auto next = dynamic_cast<cv_awaiter*>(h->next);
                h->wake_up();
                h = next;
            }
        }
    };
};

}; // namespace coro
