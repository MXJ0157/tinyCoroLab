/**
 * @file channel.hpp
 * @author JiahuiWang
 * @brief lab5c
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>

#include "coro/comp/condition_variable.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/concepts/common.hpp"
#include "coro/task.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5c, in this part you will build the basic coroutine
 * synchronization component����channel by modifing channel.hpp and channel.cpp.
 * Please ensure you have read the document of lab5c.
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
namespace detail
{
// TODO[lab5c]: Add code that you don't want to use externally in namespace detail
}; // namespace detail

// TODO[lab5c]: This channel is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
template<concepts::conventional_type T, size_t capacity = 1>
class channel
{
    using data_type = std::optional<T>;
    // 下列变量用于维持一个环形数组
    // 空: head == tail
    // 满: (head + 1) % capacity == tail
    size_t                  head{ 0 }; // 头指针
    size_t                  tail{ 0 }; // 尾指针
    size_t                  cur_num{ 0 };  // 元素数量
    std::array<T, capacity> data_array;   // 存储元素的数组
    mutex                   mtx;
    condition_variable      producer_cv, consumer_cv;
    //0:未关闭, 1:已经关闭但还有数据, 2:已经关闭并且没有数据
    const bool OPEN = true, CLOSED = false;
    atomic<bool>             cur_state{ OPEN };
public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    auto send(value_type&& value) noexcept -> task<bool>
    {
        auto guard = co_await mtx.lock_guard();
        // 已关闭,返回 false
        if (cur_state != OPEN){
            co_return false;
        }
        co_await producer_cv.wait(mtx, [this]()->bool{
            //队列中有空闲位置或者停止
            return cur_num < capacity  || cur_state == CLOSED;
            });
        //如果已经停止那么返回false
        if (cur_state == CLOSED){
            co_return false;
        }
        data_array[tail] = value;
        if (head == tail){
            consumer_cv.notify_one();
        }
        tail = (tail + 1) % capacity;
        cur_num++;
        co_return true;
    }

    auto recv() noexcept -> task<data_type>{
        auto guard = co_await mtx.lock_guard();
        co_await consumer_cv.wait(mtx, [this]()->bool{
            //队列中有空闲位置或者停止
            return cur_num != 0 || cur_state == CLOSED;
            });
        if (cur_state == CLOSED && cur_num == 0){
            co_return std::nullopt;
        }
        auto res = data_array[head];
        if (cur_num == capacity){
            producer_cv.notify_one();
        }
        head = (head + 1) % capacity;
        cur_num--;
        co_return res;
    }

    auto close() noexcept -> void{
        cur_state.store(CLOSED);
        producer_cv.notify_all();
        consumer_cv.notify_all();
    }
};

}; // namespace coro
