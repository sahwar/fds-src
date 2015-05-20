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
      seqNumTimeoutHandler(NULL)
{
    curSeqNum = 0;
    lastSeqNum = std::numeric_limits<uint64_t>::max();
    seqComplete = false;
}


MigrationSeqNum::MigrationSeqNum(FdsTimerPtr& timer,
                                 uint32_t intervalTimer,
                                 const std::function<void()> &timeoutHandler)
    : seqNumTimerInterval(intervalTimer),
      seqNumTimer(timer),
      seqNumTimeoutHandler(timeoutHandler)
{
    curSeqNum = 0;
    lastSeqNum = std::numeric_limits<uint64_t>::max();
    seqComplete = false;
    fds_verify(seqNumTimerInterval > 0);
}

MigrationSeqNum::~MigrationSeqNum()
{
    if (std::numeric_limits<uint64_t>::max() != lastSeqNum) {
        fds_verify(curSeqNum == lastSeqNum);
    }

    seqNumList.clear();

    if (nullptr != seqNumTimer) {
        fds_verify(nullptr != seqNumTimerTask);
        bool timerCancelled = stopProgressCheck();

        seqNumTimerTask.reset();
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

    if (nullptr != seqNumTimer) {
        fds_verify(nullptr != seqNumTimerTask);
        bool timerCancelled = stopProgressCheck();
        seqNumTimerTask.reset();
    }
}

bool
MigrationSeqNum::setSeqNum(uint64_t latestSeqNum, bool isLastSeqNum)
{
    std::lock_guard<std::mutex> lock(seqNumLock);
    std::set<uint64_t>::iterator cit;

    lastSetTime = std::chrono::steady_clock::now();

    if (isLastSeqNum) {
        lastSeqNum = latestSeqNum;
    }

    if (latestSeqNum == curSeqNum) {
        if (curSeqNum == lastSeqNum) {
            seqComplete = true;
            return true;
        }
        ++curSeqNum;
        while ((cit = seqNumList.begin()) != seqNumList.end()) {
            if (*cit == curSeqNum) {
                seqNumList.erase(cit);
                if (curSeqNum == lastSeqNum) {
                    seqComplete = true;
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

    if (nullptr != seqNumTimer) {
        seqNumTimerTask = FdsTimerTaskPtr(new FdsTimerFunctionTask(*seqNumTimer,
                                                                   std::bind(&MigrationSeqNum::checkProgress,
                                                                   this)));
        seqNumLock.lock();
        lastSetTime = std::chrono::steady_clock::now();
        seqNumLock.unlock();
        return seqNumTimer->scheduleRepeated(seqNumTimerTask,
                                             std::chrono::seconds(seqNumTimerInterval));
    } else {
        return false;
    }

}

bool
MigrationSeqNum::stopProgressCheck()
{
    if (nullptr != seqNumTimer) {
        return seqNumTimer->cancel(seqNumTimerTask);
    } else {
        return false;
    }
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
        fds_verify(seqNumTimeoutHandler);
        stopProgressCheck();
        seqNumTimeoutHandler();
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
 * prints out histogram data to log.
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
      seqNumTimeoutHandler(NULL)
{
    curSeqNum1 = 0;
    lastSeqNum1 = std::numeric_limits<uint64_t>::max();
    completeSeqNum1 = false;
}

MigrationDoubleSeqNum::MigrationDoubleSeqNum(FdsTimerPtr& timer,
                                             uint32_t intervalTimer,
                                             const std::function<void()> &timeoutHandler)
    : seqNumTimer(timer),
      seqNumTimerInterval(intervalTimer),
      seqNumTimeoutHandler(timeoutHandler)
{
    curSeqNum1 = 0;
    lastSeqNum1 = std::numeric_limits<uint64_t>::max();
    completeSeqNum1 = false;
    fds_verify(seqNumTimerInterval > 0);
}

MigrationDoubleSeqNum::~MigrationDoubleSeqNum()
{
    if (std::numeric_limits<uint64_t>::max() != lastSeqNum1) {
        fds_verify(curSeqNum1 == lastSeqNum1);
    }

    mapSeqNum2.clear();

    if (nullptr != seqNumTimer) {
        fds_verify(nullptr != seqNumTimerTask);
        bool timerCancelled = stopProgressCheck();
        seqNumTimerTask.reset();
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

    if (nullptr != seqNumTimer) {
        fds_verify(nullptr != seqNumTimerTask);
        bool timerCancelled = stopProgressCheck();
        seqNumTimerTask.reset();
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
    lastSetTime = std::chrono::steady_clock::now();

    if (isLastSeqNum1) {
        lastSeqNum1 = seqNum1;
    }

    /* We shouldn't see this.  This means that last set flag for the seqNum2
     * was specified more than once or are seeing seqNum1 after all
     * seqNum2 associated with it has completed.  This is likely issue with
     * programming.
     */
    fds_verify(seqNum1 >= curSeqNum1);

    if (nullptr == mapSeqNum2[seqNum1]) {
        mapSeqNum2[seqNum1] = MigrationSeqNum::ptr(new MigrationSeqNum());
    }

    fds_verify(nullptr != mapSeqNum2[seqNum1]);
    bool completeSeqNum2 = mapSeqNum2[seqNum1]->setSeqNum(seqNum2, isLastSeqNum2);
    // std::cout << "=> seqNum2 complete=" << completeSeqNum2;

    if ((seqNum1 == curSeqNum1) && mapSeqNum2[curSeqNum1]->isSeqNumComplete()) {
        if (seqNum1 == lastSeqNum1) {
            // std::cout << " " << __LINE__ << " Removing map[" << curSeqNum1 << "] ";
            mapSeqNum2.erase(curSeqNum1);
            completeSeqNum1 = true;
            // std::cout << std::endl;
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
                return true;
            }
            ++curSeqNum1;
        }
    }

    // std::cout << std::endl;

    return false;
}

bool
MigrationDoubleSeqNum::startProgressCheck()
{
    if (nullptr != seqNumTimer) {
        seqNumTimerTask = FdsTimerTaskPtr(new FdsTimerFunctionTask(*seqNumTimer,
                                                                   std::bind(&MigrationDoubleSeqNum::checkProgress,
                                                                   this)));
        seqNumLock.lock();
        lastSetTime = std::chrono::steady_clock::now();
        seqNumLock.unlock();
        return seqNumTimer->scheduleRepeated(seqNumTimerTask,
                                             std::chrono::seconds(seqNumTimerInterval));
    } else {
        return false;
    }
}


bool
MigrationDoubleSeqNum::stopProgressCheck()
{
    if (nullptr != seqNumTimer) {
        return seqNumTimer->cancel(seqNumTimerTask);
    } else {
        return false;
    }

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
        fds_verify(seqNumTimeoutHandler);
        // std::cout << "timedout" << std::endl;
        stopProgressCheck();
        seqNumTimeoutHandler();
    }
}

std::ostream& operator<< (std::ostream& out,
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
      denyTrackIOReqs(false)
{

}

MigrationTrackIOReqs::~MigrationTrackIOReqs()
{
    fds_verify(0 == numTrackIOReqs);
    fds_verify(false == waitingTrackIOReqsCompletion);
    fds_verify(true == denyTrackIOReqs);
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
