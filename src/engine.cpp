#include "coro/engine.hpp"
#include "coro/io/io_info.hpp"
#include "coro/task.hpp"

namespace coro::detail
{
using std::memory_order_relaxed;

auto engine::init() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_uringpxy.init(config::kEntryLength);
    linfo.egn = this;
}

auto engine::deinit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_uringpxy.deinit();
    linfo.egn = nullptr;
}

auto engine::ready() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return m_todoTasks > 0;
}

auto engine::get_free_urs() noexcept -> ursptr
{
    // TODO[lab2a]: Add you codes
    return m_uringpxy.get_free_sqe();
}

auto engine::num_task_schedule() noexcept -> size_t
{
    // TODO[lab2a]: Add you codes
    return m_todoTasks.load();
}

auto engine::schedule() noexcept -> coroutine_handle<>
{
    // TODO[lab2a]: Add you codes
    auto handle = m_task_queue.pop();
    m_todoTasks.fetch_sub(1);
    return handle;
}

auto engine::submit_task(coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_task_queue.push(handle);
    m_todoTasks.fetch_add(1);
    m_uringpxy.write_eventfd(1);
}

auto engine::exec_one_task() noexcept -> void
{
    auto coro = schedule();
    coro.resume();
    if (coro.done())
    {
        clean(coro);
    }
}

auto engine::handle_cqe_entry(urcptr cqe) noexcept -> void
{
    auto data = reinterpret_cast<io::detail::io_info*>(io_uring_cqe_get_data(cqe));
    data->cb(data, cqe->res);
}

auto engine::poll_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    if (m_toSubmitIo.load() > 0){
        int num = m_uringpxy.submit();
        if (num > 0){
            m_toSubmitIo.fetch_sub(num);
            m_runningIo.fetch_add(num);
        }
    }
    if (m_uringpxy.peek_uring()){
        int nready = m_uringpxy.peek_batch_cqe(m_urcqes.data(), m_urcqes.size());
        for (int i = 0; i < nready; ++i){
            handle_cqe_entry(m_urcqes[i]);
        }
        m_uringpxy.cq_advance(nready);
        m_runningIo.fetch_sub(nready);
    }
    if (ready()){
        return;
    }
    m_uringpxy.wait_eventfd();
}

auto engine::add_io_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_toSubmitIo.fetch_add(1);
    m_uringpxy.write_eventfd(1);
}

auto engine::empty_io() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return m_toSubmitIo.load() == 0 && m_runningIo.load() == 0;
}
}; // namespace coro::detail
