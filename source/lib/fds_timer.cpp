/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef USE_BOOSTBASED_TIMER
#include <fds_timer.h>

namespace fds
{
FdsTimerTask::FdsTimerTask(FdsTimer &fds_timer)
{
    state_ = TASK_STATE_UNINIT;
}

FdsTimerTask::~FdsTimerTask()
{
}


void FdsTimerTask::setExpiryTime(const std::chrono::system_clock::time_point &t)
{
    expTime_ = t;
}

std::chrono::system_clock::time_point FdsTimerTask::getExpiryTime() const {
    return expTime_;
}

/**
 * Constructor
 */
FdsTimer::FdsTimer()
: aborted_(false),
    timerThreadSleepMs_(1000),
    timerThread_(std::bind(&FdsTimer::runTimerThread_, this))
{
}

/**
 * Destroy the timer service 
 */
void FdsTimer::destroy()
{
    aborted_ = true;
    timerThread_.join();
}

/**
 * Cancel a task. 
 * @return true if task was succsfully cancelled.  Note, false
 * returned when task wasn't scheduled to begin with.
 */
bool FdsTimer::cancel(const FdsTimerTaskPtr& task)
{
    lock_.lock();
    /*
       fds_assert(task->state_ == TASK_STATE_SCHEDULED_ONCE ||
       task->state_ == TASK_STATE_SCHEDULED_REPEAT ||
       task->state_ == TASK_STATE_COMPLETE);
       */
    pendingTasks_.erase(task);
    task->state_ = TASK_STATE_CANCELLED;
    lock_.unlock();
    return true;
}

std::string FdsTimer::log_string()
{
    return "FdsTimer";
}

void FdsTimer::runTimerThread_()
{
    while (!aborted_) {
        // TODO(Rao): Improve this sleep below so that it services tasks
        // more promptly than every timerThreadSleepMs_
        auto cur_time_pt = std::chrono::system_clock::now();
        /* drain the expired tasks */
        lock_.lock();
        while (pendingTasks_.size() > 0) {
            auto task = *(pendingTasks_.begin());
            if (task->getExpiryTime() <= cur_time_pt) {
                pendingTasks_.erase(pendingTasks_.begin());
                fds_assert(task->state_ == TASK_STATE_SCHEDULED_ONCE ||
                           task->state_ == TASK_STATE_SCHEDULED_REPEAT);
                if (task->state_ == TASK_STATE_SCHEDULED_REPEAT) {
                    task->expTime_ = std::chrono::system_clock::now() + task->durationMs_;
                    pendingTasks_.insert(task);
                } else {
                    task->state_ = TASK_STATE_COMPLETE;
                }
                lock_.unlock();

                task->runTimerTask();

                lock_.lock();
            } else {
                break;
            }
        }
        lock_.unlock();
        /* go back to sleep */
        std::this_thread::sleep_for(std::chrono::milliseconds(timerThreadSleepMs_));
    }
}
}  // namespace fds

#else  // USE_BOOSTBASED_TIMER

#include <string>
#include <fds_timer.h>  // NOLINT
namespace fds
{


FdsTimerTask::FdsTimerTask(FdsTimer &fds_timer) // NOLINT
    : timer_(fds_timer.io_service_),
    lock_("FdsTimerTask"),
    scheduled_(false)
{
}

FdsTimerTask::~FdsTimerTask() { }

FdsTimer::FdsTimer()
    : work_(io_service_),
      io_thread_(&FdsTimer::start_io_service, this)
{
}

void FdsTimer::destroy()
{
    io_service_.stop();
    io_thread_.join();
}

bool FdsTimer::cancel(const FdsTimerTaskPtr& task)
{
    {
        fds_spinlock::scoped_lock l(task->lock_);
        if (!task->scheduled_) {
            return false;
        }
        task->scheduled_ = false;
    }
    /* Note, igoring the return value.  May be we shouldn't? */
    task->timer_.cancel();
    return true;
}

void FdsTimer::start_io_service()
{
    try {
        io_service_.run();
    } catch(const std::exception &e) {
        FDS_PLOG_WARN(g_fdslog) << e.what();
    }
}

}  // namespace fds
#endif  // USE_BOOSTBASED_TIMER

