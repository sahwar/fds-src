/* Copyright 2013 Formation Data Systems, Inc.
 */
#include "fds_counters.h"

#include <limits>
#include <sstream>
#include <ctime>
#include <chrono>


namespace fds {

/* Snapshot all exported counters to snapshot_counters_*/
void SamplerTask::runTimerTask()
{
    fds_mutex::scoped_lock lock(lock_);
    for (auto counters : counters_ref_) {
        snapshot_counters_.push_back(new FdsCounters(*counters));
    }
}

FdsCountersMgr::FdsCountersMgr(const std::string &id)
    : id_(id),
      lock_("Counters mutex"),
      timer_()
{
    sampler_ptr_ = boost::shared_ptr<FdsTimerTask>(new SamplerTask(timer_, exp_counters_, snapshot_counters_));
    // bool ret = timer_.scheduleRepeated(sampler_ptr_, std::chrono::milliseconds(1000));
}

/**
 * @brief Adds FdsCounters object for export.  
 *
 * @param counters - counters object to export.  Do not delete
 * counters before invoking remove_from_export 
 */
void FdsCountersMgr::add_for_export(FdsCounters *counters)
{
    fds_mutex::scoped_lock lock(lock_);
    exp_counters_.push_back(counters);
}

/**
 * @brief 
 *
 * @param counters
 */
void FdsCountersMgr::remove_from_export(FdsCounters *counters)
{
    fds_verify(!"Not implemented yet");
}

/**
 * Returns counters identified by id
 *
 * @param id
 * @return
 */
FdsCounters* FdsCountersMgr::get_counters(const std::string &id)
{
    fds_mutex::scoped_lock lock(lock_);
    /* Iterate till we find the counters we care about.
     * NOTE: When we have a lot of exported counters this is
     * inefficient.  For now this should be fine.
     */
    for (auto c : exp_counters_) {
        if (c->id() == id) {
            return c;
        }
    }
    return nullptr;
}

/**
 * @brief Constructs graphite string out of the counters objects registered for
 * export
 *
 * @return 
 */
std::string FdsCountersMgr::export_as_graphite()
{
    std::ostringstream oss;

    fds_mutex::scoped_lock lock(lock_);

    std::time_t ts = std::time(NULL);

    for (auto counters : exp_counters_) {
        std::string counters_id = counters->id();
        for (auto c : counters->exp_counters_) {
            bool lat = typeid(*c) == typeid(LatencyCounter);
            std::string strId = lat ? c->id() + "." + std::to_string(c->volid()) +
                    ".latency" : c->id() + "." + std::to_string(c->volid());
            oss << id_ << "." << counters_id << "." << strId << " " << c->value() << " "
                    << ts << std::endl;
            if (lat) {
                strId = c->id() + "." + std::to_string(c->volid()) + ".count";
                oss << id_ << "." << counters_id << "." << strId << " " <<
                    dynamic_cast<LatencyCounter*>(c)->count()
                        << " " << ts << std::endl;
            }
        }
    }
    return oss.str();
}

/**
 * Exports to output stream
 * @param stream
 */
void FdsCountersMgr::export_to_ostream(std::ostream &stream)  // NOLINT
{
    fds_mutex::scoped_lock lock(lock_);

    for (auto counters : exp_counters_) {
        std::string counters_id = counters->id();
        for (auto c : counters->exp_counters_) {
            bool lat = typeid(*c) == typeid(LatencyCounter);
            std::string strId = lat ? c->id() + "." + std::to_string(c->volid()) +
                    ".latency" : c->id() + "." + std::to_string(c->volid());
            stream << id_ << "." << counters_id << "." << strId << "\t\t" << c->value()
                    << std::endl;
            if (lat) {
                strId = c->id() + "." + std::to_string(c->volid()) + ".count";
                stream << id_ << "." << counters_id << "." << strId << "\t\t" <<
                        dynamic_cast<LatencyCounter*>(c)->count() << std::endl;
            }
        }
    }
}

/**
 * Converts to map
 * @param m
 */
void FdsCountersMgr::toMap(std::map<std::string, int64_t>& m)
{
    fds_mutex::scoped_lock lock(lock_);

    for (auto counters : exp_counters_) {
        std::string counters_id = counters->id();
        for (auto c : counters->exp_counters_) {
            bool lat = typeid(*c) == typeid(LatencyCounter);
            std::string strId = lat ? c->id() + "." + std::to_string(c->volid()) +
                    ".latency" : c->id() + "." + std::to_string(c->volid());
            m[strId] = static_cast<int64_t>(c->value());
            if (lat) {
                strId = c->id() + "." + std::to_string(c->volid()) + ".count";
                m[strId] = static_cast<int64_t>(
                        dynamic_cast<LatencyCounter*>(c)->count());
            }
        }
    }
}

/**
 * reset counters
 */
void FdsCountersMgr::reset()
{
    fds_mutex::scoped_lock lock(lock_);
    for (auto counters : exp_counters_)
        counters->reset();
}

/**
 * Constructor
 * @param id
 * @param mgr
 */
FdsCounters::FdsCounters(const std::string &id, FdsCountersMgr *mgr)
: id_(id)
{
    if (mgr) {
        mgr->add_for_export(this);
    }
}

/**
 * Copy constructor
 */
FdsCounters::FdsCounters(const FdsCounters& counters) : id_(counters.id_)
{
    for (auto c : counters.exp_counters_) {
        bool lat = typeid(*c) == typeid(LatencyCounter);
        if (lat) {
            exp_counters_.push_back(new LatencyCounter(dynamic_cast<LatencyCounter&>(*c)));
        } else {
            exp_counters_.push_back(new NumericCounter(dynamic_cast<NumericCounter&>(*c)));
        }
    }
}

/**
 * Exposed for mock testing
 */
FdsCounters::FdsCounters() {}

/**
 * Destructor
 */
FdsCounters::~FdsCounters() {}

/**
 * Returns id
 * @return
 */
std::string FdsCounters::id() const
{
    return id_;
}

/**
 * Converts to string
 * @return
 */
std::string FdsCounters::toString()
{
    std::ostringstream oss;
    for (auto c : exp_counters_) {
        bool lat = typeid(*c) == typeid(LatencyCounter);
        std::string strId = lat ? c->id() + ".latency" : c->id();
        oss << id_ << "." << "." << strId << " = " << c->value() << "; ";
        if (lat) {
            strId = c->id() + ".count";
            oss << id_ << "." << "." << strId << " = " <<
                dynamic_cast<LatencyCounter*>(c)->count() << "; ";
        }
    }
    return oss.str();
}

/**
 * Converts to map
 * @param m
 */
void FdsCounters::toMap(std::map<std::string, int64_t>& m) const  // NOLINT
{
    for (auto c : exp_counters_) {
        bool lat = typeid(*c) == typeid(LatencyCounter);
        std::string strId = lat ? c->id() + ".latency" : c->id();
        m[strId] = static_cast<int64_t>(c->value());
        if (lat) {
            strId = c->id() + ".count";
            m[strId] = static_cast<int64_t>(
                    dynamic_cast<LatencyCounter*>(c)->count());
        }
    }
}

/**
 * Reset counters
 */
void FdsCounters::reset()  // NOLINT
{
    for (auto c : exp_counters_) {
	c->reset();
    }
}

/**
 * @brief Marks the counter for export.  Only export counters
 * that are members of the derived class.  This method is invoked
 * by FdsBaseCounter constructor to mark the counter as exported.
 *
 * @param cp Counter to export
 */
void FdsCounters::add_for_export(FdsBaseCounter* cp)
{
    fds_assert(cp);
    exp_counters_.push_back(cp);
}

void FdsCounters::remove_from_export(FdsBaseCounter* cp)
{
    fds_verify(!"Not implemented yet");
}

/**
 * @brief  Base counter constructor.  Enables a counter to
 * be exported with an identifier.  If export_parent is NULL
 * counter will not be exported.
 * 
 * Providing constructor with volume id enabled and without
 *
 * @param id - id to use when exporting the counter
 * @vol id - volume id (used for export)
 * @param export_parent - Pointer to the parent.  If null counter
 * is not exported.
 */

FdsBaseCounter::FdsBaseCounter(const std::string &id, 
                                FdsCounters *export_parent)
: id_(id), volid_enable_(false), volid_(0) 
{
    if (export_parent) {
        export_parent->add_for_export(this);
    }
}


FdsBaseCounter::FdsBaseCounter(const std::string &id, fds_volid_t volid, 
                                FdsCounters *export_parent)
: id_(id), volid_enable_(true), volid_(volid) 
{
    if (export_parent) {
        export_parent->add_for_export(this);
    }
}

/**
 * Copy Constructor
 */
FdsBaseCounter::FdsBaseCounter(const FdsBaseCounter &c)
: id_(c.id_), volid_enable_(c.volid_enable_), volid_(c.volid_)
{
}

/**
 * Exposed for mock testing
 */
FdsBaseCounter::FdsBaseCounter() {}

/**
 * Destructor
 */
FdsBaseCounter::~FdsBaseCounter()
{
}

/**
 * Returns id
 * @return
 */
std::string FdsBaseCounter::id() const
{
    return id_;
}

/**
 * Sets id
 * @param id
 */
void FdsBaseCounter::set_id(std::string id)
{
    id_ = id;
}

/**
 * Returns volid
 * @return
 */
fds_volid_t FdsBaseCounter::volid() const
{
    return volid_;
}

/**
 * Returns volid_enable
 * @return
 */
bool FdsBaseCounter::volid_enable() const
{
    return volid_enable_;
}

/**
 * Sets volid_
 */
void FdsBaseCounter::set_volid(fds_volid_t volid)
{
    volid_ = volid;
}


/**
 * Constructor
 * @param id
 * @param export_parent
 */
NumericCounter::NumericCounter(const std::string &id, fds_volid_t volid, 
                                FdsCounters *export_parent)
:   FdsBaseCounter(id, volid, export_parent)
{
    val_ = 0;
    min_value_ = std::numeric_limits<uint64_t>::max();
    max_value_ = 0;
}

NumericCounter::NumericCounter(const std::string &id, FdsCounters *export_parent)
: FdsBaseCounter(id, export_parent)
{
    val_ = 0;
    min_value_ = std::numeric_limits<uint64_t>::max();
    max_value_ = 0;
}
/**
 * Copy Constructor
 */
NumericCounter::NumericCounter(const NumericCounter& c)
: FdsBaseCounter(c)
{
    val_ = c.val_.load(std::memory_order_relaxed);
    min_value_ = c.min_value_.load(std::memory_order_relaxed);
    max_value_ = c.max_value_.load(std::memory_order_relaxed);
}

/**
 *  Exposed for testing
 */
NumericCounter::NumericCounter() {}
/**
 *
 * @return
 */
uint64_t NumericCounter::value() const
{
    return val_.load(std::memory_order_relaxed);
}

/**
 * reset counter
 */
void NumericCounter::reset()
{
    val_.store(0, std::memory_order_relaxed);
    min_value_.store(std::numeric_limits<uint64_t>::max(), std::memory_order_relaxed);
    max_value_.store(0, std::memory_order_relaxed);;
}
/**
 *
 */
void NumericCounter::incr() {
    incr(1);
}

/**
 *
 * @param v
 */
void NumericCounter::incr(const uint64_t v) {
    auto val = val_.fetch_add(v, std::memory_order_relaxed) + v;
    auto old_max = max_value_.load(std::memory_order_consume);
    if (old_max < val)
        { max_value_.store(val, std::memory_order_release); }
}

/**
 *
 */
void NumericCounter::decr() {
    decr(1);
}

/**
 *
 * @param v
 */
void NumericCounter::decr(const uint64_t v) {
    auto val = val_.fetch_sub(v, std::memory_order_relaxed) - v;
    auto old_min = min_value_.load(std::memory_order_consume);
    if (old_min > val)
        { min_value_.store(val, std::memory_order_release); }
}

/**
 *
 * @param id
 * @param export_parent
 */
LatencyCounter::LatencyCounter(const std::string &id, fds_volid_t volid, 
                                FdsCounters *export_parent)
    :   FdsBaseCounter(id, volid, export_parent),
        total_latency_(0),
        cnt_(0),
        min_latency_(std::numeric_limits<uint64_t>::max()),
        max_latency_(0) {}

LatencyCounter::LatencyCounter(const std::string &id, FdsCounters *export_parent)
    :   FdsBaseCounter(id, export_parent),
        total_latency_(0),
        cnt_(0),
        min_latency_(std::numeric_limits<uint64_t>::max()),
        max_latency_(0) {}


/**
 * Copy Constructor
 */
LatencyCounter::LatencyCounter(const LatencyCounter &c)
    :   FdsBaseCounter(c),
        total_latency_(c.total_latency_.load()),
        cnt_(c.cnt_.load()),
        min_latency_(c.min_latency_.load()),
        max_latency_(c.max_latency_.load()) {}


/**
 * Exposed for testing
 */
LatencyCounter::LatencyCounter()
    :   total_latency_(0),
        cnt_(0),
        min_latency_(std::numeric_limits<uint64_t>::max()),
        max_latency_(0) {}

/**
 *
 * @return
 */
uint64_t LatencyCounter::value() const
{
    uint64_t cnt = count();
    if (cnt == 0) {
        return 0;
    }
    return total_latency() / cnt;
}

/**
 * reset counter
 */
void LatencyCounter::reset()
{
    total_latency_ = 0;
    cnt_ = 0;
    min_latency_ = std::numeric_limits<uint64_t>::max();
    max_latency_ = 0;
}

/**
 *
 * @param val
 * @param cnt
 */
void LatencyCounter::update(const uint64_t &val, uint64_t cnt /* = 1 */) {
    total_latency_.fetch_add(val);
    cnt_.fetch_add(cnt);
    if (1 == cnt) {
        if (val < min_latency()) {
            min_latency_.store(val);
        }
        if (val > max_latency()) {
            max_latency_.store(val);
        }
    }
}

LatencyCounter & LatencyCounter::operator +=(const LatencyCounter & rhs) {
    if (&rhs != this) {
        update(rhs.total_latency(), rhs.count());

        uint64_t rhs_min = rhs.min_latency();
        if (rhs_min < min_latency()) {
            min_latency_.store(rhs_min);
        }

        uint64_t rhs_max = rhs.max_latency();
        if (rhs_max > max_latency()) {
            max_latency_.store(rhs_max);
        }
    }
    return *this;
}

}  // namespace fds
