/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <snapshot/scheduler.h>
#include <time.h>
namespace atc = apache::thrift::concurrency;
namespace fds { namespace snapshot {

Scheduler::Scheduler(OrchMgr* om, TaskProcessor* processor) {
    this->om = om;
    this->taskProcessor = processor;
    runner = new std::thread(&Scheduler::run, this);
    LOGDEBUG << "scheduler instantiated";
}

Scheduler::~Scheduler() {
    shutdown();
    runner->join();
    delete runner;
}

uint64_t Scheduler::getCurrentTime() {
    time_t  tt;
    time(&tt);
    return tt;
}

// will also update / modify
bool Scheduler::addPolicy(const fpi::SnapshotPolicy& policy) {
    atc::Synchronized s(monitor);
    // check if the policy is already here
    bool fModified = false;
    auto handleptr = handleMap.find(policy.id);
    if (handleptr != handleMap.end()) {
        // policy already exists
        LOGDEBUG << "policy already exists : " << policy.id;
        Task* task = handleptr->second->node_->value;
        if (task->setRecurrence(policy.recurrenceRule)) {
            LOGDEBUG << "updating policy:" << policy.id << " : " <<policy.recurrenceRule;
            pq.update(*(handleptr->second), task);
            fModified = true;
        }
    } else {
        Task* task = new Task();
        task->policyId = policy.id;
        task->setRecurrence(policy.recurrenceRule);
        auto handle = pq.push(task);
        handleMap[policy.id] = &handle;
        LOGDEBUG << "new policy:" << policy.id << " : " << policy.recurrenceRule;
        fModified = true;
    }

    if (fModified) {
        ping();
    }
    return fModified;
}

bool Scheduler::removePolicy(uint64_t policyId) {
    atc::Synchronized s(monitor);
    auto handleptr = handleMap.find(policyId);
    if (handleptr != handleMap.end()) {
        pq.erase(*(handleptr->second));
        handleMap.erase(handleptr);
        ping();
        LOGDEBUG << "removed policy from scheduler, id:" <<policyId;
        return true;
    } else {
        LOGWARN << "unable to remove policy from scheduler, id:" <<policyId;
        return false;
    }
}

void Scheduler::dump() {
    LOGDEBUG << "dump of scheduler queue:";
    for (PriorityQueue::ordered_iterator it = pq.ordered_begin(); it != pq.ordered_end(); ++it) {
        LOGDEBUG << *(*it) << ' ';
    }
}
void Scheduler::shutdown() {
    atc::Synchronized s(monitor);
    fShutdown = true;
    monitor.notifyAll();
}

void Scheduler::ping() {
    monitor.notifyAll();
}

void Scheduler::run() {
    atc::Synchronized s(monitor);
    LOGNORMAL << "snapshot scheduler started";
    while (!fShutdown) {
        while (!fShutdown && pq.empty()) {
            LOGDEBUG << "q empty .. waiting.";
            monitor.waitForever();
        }

        while (!fShutdown && !pq.empty()) {
            Task* task;
            uint64_t currTime = getCurrentTime();
            task = pq.top();
            LOGDEBUG << "curTime:" << currTime << " next:" << task->runAtTime;
            dump();
            if (task->runAtTime > currTime) {
                // there is no task to be executed at the time
                LOGDEBUG << "going into wait . currTime:";
                monitor.waitForTimeRelative((task->runAtTime - currTime)*1000);  // ms
            } else {
                // to be executed now ..
                LOGDEBUG << "processing policyid:" << task->policyId;
                taskProcessor->process(*task);

                // now check the next time & reschedule the task
                auto handleptr = handleMap.find(task->policyId);
                if (handleptr == handleMap.end()) {
                    LOGERROR << "major error . policyid is missing from the map : "
                             << task->policyId;
                }

                if (task->setNextRecurrence()) {
                    LOGDEBUG << "rescheduling policy:" << task->policyId
                             << " @ " << task->runAtTime;
                    pq.pop();
                    auto handle = pq.push(task);
                    handleMap[task->policyId] = &handle;
                    // pq.update(*(handleptr->second), task);
                } else {
                    LOGWARN << "no more recurrence of this policy: " << task->policyId;
                    pq.pop();
                    handleMap.erase(handleptr);
                }
            }
        }
    }  // !fShutdown
}

}  // namespace snapshot
}  // namespace fds
