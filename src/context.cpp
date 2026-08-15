#include "coro/context.hpp"
#include "coro/scheduler.hpp"

namespace coro
{
context::context() noexcept
{
    m_id = ginfo.context_id.fetch_add(1, std::memory_order_relaxed);
    this->schd = nullptr;
}

auto context::init() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.init();
    linfo.ctx = this;
}

void context::setSchd(scheduler* schd){
    this->schd = schd;
}

auto context::deinit() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.deinit();
    linfo.ctx = nullptr;
    coroutine_wait_count = 0;
}

auto context::start() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job = make_unique<jthread>(
        [this](stop_token token)
        {
            this->init();
            this->run(token);
            this->deinit();
        });
}

auto context::notify_stop() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job->request_stop();
    m_engine.wake_up(1);
}

auto context::submit_task(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.submit_task(handle);
}

auto context::register_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    coroutine_wait_count.fetch_add(register_cnt);
}

auto context::unregister_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    coroutine_wait_count.fetch_sub(register_cnt);
}

bool context::finish_all(){
    return coroutine_wait_count == 0 &&
        m_engine.num_task_schedule() == 0 &&
        m_engine.empty_io();
}

auto context::run(stop_token token) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    while (1){
        while (m_engine.ready()){
            m_engine.exec_one_task();
        }

        if (finish_all()){
            if (this->schd != nullptr)
            {
                this->schd->try_mark_context_finished(m_id);
            }
            if (this->schd == nullptr || token.stop_requested()){
                break;
            }
        }

        m_engine.poll_submit();
    }
}

}; // namespace coro
