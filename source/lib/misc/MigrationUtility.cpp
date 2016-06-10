/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#include <limits>
#include <set>
#include <chrono>

#include <fds_assert.h>
#include <util/Log.h>
#include <fds_timer.h>

#include <MigrationUtility.h>

namespace fds {

MigrationSeqNum::MigrationSeqNum()
    : seqNumTimerInterval(0),
      seqNumTimer(nullptr),
      seqNumTimerTask(nullptr),
      seqNumTimeoutHandler(NULL),
      seqNumTimerEnabled(false),
      seqNumTimerStarted(false)
{
    curSeqNum = 0;
    lastSeqNum = std::numeric_limits<uint64_t>::max();
    seqComplete = false;
}


MigrationSeqNum::MigrationSeqNum(FdsTimerPtr& timer,
                                 uint32_t intervalTimer,
                                 const std::function<void()> &timeoutHandler,
                                 fds_uint64_t pId)
    : seqNumTimerInterval(intervalTimer),
      seqNumTimer(timer),
      seqNumTimeoutHandler(timeoutHandler),
      seqNumTimerEnabled(true),
      seqNumTimerStarted(false),
      parentId(pId)
{
    curSeqNum = 0;
    lastSeqNum = std::numeric_limits<uint64_t>::max();
    seqComplete = false;
}

MigrationSeqNum::~MigrationSeqNum()
{
    seqNumList.clear();

    if (seqNumTimerEnabled) {
        fds_assert(nullptr != seqNumTimer);
        if (nullptr != seqNumTimerTask) {
            bool timerCancelled = stopProgressCheck();
            seqNumTimerTask.reset();
        }
        fds_assert(false == seqNumTimerStarted);
    }
}

bool
MigrationSeqNum::isSeqNumComplete()
{
    return seqComplete;
}

void
MigrationSeqNum::resetSeqNum()
{
    curSeqNum = 0;
    lastSeqNum = std::numeric_limits<uint64_t>::max();
    seqComplete = false;
    seqNumList.clear();

    if (seqNumTimerEnabled) {
        fds_assert(nullptr != seqNumTimer);
        if (nullptr != seqNumTimerTask) {
            bool timerCancelled = stopProgressCheck();
            seqNumTimerTask.reset();
        }
        fds_assert(false == seqNumTimerStarted);
    }
}

bool
MigrationSeqNum::setSeqNum(uint64_t latestSeqNum, bool isLastSeqNum)
{
    std::lock_guard<std::mutex> lock(seqNumLock);
    std::set<uint64_t>::iterator cit;

    // Set the current time
    lastSetTime = std::chrono::steady_clock::now();

    // After the timestamp, start the progress check
    startProgressCheck();

    if (isLastSeqNum) {
        lastSeqNum = latestSeqNum;
    }

    if (latestSeqNum == curSeqNum) {
        if (curSeqNum == lastSeqNum) {
            seqComplete = true;
            LOGDEBUG << " Last sequence recvd. Handling in order receipt. "
                     << " parent(uuid): " << std::hex << parentId << std::dec
                     << " curSeqNum: " << curSeqNum << " lastSeqNum: " << lastSeqNum
                     << " latestSeqNum: " << latestSeqNum << " isLastSeqNum: " << isLastSeqNum;
            bool cancelled = stopProgressCheck();
            return true;
        }
        ++curSeqNum;
        while ((cit = seqNumList.begin()) != seqNumList.end()) {
            if (*cit == curSeqNum) {
                seqNumList.erase(cit);
                if (curSeqNum == lastSeqNum) {
                    seqComplete = true;
                    LOGDEBUG << " Last sequence recvd. Handling out of order receipt. "
                             << " parent(uuid): " << std::hex << parentId << std::dec
                             << " curSeqNum: " << curSeqNum << " lastSeqNum: " << lastSeqNum
                             << " latestSeqNum: " << latestSeqNum << " isLastSeqNum: " << isLastSeqNum;
                    bool cancelled = stopProgressCheck();
                    return true;
                }
                ++curSeqNum;
            } else {
                break;
            }
        }

    } else {
        seqNumList.insert(latestSeqNum);
    }

    return false;
}


bool
MigrationSeqNum::startProgressCheck()
{
    if (seqNumTimerEnabled) {
        if (!seqNumTimerStarted) {
            LOGDEBUG << "starting migration sequence check for parent(uuid): "
                     << std::hex << parentId << std::dec;
            seqNumTimerTask = FdsTimerTaskPtr(new FdsTimerFunctionTask(std::bind(&MigrationSeqNum::checkProgress,
                                                                       this)));
            // Before the timer is set, just set the set time to curren time.
            lastSetTime = std::chrono::steady_clock::now();

            // no locking needed for this one, but what the hell...
            seqNumTimerStarted = true;

            return seqNumTimer->scheduleRepeated(seqNumTimerTask,
                                                 std::chrono::seconds(seqNumTimerInterval));
        }
    }
    return false;
}

bool
MigrationSeqNum::stopProgressCheck()
{
    if (seqNumTimerEnabled) {
        if (seqNumTimerStarted) {
            LOGDEBUG << "stopping migration sequence check for parent(uuid): "
                     << std::hex << parentId << std::dec;
            bool cancelled = seqNumTimer->cancel(seqNumTimerTask);
            seqNumTimerStarted = false;
            return cancelled;
        }
    }
    return false;
}

void
MigrationSeqNum::checkProgress()
{
    seqNumLock.lock();
    std::chrono::steady_clock::time_point currTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_span =
                std::chrono::duration_cast<std::chrono::duration<double>>(currTime - lastSetTime);
    seqNumLock.unlock();

    if (time_span.count() >= static_cast<double>(seqNumTimerInterval)) {
        fds_assert(seqNumTimeoutHandler);
        stopProgressCheck();
        if (seqNumTimeoutHandler) { seqNumTimeoutHandler(); }
    }

}


std::ostream& operator<< (std::ostream& out,
                          const MigrationSeqNum& seqNumRecv)
{
    out << "curSeqNum: " << seqNumRecv.curSeqNum << std::endl;
    out << "lastSeqNum: " << seqNumRecv.lastSeqNum << std::endl;

    for (auto cit = seqNumRecv.seqNumList.begin();
         cit != seqNumRecv.seqNumList.end();
         ++cit) {
        out << "[" << *cit << "] ";
    }
    out << std::endl;

    return out;
}

/**
 * prints out double sequence data to log.
 */
boost::log::formatting_ostream& operator<< (boost::log::formatting_ostream& out,
                                            const MigrationSeqNum& seqNumRecv)
{
    out << "curSeqNum: " << seqNumRecv.curSeqNum << std::endl;
    out << "lastSeqNum: " << seqNumRecv.lastSeqNum << std::endl;

    for (auto cit = seqNumRecv.seqNumList.begin();
         cit != seqNumRecv.seqNumList.end();
         ++cit) {
        out << "[" << *cit << "] ";
    }
    out << std::endl;

    return out;
}

MigrationDoubleSeqNum::MigrationDoubleSeqNum()
    : seqNumTimerInterval(0),
      seqNumTimer(nullptr),
      seqNumTimerTask(nullptr),
      seqNumTimeoutHandler(NULL),
      seqNumTimerEnabled(false),
      seqNumTimerStarted(false)
{
    curSeqNum1 = 0;
    lastSeqNum1 = std::numeric_limits<uint64_t>::max();
    completeSeqNum1 = false;
}

MigrationDoubleSeqNum::MigrationDoubleSeqNum(FdsTimerPtr& timer,
                                             uint32_t intervalTimer,
                                             const std::function<void()> &timeoutHandler,
                                             fds_uint64_t id)
    : seqNumTimer(timer),
      seqNumTimerInterval(intervalTimer),
      seqNumTimeoutHandler(timeoutHandler),
      seqNumTimerEnabled(true),
      seqNumTimerStarted(false)
{
    curSeqNum1 = 0;
    lastSeqNum1 = std::numeric_limits<uint64_t>::max();
    completeSeqNum1 = false;
    parentId = id;
}

MigrationDoubleSeqNum::~MigrationDoubleSeqNum()
{
    mapSeqNum2.clear();

    if (seqNumTimerEnabled) {
        fds_assert(nullptr != seqNumTimer);
        if (nullptr != seqNumTimerTask) {
            bool timerCancelled = stopProgressCheck(true, true);
            seqNumTimerTask.reset();
        }
        fds_assert(false == seqNumTimerStarted);
    }

}

bool
MigrationDoubleSeqNum::isDoubleSeqNumComplete()
{
    return completeSeqNum1;
}
void
MigrationDoubleSeqNum::resetDoubleSeqNum()
{
    curSeqNum1 = 0;
    lastSeqNum1 = std::numeric_limits<uint64_t>::max();
    completeSeqNum1 = false;
    mapSeqNum2.clear();

    if (seqNumTimerEnabled) {
        fds_assert(nullptr != seqNumTimer);
        if (nullptr != seqNumTimerTask) {
            bool timerCancelled = stopProgressCheck(true, true);
            seqNumTimerTask.reset();
        }
        fds_assert(false == seqNumTimerStarted);
    }

}

bool
MigrationDoubleSeqNum::setDoubleSeqNum(uint64_t seqNum1, bool isLastSeqNum1,
                                       uint64_t seqNum2, bool isLastSeqNum2)
{
    std::lock_guard<std::mutex> lock(seqNumLock);

    // std::cout << "seq1(" << seqNum1 << "," << isLastSeqNum1 << ") ";
    // std::cout << "seq2(" << seqNum2 << "," << isLastSeqNum2 << ") ";
    //

    // Set the time
    lastSetTime = std::chrono::steady_clock::now();
    // after the timestamp, start the progress check.
    startProgressCheck(isLastSeqNum1, isLastSeqNum2);

    if (isLastSeqNum1) {
        lastSeqNum1 = seqNum1;
    }

    /* We shouldn't see this.  This means that last set flag for the seqNum2
     * was specified more than once or are seeing seqNum1 after all
     * seqNum2 associated with it has completed.  This is likely issue with
     * programming.
     */
    fds_assert(seqNum1 >= curSeqNum1);

    if (nullptr == mapSeqNum2[seqNum1]) {
        mapSeqNum2[seqNum1] = MigrationSeqNum::ptr(new MigrationSeqNum());
    }

    fds_assert(nullptr != mapSeqNum2[seqNum1]);
    bool completeSeqNum2 = mapSeqNum2[seqNum1]->setSeqNum(seqNum2, isLastSeqNum2);
    // std::cout << "=> seqNum2 complete=" << completeSeqNum2;

    if ((seqNum1 == curSeqNum1) && mapSeqNum2[curSeqNum1]->isSeqNumComplete()) {
        if (seqNum1 == lastSeqNum1) {
            // std::cout << " " << __LINE__ << " Removing map[" << curSeqNum1 << "] ";
            mapSeqNum2.erase(curSeqNum1);
            completeSeqNum1 = true;
            // std::cout << std::endl;
            LOGDEBUG << " (seqNum1 == lastSeqNum1) : "
                     << " executor: " << std::hex << parentId << std::dec
                     << " curSeqNum1: " << curSeqNum1 << " lastSeqNum1: " << lastSeqNum1
                     << " seqNum1: " << seqNum1 << " seqNum2: " << seqNum2
                     << " isLastSeqNum1: " << isLastSeqNum1 << " isLastSeqNum2: " << isLastSeqNum2;
            bool cancelled = stopProgressCheck(isLastSeqNum1, isLastSeqNum2);
            return true;
        }
        // std::cout << " " << __LINE__ << " Removing map[" << curSeqNum1 << "] ";
        mapSeqNum2.erase(curSeqNum1);
        ++curSeqNum1;

        // loop to check if there are out of sequence numbers.
        while ((nullptr != mapSeqNum2[curSeqNum1]) && mapSeqNum2[curSeqNum1]->isSeqNumComplete()) {
            // std::cout << " " << __LINE__ << " Removing map[" << curSeqNum1 << "] ";
            mapSeqNum2.erase(curSeqNum1);
            if (curSeqNum1 == lastSeqNum1) {
                // std::cout << " " << __LINE__ << "Last sequence" << std::endl;
                completeSeqNum1 = true;
                LOGDEBUG << "(curSeqNum1 == lastSeqNum1) : "
                         << " executor: " << std::hex << parentId << std::dec
                         << " curSeqNum1: " << curSeqNum1 << " lastSeqNum1: " << lastSeqNum1
                         << " seqNum1: " << seqNum1 << " seqNum2: " << seqNum2
                         << " isLastSeqNum1: " << isLastSeqNum1 << " isLastSeqNum2: " << isLastSeqNum2;
                bool cancelled = stopProgressCheck(isLastSeqNum1, isLastSeqNum2);
                return true;
            }
            ++curSeqNum1;
        }
    }

    // std::cout << std::endl;

    return false;
}

bool
MigrationDoubleSeqNum::startProgressCheck(bool isNum1Last, bool isNum2Last)
{
    // no need to synchronize, since there can be only one unique
    // "true & true" combo.
    if (seqNumTimerEnabled) {
        if (!seqNumTimerStarted && (!isNum1Last && !isNum2Last)) {
            LOGDEBUG << "starting double sequence check for parent(uuid): "
                     << std::hex << parentId << std::dec;
            fds_assert(nullptr == seqNumTimerTask);
            seqNumTimerTask =
                FdsTimerTaskPtr(new FdsTimerFunctionTask(std::bind(&MigrationDoubleSeqNum::checkProgress,
                                                         this)));
            seqNumTimerStarted = true;
            return seqNumTimer->scheduleRepeated(seqNumTimerTask,
                                                 std::chrono::seconds(seqNumTimerInterval));
        }
    }
    return false;
}


bool
MigrationDoubleSeqNum::stopProgressCheck(bool isNum1Last, bool isNum2Last)
{
    if (seqNumTimerEnabled) {
       if (seqNumTimerStarted && (isNum1Last && isNum2Last)) {
            fds_assert(nullptr != seqNumTimerTask);
            LOGDEBUG << "stopped double sequence check for parent(uuid): "
                     << std::hex << parentId << std::dec;
            bool cancelled = seqNumTimer->cancel(seqNumTimerTask);
            seqNumTimerStarted = false;
            return cancelled;
        }
    }

   return false;
}

void
MigrationDoubleSeqNum::checkProgress()
{
    seqNumLock.lock();
    std::chrono::steady_clock::time_point currTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_span =
                std::chrono::duration_cast<std::chrono::duration<double>>(currTime - lastSetTime);
    seqNumLock.unlock();


    if (time_span.count() >= static_cast<double>(seqNumTimerInterval)) {
        fds_assert(seqNumTimeoutHandler);
        // std::cout << "timedout" << std::endl;
        LOGNOTIFY << "Migration Timed Out with duration=" << time_span.count() << " executor: " << std::hex << parentId << std::dec;
        stopProgressCheck(true, true);
        if (seqNumTimeoutHandler) { seqNumTimeoutHandler(); }
    }
}

std::ostream& operator<< (std::ostream& out,
                          const MigrationDoubleSeqNum& doubleSeqNum)
{
    out << "curSeqNum1: " << doubleSeqNum.curSeqNum1 << std::endl;
    out << "lastSeqNum1: " << doubleSeqNum.lastSeqNum1 << std::endl;
    out << "completeSeqNum1: " << doubleSeqNum.completeSeqNum1 << std::endl;

    for (auto cit = doubleSeqNum.mapSeqNum2.begin();
         cit != doubleSeqNum.mapSeqNum2.end();
         ++cit) {
        out << *cit->second;
    }
    out << std::endl;

    return out;
}

boost::log::formatting_ostream& operator<< (boost::log::formatting_ostream& out,
                                            const MigrationDoubleSeqNum& doubleSeqNum)
{
    out << "curSeqNum: " << doubleSeqNum.curSeqNum1 << std::endl;
    out << "lastSeqNum: " << doubleSeqNum.lastSeqNum1 << std::endl;
    out << "complete: " << doubleSeqNum.completeSeqNum1 << std::endl;

    for (auto cit = doubleSeqNum.mapSeqNum2.begin();
         cit != doubleSeqNum.mapSeqNum2.end();
         ++cit) {
        out << *cit->second;
    }
    out << std::endl;

    return out;
}

MigrationTrackIOReqs::MigrationTrackIOReqs()
    : numTrackIOReqs(0),
      waitingTrackIOReqsCompletion(false),
      denyTrackIOReqs(false),
	  trackingStarted(false)
{

}

MigrationTrackIOReqs::~MigrationTrackIOReqs()
{
    fds_assert(0 == numTrackIOReqs);
    fds_assert(!waitingTrackIOReqsCompletion);
    fds_assert(denyTrackIOReqs || !trackingStarted);

    if (numTrackIOReqs > 0 || waitingTrackIOReqsCompletion) {
        LOGERROR << "Tracking started = " << trackingStarted
                 << "numTrackIOReqs = " << numTrackIOReqs
                 << "waiting = " << waitingTrackIOReqsCompletion;
    }
}

// Start tracking outstanding IO requests for SM token migration.
// If successfully start tracking, then must finish tracking
// outstanding IO.  If not properly paired the operation of
// starting and finishing,
//
// Example:
// bool result = startTrackIOReqs().
// if (!result) {
//      return;
//  }
//  ...
//  finishTrackIOReqs().
//
bool
MigrationTrackIOReqs::startTrackIOReqs()
{
    std::lock_guard<std::mutex> lock(trackReqsMutex);
    trackingStarted = true;

    // If the deny reqs
    if (denyTrackIOReqs) {
        LOGMIGRATE << "Migration IO Track Failed";
        return false;
    }
    ++numTrackIOReqs;

    LOGMIGRATE << "Migration IO Start Track Cnt=" << numTrackIOReqs;

    return true;
}

// Finish tracking oustanding IO.  Should be called only
// if IO is successfully tracked by successful call to startTrackIOReqs().
void
MigrationTrackIOReqs::finishTrackIOReqs()
{
    std::lock_guard<std::mutex> lock(trackReqsMutex);

    if (numTrackIOReqs > 0) {
        --numTrackIOReqs;
        LOGMIGRATE << "Migration IO Finish Track Cnt=" << numTrackIOReqs;
    }

    if ((numTrackIOReqs == 0) && (waitingTrackIOReqsCompletion == true)) {
        LOGMIGRATE << "Migration IO Finish Notify";
        trackReqsCondVar.notify_all();
    }
}

void
MigrationTrackIOReqs::waitForTrackIOReqs()
{
    std::unique_lock<std::mutex> lock(trackReqsMutex);
    denyTrackIOReqs = true;
    waitingTrackIOReqsCompletion = true;
    trackReqsCondVar.wait(lock, [this]{return numTrackIOReqs == 0;});
    waitingTrackIOReqsCompletion = false;

}

}  // namespace fds
