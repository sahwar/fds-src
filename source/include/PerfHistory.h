/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_PERFHISTORY_H_
#define SOURCE_INCLUDE_PERFHISTORY_H_

#include <string>
#include <vector>
#include <unordered_map>

#include <fds_types.h>
#include <serialize.h>
#include <fds_error.h>
#include <fds_typedefs.h>
#include <concurrency/RwLock.h>
#include <fdsp/fds_service_types.h>
#include <StatTypes.h>

namespace fds {

class GenericCounter: public serialize::Serializable {
  public:
    GenericCounter();
    explicit GenericCounter(const FdsBaseCounter& c);
    explicit GenericCounter(const GenericCounter& c);
    ~GenericCounter();

    void reset();
    void add(fds_uint64_t val);
    GenericCounter & operator +=(const GenericCounter & rhs);
    GenericCounter & operator +=(const fds_uint64_t & val);
    GenericCounter & operator =(const GenericCounter & rhs);
    GenericCounter & operator =(const fds_uint64_t & val);

    /**
     * Getters
     */
    inline fds_uint64_t min() const { return min_; }
    inline fds_uint64_t max() const { return max_; }
    inline fds_uint64_t count() const { return count_; }
    inline fds_uint64_t total() const { return total_; }
    inline double average() const {
        if (count_ == 0) return 0;
        return static_cast<double>(total_) / count_;
    }
    inline double countPerSec(fds_uint32_t interval_sec) const {
        fds_verify(interval_sec > 0);
        return static_cast<double>(count_) / interval_sec;
    }

    /*
     * For serializing and de-serializing
     */
    uint32_t virtual write(serialize::Serializer* s) const;
    uint32_t virtual read(serialize::Deserializer* d);

    friend std::ostream& operator<< (std::ostream &out,
                                     const GenericCounter& counter);

  private:
    fds_uint64_t total_;
    fds_uint64_t count_;
    fds_uint64_t min_;
    fds_uint64_t max_;
};

typedef std::unordered_map<FdsStatType, GenericCounter, FdsStatHash> counter_map_t;

/**
 * This class describes one slot in the volume performance history
 */
class StatSlot: public serialize::Serializable {
  public:
    StatSlot();
    StatSlot(const StatSlot& slot);
    ~StatSlot();

    /**
     * Call this method before filling in the stat; resets to
     * record stats for timestamp 'rel_sec'
     * @param[in] rel_sec relative time in seconds
     * @param[in] stat_size is time interval for the stat; if
     * stat_size is given 0, then will use time interval that is
     * already (previously) set
     */
    void reset(fds_uint64_t rel_sec, fds_uint32_t stat_size = 0);

    /**
     * Add counter of type 'counter_type' to this slot
     */
    void add(FdsStatType stat_type,
             const GenericCounter& counter);
    void add(FdsStatType stat_type,
             fds_uint64_t value);

    /**
     * slot length must match, but ignores the timestamp of rhs
     */
    StatSlot& operator +=(const StatSlot & rhs);
    StatSlot& operator =(const StatSlot & rhs);

    inline fds_uint64_t getTimestamp() const { return rel_ts_sec; }
    inline fds_uint32_t slotLengthSec() const { return interval_sec; }
    fds_bool_t isEmpty() const;
    /**
     * count / time interval of this slot in seconds
     */
    double getEventsPerSec(FdsStatType type) const;
    /**
     * total value of event 'type'
     */
    fds_uint64_t getTotal(FdsStatType type) const;
    fds_uint64_t getCount(FdsStatType type) const;
    /**
     * total / count for event 'type'
     */
    double getAverage(FdsStatType type) const;
    double getMin(FdsStatType type) const;
    double getMax(FdsStatType type) const;
    /**
     * Get counter of a given type
     */
    void getCounter(FdsStatType type,
                    GenericCounter* out_counter) const;

    /*
     * For serializing and de-serializing
     */
    uint32_t virtual write(serialize::Serializer* s) const;
    uint32_t virtual read(serialize::Deserializer* d);

    friend std::ostream& operator<< (std::ostream &out,
                                     const StatSlot& slot);

  private:
    counter_map_t stat_map;
    fds_uint64_t rel_ts_sec;  /**< timestamp in seconds, relative */
    fds_uint32_t interval_sec;  /**< time interval of this slot */
};

/**
 * Recent volume's history of aggregated performance counters, where each
 * time slot in history is same time interval and time interval is
 * configurable. History is implemented as circular array.
 */
class VolumePerfHistory {
  public:
    VolumePerfHistory(fds_volid_t volid,
                      fds_uint64_t start_ts,
                      fds_uint32_t slots,
                      fds_uint32_t slot_len_sec);
    ~VolumePerfHistory();

    typedef boost::shared_ptr<VolumePerfHistory> ptr;
    typedef boost::shared_ptr<const VolumePerfHistory> const_ptr;

    /**
     * Record aggregated Perf counter into the history
     * @param[in] ts (not relative!) timestamp in nanos of the counter
     * @param[in] counter_type type of the counter
     * @param[in] counter is the counter to record
     */
    void recordPerfCounter(fds_uint64_t ts,
                           FdsStatType stat_type,
                           const GenericCounter& counter);

    /**
     * Record one event into the history
     * @param[in] ts (not relative!) timestamp in nanos of the counter
     * @param[in] stat_type type of the counter
     * @param[in] value is the value of the stat
     */
    void recordEvent(fds_uint64_t ts,
                     FdsStatType stat_type,
                     fds_uint64_t value);

    /**
     * Returns simple moving average of a given stat type
     * The last timeslot is not counted in the moving average, because
     * it may not be fully filled in
     * @param stat_type stat for which we calculate SMA
     * @param end_ts last timestamp to consider for the SMA, 0 if consider
     * the timestamp of the last fully filled timeslot
     * @param interval_sec interval in seconds for which calculate SMA, 0
     * if calculate for the whole history
     */
    double getSMA(FdsStatType stat_type,
                  fds_uint64_t end_ts = 0,
                  fds_uint32_t interval_sec = 0);

    /**
     * Add slots from fdsp message to this history
     * It is up to the caller that we don't add the same slot more than once
     * @param[in] fdsp_start_ts is remote start timestamp; all relative timestamps
     * in fdsp message are relative to fdsp_start_ts
     */
    Error mergeSlots(const fpi::VolStatList& fdsp_volstats,
                     fds_uint64_t fdsp_start_ts);
    void mergeSlots(const std::vector<StatSlot>& stat_list,
                    fds_uint64_t remote_start_ts);

    /**
     * Copies history into FDSP volume stat list; only timestamps that are
     * greater than last_rel_sec are copied
     * @param[in] last_rel_sec is a relative timestamp in seconds
     * @return last timestamp copied into FDSP volume stat list
     */
    fds_uint64_t toFdspPayload(fpi::VolStatList& fdsp_volstats,
                               fds_uint64_t last_rel_sec);

    /**
     * Copies history in StatSlot array, where index 0 constains the
     * earliest timestamp copied. Only timestamps that are greater than
     * last_rel_sec are copied
     * @param[in] last_rel_sec is a relative timestamp in seconds
     * @param[in] last_seconds_to_ignore number of most recent seconds
     * that will not be copied into the list; 0 means copy all most recent
     * history
     * @return last timestamp copied into stat_list
     */
    fds_uint64_t toSlotList(std::vector<StatSlot>& stat_list,
                            fds_uint64_t last_rel_sec,
                            fds_uint32_t last_seconds_to_ignore = 0);

    friend std::ostream& operator<< (std::ostream &out,
                                     const VolumePerfHistory& hist);

    /**
     * For now to maintain same file format when printing fine-grain qos stats
     * so that our gnuplot printing scripts works
     * Format:
     * [curts],volid,seconds_since_beginning_of_history,iops,ave_lat,min_lat,max_lat
     * @param[in] cur_ts timestamp that will be printed
     * @param[in] last_rel_sec will print relative timestamps in seconds > last_rel_sec
     * @return last timestamp printed
     */
    fds_uint64_t print(std::ofstream& dumpFile,
                       boost::posix_time::ptime curts,
                       fds_uint64_t last_rel_sec);

    VolumePerfHistory::ptr getSnapshot();
    inline fds_uint64_t getTimestamp(fds_uint64_t rel_seconds) const {
        return (start_nano_ + rel_seconds * 1000000000);
    }

  private:  /* methods */
    fds_uint32_t useSlotLockHeld(fds_uint64_t rel_seconds);
    inline fds_uint64_t tsToRelativeSec(fds_uint64_t tsnano) const {
        fds_verify(tsnano >= start_nano_);
        return (tsnano - start_nano_) / 1000000000;
    }
    inline fds_uint64_t timestamp(fds_uint64_t start_nano,
                                  fds_uint64_t rel_sec) const {
        return (start_nano + rel_sec * 1000000000);
    }
    fds_uint64_t getLocalRelativeSec(fds_uint64_t remote_rel_sec,
                                     fds_uint64_t remote_start_ts);

  private:
    /**
     * Configurable parameters
     */
    fds_volid_t volid_;       /**< volume id  */
    fds_uint32_t nslots_;     /**< number of slots in history */
    fds_uint32_t slotsec_;    /**< slot length is seconds */

    fds_uint64_t start_nano_;  /**< ts in history are relative to this ts */

    /**
     * Array of time slots with stats; once we fill in the last slot
     * in the array, we will circulate and re-use the first slot for the
     * next time interval and so on
     */
    StatSlot* stat_slots_;
    fds_uint64_t last_slot_num_;  /**< sequence number of the last slot */
    fds_rwlock stat_lock_;  // protects stat_slots and last_slot_num
};

}  // namespace fds

#endif  // SOURCE_INCLUDE_PERFHISTORY_H_
