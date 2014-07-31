/* Copyright 2013 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_FDS_COUNTERS_H_
#define SOURCE_INCLUDE_FDS_COUNTERS_H_

#include <string>
#include <iostream>
#include <atomic>
#include <vector>
#include <ostream>
#include <boost/noncopyable.hpp>
#include <fds_types.h>
#include <fds_assert.h>
#include <concurrency/Mutex.h>
#include <fds_timer.h>

namespace fds {
/* Forward declarations */
class FdsCounters;
class FdsBaseCounter;
class FdsCountersMgr;

/* Class SamplerTask: performs snapshotting and sampling. Operates on data 
   structures in couter manager */
class SamplerTask : public FdsTimerTask {
    public:
    SamplerTask(FdsTimer &fds_timer, const std::vector<FdsCounters*> &counters,  std::vector<FdsCounters*> &snapshot_counters) : 
        FdsTimerTask(fds_timer), counters_ref_(counters), snapshot_counters_(snapshot_counters) {}
    void runTimerTask() override;
    void snapshot_counters() {};

    protected:
    /* Reference to counter manager's exported counters (read only) */
    const std::vector<FdsCounters*>& counters_ref_;
    /* Reference to counter manager's snapshot counters */
    std::vector<FdsCounters*>& snapshot_counters_;
};

/**
 * @brief Counter manager.  Mananges the job of exporting registered
 * FdsCounters class objects in various different formats.
 * Supported format are:
 * -Graphite
 */
class FdsCountersMgr : public boost::noncopyable {
public:
    FdsCountersMgr(const std::string &id);
    ~FdsCountersMgr() {timer_.destroy();}
    void add_for_export(FdsCounters *counters);
    void remove_from_export(FdsCounters *counters);

    FdsCounters* get_counters(const std::string &id);

    std::string export_as_graphite();

    void export_to_ostream(std::ostream &stream);

    void toMap(std::map<std::string, int64_t>& m);

    void reset();

protected:
    std::string id_;
    /* Counter objects that are exported out */
    std::vector<FdsCounters*> exp_counters_;
    /* Lock for this object */
    fds_mutex lock_;
    /* Timer */
    FdsTimer timer_;
    /* Sampler task */
    boost::shared_ptr<FdsTimerTask> sampler_ptr_;
    /* Snapshot */
    std::vector<FdsCounters*> snapshot_counters_;
};

/**
 * @brief Base counters class.  Any module that has a set of counters
 * should derive from this class
 */
class FdsCounters : public boost::noncopyable { 
public:
    FdsCounters(const std::string &id, FdsCountersMgr *mgr);
    FdsCounters(const FdsCounters& counters);
    /* Exposed for mock testing */
    FdsCounters();
    virtual ~FdsCounters();

    std::string id() const;

    std::string toString();

    void toMap(std::map<std::string, int64_t>& m) const;

    void reset();

protected:
    void add_for_export(FdsBaseCounter* cp);

    void remove_from_export(FdsBaseCounter* cp);

protected:
    /* Exported counters */
    std::vector<FdsBaseCounter*> exp_counters_;
    /* Id of this counter set */
    std::string id_;

    friend class FdsBaseCounter;
    friend class FdsCountersMgr;
};

/**
 * @brief Base class for counters that are to be exported.
 */
class FdsBaseCounter : public boost::noncopyable {
public:
    FdsBaseCounter(const std::string &id, FdsCounters *export_parent);
    FdsBaseCounter(const std::string &id, fds_volid_t volid, 
                    FdsCounters *export_parent);
    FdsBaseCounter(const FdsBaseCounter& c);
    /* Exposed for testing */
    FdsBaseCounter();
    virtual ~FdsBaseCounter();

    virtual uint64_t value() const = 0;
    virtual std::string id() const;
    virtual void set_id(std::string id);
    virtual fds_volid_t volid() const;
    virtual void set_volid(fds_volid_t volid);
    virtual bool volid_enable() const;
    virtual void reset() = 0;

private:
    std::string id_;
    bool volid_enable_;
    fds_volid_t volid_;
};


/**
 * @brief Numeric counter
 */
class NumericCounter : public FdsBaseCounter
{
public:
    NumericCounter(const std::string &id, fds_volid_t volid, 
                    FdsCounters *export_parent);
    NumericCounter(const std::string &id, FdsCounters *export_parent);
    NumericCounter(const NumericCounter &c);
    /* Exposed for testing */
    NumericCounter();

    virtual uint64_t value() const override;

    virtual void reset() override;

    void incr();
    void incr(const uint64_t v);

private:
    std::atomic<uint64_t> val_;
};

/**
 * @brief Latency Counter
 */
class LatencyCounter : public FdsBaseCounter
{
public:
    LatencyCounter(const std::string &id, fds_volid_t volid, 
                        FdsCounters *export_parent);
    LatencyCounter(const std::string &id, FdsCounters *export_parent);
    LatencyCounter(const LatencyCounter &c);

    /* Exposed for testing */
    LatencyCounter();

    virtual uint64_t value() const override;

    virtual void reset() override;

    void update(const uint64_t &val, uint64_t cnt = 1);

    virtual LatencyCounter & operator +=(const LatencyCounter & rhs);

    inline double latency() const {
        uint64_t cnt = count();
        if (!cnt) {
            return 0;
        }
        return static_cast<double>(total_latency()) / cnt;
    }

    inline uint64_t total_latency() const {
        return total_latency_.load();
    }

    inline uint64_t count() const {
        return cnt_.load();
    }

    inline uint64_t min_latency() const {
        return min_latency_.load();
    }

    inline uint64_t max_latency() const {
        return max_latency_.load();
    }

private:
    std::atomic<uint64_t> total_latency_;
    std::atomic<uint64_t> cnt_;
    std::atomic<uint64_t> min_latency_;
    std::atomic<uint64_t> max_latency_;
};

}  // namespace fds

#endif  // SOURCE_INCLUDE_FDS_COUNTERS_H_
