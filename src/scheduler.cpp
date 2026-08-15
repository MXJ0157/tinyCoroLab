#include "coro/scheduler.hpp"

namespace coro
{
auto scheduler::init_impl(size_t ctx_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    detail::init_meta_info();
    m_ctx_cnt = ctx_cnt;
    m_context_states = std::vector<std::atomic<int>>(m_ctx_cnt);
    for (size_t i = 0; i < m_ctx_cnt; ++i)
    {
        m_context_states[i].store(kContextRunning, std::memory_order_relaxed);
    }
    m_unfinished_context_count.store(m_ctx_cnt, std::memory_order_release);
    m_ctxs    = detail::ctx_container{};
    m_ctxs.reserve(m_ctx_cnt);
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs.emplace_back(std::make_unique<context>());
        m_ctxs[i]->setSchd(this);
    }
    m_dispatcher.init(m_ctx_cnt, &m_ctxs);

#ifdef ENABLE_MEMORY_ALLOC
    coro::allocator::memory::mem_alloc_config config;
    m_mem_alloc.init(config);
    ginfo.mem_alloc = &m_mem_alloc;
#endif
}

auto scheduler::loop_impl() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    for (int i = 0; i < m_ctx_cnt; i++){
        m_ctxs[i]->start();
    }

    wait_all_context_finished();

    stop_impl();

    for (size_t i = 0; i < m_ctx_cnt; ++i){
        m_ctxs[i]->join();
    }
}

auto scheduler::stop_impl() noexcept -> void
{
    // TODO[lab2b]: example function
    // This is an example which just notify stop signal to each context,
    // if you don't need this, function just ignore or delete it
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->notify_stop();
    }
}

auto scheduler::submit_task_impl(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    size_t ctx_id = m_dispatcher.dispatch();
    int old_state = m_context_states[ctx_id].exchange(kContextRunning, std::memory_order_acq_rel);
    if (old_state == kContextFinished)
    {
        m_unfinished_context_count.fetch_add(1, std::memory_order_acq_rel);
    }
    m_ctxs[ctx_id]->submit_task(handle);
}

auto scheduler::try_mark_context_finished(size_t ctx_id) noexcept -> void
{
    if (ctx_id >= m_ctx_cnt || !m_ctxs[ctx_id]->finish_all())
    {
        return;
    }

    int expected = kContextRunning;
    if (!m_context_states[ctx_id].compare_exchange_strong(
            expected, kContextFinishing, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return;
    }

    if (!m_ctxs[ctx_id]->finish_all())
    {
        int finishing = kContextFinishing;
        m_context_states[ctx_id].compare_exchange_strong(
            finishing, kContextRunning, std::memory_order_acq_rel, std::memory_order_acquire);
        return;
    }

    int finishing = kContextFinishing;
    if (m_context_states[ctx_id].compare_exchange_strong(
            finishing, kContextFinished, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        m_unfinished_context_count.fetch_sub(1, std::memory_order_acq_rel);
    }
}

auto scheduler::wait_all_context_finished() noexcept -> void
{
    while (m_unfinished_context_count.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }
}
}; // namespace coro
