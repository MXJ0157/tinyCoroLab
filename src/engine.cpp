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
    return !m_task_queue.was_empty();
}

auto engine::get_free_urs() noexcept -> ursptr
{
    // TODO[lab2a]: Add you codes
    return m_uringpxy.get_free_sqe();
}

auto engine::num_task_schedule() noexcept -> size_t
{
    // TODO[lab2a]: Add you codes
    return m_task_queue.was_size();
}

auto engine::schedule() noexcept -> coroutine_handle<>
{
    // TODO[lab2a]: Add you codes
    auto handle = m_task_queue.pop();
    return handle;
}

auto engine::submit_task(coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_task_queue.push(handle);
    m_uringpxy.write_eventfd(1);
}

void engine::wake_up(int val){
    m_uringpxy.write_eventfd(val);
}

auto engine::exec_one_task() noexcept -> void
{
    auto coro = schedule();

    auto task_coro = ::coro::coroutine_handle::from_address(coro.address());
    auto cleanup_coro = task_coro.promise().detached_root;

    // A non-detached top-level task is still owned by its task object, so its
    // frame remains valid after resume() and clean() will intentionally do
    // nothing.  An awaited child without a detached root has no stable handle
    // that the engine may inspect after resume().
    if (!cleanup_coro && !task_coro.promise().parent)
    {
        cleanup_coro = coro;
    }

    coro.resume();

    if (cleanup_coro && cleanup_coro.done())
    {
        clean(cleanup_coro);
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
    m_uringpxy.wait_eventfd();
    if (m_uringpxy.peek_uring()){
        int nready = m_uringpxy.peek_batch_cqe(m_urcqes.data(), m_urcqes.size());
        for (int i = 0; i < nready; ++i){
            handle_cqe_entry(m_urcqes[i]);
        }
        m_uringpxy.cq_advance(nready);
        m_runningIo.fetch_sub(nready);
    }
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
