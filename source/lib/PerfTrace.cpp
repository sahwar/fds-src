#include "PerfTrace.h"

#include <stdexcept>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "util/timeutils.h"
#include "fds_process.h"

// XXX: uncomment if name space for context is limited
//#define CACHE_REGEX_MATCH_RESULTS

// XXX: uncomment this to use rdtsc()
//#define USE_RDTSC_TIME

namespace {

typedef fds_int64_t fds_volid_t; // FIXME(matteo): ugly

// start garbage collection for PerfContext if entries in latencyMap_
// reaches this number
const unsigned MAX_PENDING_PERF_CONTEXTS = 25000;

void createLatencyCounter(fds::PerfContext & ctx) {
    fds_assert(ctx.start_cycle);
    fds_assert(ctx.start_cycle <= ctx.end_cycle);

    fds::LatencyCounter * plc = new fds::LatencyCounter(ctx.name, ctx.volid, 0);
    plc->update(ctx.end_cycle - ctx.start_cycle);
    ctx.data.reset(plc);
}
// FIXME(matteo): ctx may be useless here
template <typename T>
void initializeCounter(fds::PerfContext * ctx, fds::FdsCounters * parent, 
                    const fds::PerfEventType & type, const fds::fds_volid_t volid,
                    std::string name) {
    fds_assert(ctx);
    fds_assert(0 == ctx->data.get());
    GLOGDEBUG << "Creating performance counter for type='" << fds::eventTypeToStr[ctx->type]
            << "' volid='" << ctx->volid
            << "' name='" << ctx->name
            << "' from type='" << fds::eventTypeToStr[type]
            << "' volid='" << volid
            << "' name='" << name << "' ";

    std::string counterName = fds::eventTypeToStr[type];
    if (!name.empty()) {
        counterName += "." + name;
    }

    ctx->data.reset(new T(counterName, volid, parent));
}

void stringToEventsFilter(const std::string & str, std::bitset<fds::MAX_EVENT_TYPE> & filter) {
    // Sanitize the input and return bitset
    std::vector<std::string> tokenVec;
    boost::split(tokenVec, str, boost::is_any_of(","));
    for(std::string & str : tokenVec) {
        boost::trim(str);
        unsigned startPos = fds::MAX_EVENT_TYPE;
        unsigned endPos = startPos;
        if (std::string::npos == str.find('-')) {
            startPos = endPos = boost::lexical_cast<unsigned>(str);
        } else {

            std::vector<std::string> rangeVec;
            boost::split(rangeVec, str, boost::is_any_of("-"));
            if (2 != rangeVec.size()) {
                throw std::runtime_error("Invalid event filter string");
            }

            boost::trim(rangeVec[0]);
            startPos = boost::lexical_cast<unsigned>(rangeVec[0]);
            boost::trim(rangeVec[1]);
            endPos = boost::lexical_cast<unsigned>(rangeVec[1]);
        }

        if (fds::MAX_EVENT_TYPE <= startPos) {
            throw std::runtime_error("Invalid start value in a range");
        }
        if (endPos < startPos) {
            throw std::runtime_error("Invalid end value in a range");
        }

        if (fds::MAX_EVENT_TYPE <= endPos) {
            endPos = fds::MAX_EVENT_TYPE - 1;
        }

        for(unsigned pos = startPos; pos <= endPos; ++pos) {
            filter[pos] = 1;
        }
    }
}

} /* namespace (anonymous) */

namespace fds {

const std::string PERF_COUNTERS_NAME("perf");

const unsigned PERF_CONTEXT_TIMEOUT = 1800; // in seconds (30 mins)

const char * eventTypeToStr[] = {
        "trace", // generic event
        "trace_err",

        // Store Manager
        "put_io",
        "put_obj_req",
        "put_obj_req_err",
        "put_trans_queue_wait",
        "put_qos_queue_wait",
        "put_cache_hit",

        "duplicate_obj",
        "hash_collision",

        "get_io",
        "get_obj_req",
        "get_obj_req_err",
        "get_trans_queue_wait",
        "get_qos_queue_wait",
        "get_cache_hit",

        "delete_io",
        "delete_obj_req",
        "delete_obj_req_err",
        "delete_trans_queue_wait",
        "delete_qos_queue_wait",
        "delete_cache_hit",

        "murmur3_hash",
        "dlt_lkup",
        "dmt_lkup",

        "put_obj_dedupe_chk",
        "persist_disk_write",
        "put_obj_loc_indx_update",

        "get_obj_cache_lkup",
        "get_obj_lkup_loc_indx",
        "get_obj_pl_read_disk",

        "commit_log_write",
        "get_metadata_read",
        "get_disk_read",
        "put_metadata_write",
        "put_disk_write",
        "delete_metadata",
        "delete_disk",

        "put_odb",
        "get_odb",
        "disk_write",
        "disk_read",

        // Access Manager
        "am_put_obj_req",
        "am_put_qos",
        "am_put_hash",
        "am_put_sm",
        "am_put_dm",

        "am_get_obj_req",
        "am_get_qos",
        "am_get_hash",
        "am_get_sm",
        "am_get_dm",

        "am_delete_obj_req",
        "am_delete_qos",
        "am_delete_hash",
        "am_delete_sm",
        "am_delete_dm",

        // Data Manager
        "dm_tx_op",
        "dm_tx_op_err",
        "dm_tx_op_req",
        "DM_TX_OP_REQ_ERR",

        "dm_tx_commit_req",
        "dm_tx_commit_req_err",
        "dm_vol_cat_write",
        "dm_tx_qos_wait",

        "dm_query_req",
        "dm_query_req_err",
        "dm_vol_cat_read",
        "dm_query_qos_wait",

        "dm_cache_hit"
};

PerfTracer::PerfTracer() : aggregateCounters_(fds::MAX_EVENT_TYPE),
                           namedCounters_(fds::MAX_EVENT_TYPE),
                           enable_(true),
                           useEventsFilter_(false),
                           nameFilter_("^$"),
                           useNameFilter_(false),
                           config_helper_(g_fdsprocess->get_conf_helper()) {
    fds_assert(g_cntrs_mgr);
    fds_assert(g_fdslog);

    GLOGDEBUG << "Instantiating PerfTracer";

    exportedCounters.reset(new FdsCounters(PERF_COUNTERS_NAME, g_cntrs_mgr.get()));

    reconfig();
}

PerfTracer::~PerfTracer() {
    GLOGDEBUG << "Destroying PerfTracer";
    for (auto& kv : latencyMap_) {
        delete kv.second;
    }
    latencyMap_.clear();

    for (auto & e : namedCounters_) {
        for (auto& kv : e) { 
            for (auto& kvv : kv.second) {
                //TODO: exportedCounters->remove_from_export(kv.second.data);
                delete kvv.second;
            }
            kv.second.clear();
        }
        e.clear();
    }
    namedCounters_.clear();
}

PerfTracer & PerfTracer::instance() {
    static PerfTracer instance_;
    return instance_;
}

void PerfTracer::reconfig() {
    GLOGDEBUG << "Reading PerfTrace configuration";

    // read global enable/ disable settings
    bool tmpEnable = config_helper_.get<bool>(PERF_COUNTERS_NAME + "." + "enable", false);

    // read events filter settings
    const std::string eventsFilterKey(PERF_COUNTERS_NAME + "." + "event_types");
    std::bitset<MAX_EVENT_TYPE> efilt;
    bool tmpUseEventsFilter = config_helper_.exists(eventsFilterKey);
    if (tmpUseEventsFilter) {
        std::string efStr = config_helper_.get<std::string>(eventsFilterKey);

       // process events filter settings
       try {
           stringToEventsFilter(efStr, efilt);
       } catch (std::exception & e) {
           GLOGWARN << "Error while reading events filter, ignoring the configuration. "
                   << e.what();
           tmpUseEventsFilter = false;
       }
    }

    // read name filter settings
    const std::string nameFilterKey(PERF_COUNTERS_NAME + "." + "match");
    std::string exprStr;
    bool tmpUseNameFilter = config_helper_.exists(nameFilterKey);
    if (tmpUseNameFilter) {
        exprStr = config_helper_.get<std::string>(nameFilterKey);
    }
    if(exprStr.empty()) {
        tmpUseNameFilter = false;
    }

    FDSGUARD(config_mutex_);

    // update name filter config
    if (tmpUseNameFilter) {
        try {
            nameFilter_ = exprStr;
        } catch (boost::bad_expression & ex) {
            GLOGWARN << "Invalid expression in name filter, ignoring configuration";
            tmpUseNameFilter = false;
        }
    }
    useNameFilter_ = tmpUseNameFilter;

    // update events filter config
    if (tmpUseEventsFilter) {
        for (unsigned i = 0; i < MAX_EVENT_TYPE; ++i) {
            eventsFilter_[i] = efilt[i];
        }
    }
    useEventsFilter_ = tmpUseEventsFilter;

    // update global enable/ disable
    enable_ = tmpEnable;
}

void PerfTracer::updateCounter(PerfContext & ctx, const PerfEventType & type, 
        const uint64_t & val,  const uint64_t cnt, const fds_volid_t volid,
        const std::string name) {
    GLOGNORMAL << "Updating performance counter for type='" << eventTypeToStr[ctx.type]
            << "' val='" << val << "' count='" << cnt << "' name='"
            << ctx.name << "'";
    FdsCounters * counterParent = ctx.enabled ? exportedCounters.get() : 0;
    // update counter
    if (cnt) {
        std::call_once(*ctx.once, initializeCounter<LatencyCounter>, &ctx, counterParent, type, volid, name);
        LatencyCounter * plc = dynamic_cast<LatencyCounter *>(ctx.data.get());
        fds_assert(plc || !"Counter type mismatch between tracepoints!")
        plc->update(val, cnt);
    } else {
        std::call_once(*ctx.once, initializeCounter<NumericCounter>, &ctx, counterParent, type, volid, name);
        NumericCounter * pnc = dynamic_cast<NumericCounter *>(ctx.data.get());
        fds_assert(pnc || !"Counter type mismatch between tracepoints!")
        pnc->incr(val);
    } 
}

void PerfTracer::upsert(const PerfEventType & type, fds_volid_t volid, 
            uint64_t val, uint64_t cnt, const std::string & name) {
    PerfContext * ctx = 0;

    FDSGUARD(ptrace_mutex_named_);

    PerfContextMap::iterator pos = namedCounters_[type][volid].find(name);
    if (namedCounters_[type][volid].end() != pos) {
        if (!pos->second->enabled) {
            return; // disabled for this name
        }

        ctx = pos->second;
    } else {
        ctx = new PerfContext(type, volid, name);
        if (!name.empty()) {
#ifdef CACHE_REGEX_MATCH_RESULTS
            if (!regex_match(name, nameFilter_)) {
                ctx->enabled = false;
            }
#endif
        }

        namedCounters_[type][volid][name] = ctx;
    }

    // update counter
    updateCounter(*ctx, type, val, cnt, volid, name);
}

void PerfTracer::decrement(const PerfEventType & type, fds_volid_t volid, 
            uint64_t val, const std::string & name) {
    FDSGUARD(ptrace_mutex_named_);

    PerfContextMap::iterator pos = namedCounters_[type][volid].find(name);
    fds_assert(pos != namedCounters_[type][volid].end())
    if (!pos->second->enabled) {
        return; // disabled for this name
    }
    PerfContext * ctx = pos->second;
    // update counter
    NumericCounter * pnc = dynamic_cast<NumericCounter *>(ctx->data.get());
    fds_assert(pnc || !"Counter type mismatch between tracepoints!")
    pnc->decr(val);
}

void PerfTracer::incr(const PerfEventType & type, fds_volid_t volid, std::string name /* = "" */) {
    PerfTracer::incr(type, volid, 1, 0, name);
}

void PerfTracer::decr(const PerfEventType & type, fds_volid_t volid, std::string name /* = "" */) {
    PerfTracer::decr(type, volid, 1, name);
}

void PerfTracer::incr(const PerfEventType & type, fds_volid_t volid, 
        uint64_t val, uint64_t cnt /* = 0 */, std::string name /* = "" */) {
    fds_assert(type < MAX_EVENT_TYPE);

    // check if performance data collection is enabled
    if (!instance().enable_) {
        return;
    }

    if (instance().useEventsFilter_  && !instance().eventsFilter_[type]) {
        return;
    }

    // update aggregate counter first
    {
        FDSGUARD(instance().ptrace_mutex_aggregate_);
        instance().updateCounter(instance().aggregateCounters_[type][volid], type, val, cnt, volid, name);
    }
    if (!name.empty() && instance().useNameFilter_) {
#ifndef CACHE_REGEX_MATCH_RESULTS
        if (regex_match(name, instance().nameFilter_)) {
#endif
            instance().upsert(type, volid, val, cnt, name);
#ifndef CACHE_REGEX_MATCH_RESULTS
        }
#endif
    }
}

void PerfTracer::decr(const PerfEventType & type, fds_volid_t volid, 
        uint64_t val, std::string name /* = "" */) {
    fds_assert(type < MAX_EVENT_TYPE);

    // check if performance data collection is enabled
    if (!instance().enable_) {
        return;
    }

    if (instance().useEventsFilter_  && !instance().eventsFilter_[type]) {
        return;
    }

    // update aggregate counter first
    {
        FDSGUARD(instance().ptrace_mutex_aggregate_);
        PerfContext & ctx = instance().aggregateCounters_[type][volid];
        NumericCounter * pnc = dynamic_cast<NumericCounter *>(ctx.data.get());
        fds_assert(pnc || !"Counter type mismatch between tracepoints!")
        pnc->decr(val);
        
    }
    if (!name.empty() && instance().useNameFilter_) {
#ifndef CACHE_REGEX_MATCH_RESULTS
        if (regex_match(name, instance().nameFilter_)) {
#endif
            instance().decrement(type, volid, val, name);
#ifndef CACHE_REGEX_MATCH_RESULTS
        }
#endif
    }
}
void PerfTracer::tracePointBegin(const std::string & id, 
        const PerfEventType & type, fds_volid_t volid, 
        std::string name /* = "" */) {
    GLOGDEBUG << "Received tracePointBegin() for id='" << id << "' type='" <<
            eventTypeToStr[type] << "' name='" << name << "'  volid='" << volid << "'";
    PerfContext * ctx = new PerfContext(type, volid, name);
    tracePointBegin(*ctx);

    size_t sz = instance().latencyMap_.size();
    if (sz > MAX_PENDING_PERF_CONTEXTS) {
        GLOGWARN << "Too many trace points pending - started but not ended (" << sz << ")";
    }

    FDSGUARD(instance().latency_map_mutex_);
#ifdef DEBUG
    PerfContextMap::iterator pos = instance().latencyMap_.find(id);
    fds_assert(instance().latencyMap_.end() == pos);
#endif

    instance().latencyMap_[id] = ctx;
}

void PerfTracer::tracePointBegin(PerfContext & ctx) {
    GLOGDEBUG << "Starting perf trace for type='" << eventTypeToStr[ctx.type] << "' name='" <<
            ctx.name << "'";
#ifdef USE_RDTSC_TIME
    ctx.start_cycle = util::getNanosFromTicks(util::getClockTicks());
#else
    ctx.start_cycle = util::getTimeStampNanos();
#endif
}

boost::shared_ptr<PerfContext> PerfTracer::tracePointEnd(const std::string & id) {
    PerfContext * ppc = 0;

    GLOGDEBUG << "Received tracePointEnd() for id='" << id << "'";
    {
        FDSGUARD(instance().latency_map_mutex_);

        PerfContextMap::iterator pos = instance().latencyMap_.find(id);
        fds_assert(instance().latencyMap_.end() != pos);

        ppc = pos->second;
        instance().latencyMap_.erase(pos);
    }

    tracePointEnd(*ppc);

    return boost::shared_ptr<PerfContext>(ppc);
}

void PerfTracer::tracePointEnd(PerfContext & ctx) {
    GLOGDEBUG << "Ending perf trace for type='" << eventTypeToStr[ctx.type] << "' name='" <<
            ctx.name << "'";
#ifdef USE_RDTSC_TIME
    ctx.end_cycle = util::getNanosFromTicks(util::getClockTicks());
#else
    ctx.end_cycle = util::getTimeStampNanos();
#endif
    // Avoid creating a counter if counter has not been properly initialized. Print warning instead
    if (ctx.name != "" && ctx.type != TRACE) {
        createLatencyCounter(ctx);
        LatencyCounter * plc = dynamic_cast<LatencyCounter *>(ctx.data.get());
        incr(ctx.type, ctx.volid, plc->total_latency(), plc->count(), ctx.name);
    } else {
        GLOGDEBUG << "Counter wothout a name or type -  name: " << ctx.name << " type: " << eventTypeToStr[ctx.type];
    }
}

void PerfTracer::setEnabled(bool val /* = true */) {
    // FDSGUARD(instance().config_mutex_);
    instance().enable_ = val;
}

bool PerfTracer::isEnabled() {
    return instance().enable_;
}

void PerfTracer::refresh() {
    instance().reconfig();
}

} /* namespace fds */
