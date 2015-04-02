/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>
#include <limits>
#include <vector>
#include <fds_process.h>
#include <net/SvcMgr.h>
#include <util/math-util.h>
#include <DataMgr.h>
#include <StatStreamAggregator.h>
#include <fdsp/Streaming.h>
#include <time.h>

namespace fds {

#define MinLogRegId (std::numeric_limits<fds_uint32_t>::max())
#define HourLogRegId (std::numeric_limits<fds_uint32_t>::max() - 1)

class VolStatsTimerTask : public FdsTimerTask {
  public:
    StatStreamAggregator* aggr_;

    VolStatsTimerTask(FdsTimer &timer,
                      StatStreamAggregator* aggr)
            : FdsTimerTask(timer)
    {
        aggr_ = aggr;
    }
    ~VolStatsTimerTask() {}

    virtual void runTimerTask() override;
};

StatStreamTimerTask::StatStreamTimerTask(FdsTimer &timer,
                                         fpi::StatStreamRegistrationMsgPtr reg,
                                         StatStreamAggregator & statStreamAggr)
        : FdsTimerTask(timer), reg_(reg),
          statStreamAggr_(statStreamAggr) {
    for (auto volId : reg_->volumes) {
        vol_last_ts_[volId] = 0;
    }
}

void StatStreamTimerTask::cleanupVolumes(const std::vector<fds_volid_t>& cur_vols) {
    std::vector<fds_volid_t> rm_vols;
    for (auto i : vol_last_ts_) {
        fds_bool_t found = false;
        for (auto volId : cur_vols) {
            if (i.first == volId) {
                found = true;
                break;
            }
        }
        if (!found) rm_vols.push_back(i.first);
    }
    for (auto rmvol : rm_vols) {
        vol_last_ts_.erase(rmvol);
    }
}

VolumeStats::VolumeStats(fds_volid_t volume_id,
                         fds_uint64_t start_time,
                         const StatHistoryConfig& conf)
        : volid_(volume_id),
          finegrain_hist_(new VolumePerfHistory(volume_id,
                                                start_time,
                                                conf.finestat_slots_,
                                                conf.finestat_slotsec_)),
          coarsegrain_hist_(new VolumePerfHistory(volume_id,
                                                  start_time,
                                                  conf.coarsestat_slots_,
                                                  conf.coarsestat_slotsec_)),
          longterm_hist_(new VolumePerfHistory(volume_id,
                                               start_time,
                                               conf.longstat_slots_,
                                               conf.longstat_slotsec_)),
          long_stdev_update_ts_(0),
          recent_stdev_update_ts_(0),
          cap_recent_stdev_(0),
          cap_long_stdev_(0),
          cap_recent_wma_(0),
          perf_recent_stdev_(0),
          perf_long_stdev_(0),
          perf_recent_wma_(0) {
    last_print_ts_ = 0;
}

VolumeStats::~VolumeStats() {
}

void VolumeStats::processStats() {
    std::vector<StatSlot> slots;
    // note that we are passing time interval modules push stats to the aggregator
    // -- this is time interval back from current time that will be not included into
    // the list of slots returned, because these slots may not be fully filled yet, eg.
    // aggregator will receive stats from some modules but maybe not from all modules yet.
    last_print_ts_ = finegrain_hist_->toSlotList(slots, last_print_ts_,
                                                 0, 2*FdsStatPushPeriodSec);
    // first add the slots to coarse grain stat history (we do it here rather than on
    // receiving remote stats because it's more efficient)
    if (slots.size() > 0) {
        coarsegrain_hist_->mergeSlots(slots,
                                      finegrain_hist_->getStartTime());
        LOGDEBUG << "Updated coarse-grain stats: " << *coarsegrain_hist_;
        longterm_hist_->mergeSlots(slots,
                                   finegrain_hist_->getStartTime());
        LOGDEBUG << "Updated long-term stats: " << *longterm_hist_;
    }

    for (std::vector<StatSlot>::const_iterator cit = slots.cbegin();
         cit != slots.cend();
         ++cit) {
        // debugging for now, remove at some point
        LOGNORMAL << "[" << finegrain_hist_->getTimestamp((*cit).getTimestamp()) << "], "
                  << "Volume " << std::hex << volid_ << std::dec << ", "
                  << "Puts " << StatHelper::getTotalPuts(*cit) << ", "
                  << "Gets " << StatHelper::getTotalGets(*cit) << ", "
                  << "Queue full " << StatHelper::getQueueFull(*cit) << ", "
                  << "Logical bytes " << StatHelper::getTotalLogicalBytes(*cit) << ", "
                  << "Physical bytes " << StatHelper::getTotalPhysicalBytes(*cit) << ", "
                  << "Metadata bytes " << StatHelper::getTotalMetadataBytes(*cit) << ", "
                  << "Blobs " << StatHelper::getTotalBlobs(*cit) << ", "
                  << "Objects " << StatHelper::getTotalObjects(*cit) << ", "
                  << "Ave blob size " << StatHelper::getAverageBytesInBlob(*cit) << " bytes, "
                  << "Ave objects in blobs " << StatHelper::getAverageObjectsInBlob(*cit) << ", "
                  << "Gets from SSD " << StatHelper::getTotalSsdGets(*cit) << ", "
                  << "Short term perf wma " << perf_recent_wma_ << ", "
                  << "Short term perf sigma " << perf_recent_stdev_ << ", "
                  << "Long term perf sigma " << perf_long_stdev_ << ", "
                  << "Short term capacity sigma " << cap_recent_stdev_ << ", "
                  << "Short term capacity wma " << cap_recent_wma_ << ", "
                  << "Long term capacity sigma " << cap_long_stdev_;
    }
}


//
// returns firebreak related metrics; may update cached metrics
// first if they are not recent enough
//
void VolumeStats::getFirebreakMetrics(double* recent_cap_stdev,
                                      double* long_cap_stdev,
                                      double* recent_cap_wma,
                                      double* recent_perf_stdev,
                                      double* long_perf_stdev,
                                      double* recent_perf_wma) {
    // update cached metrics if they are stale
    updateFirebreakMetrics();
    *recent_cap_stdev = cap_recent_stdev_;
    *long_cap_stdev = cap_long_stdev_;
    *recent_cap_wma = cap_recent_wma_;
    *recent_perf_stdev = perf_recent_stdev_;
    *long_perf_stdev = perf_long_stdev_;
    *recent_perf_wma = perf_recent_wma_;
}

//
// calculates stdev for performance and capacity
//
void VolumeStats::updateFirebreakMetrics() {
    std::vector<StatSlot> slots;
    fds_uint64_t now = util::getTimeStampNanos();
    fds_uint64_t elapsed_nanos = 0;
    if (now > long_stdev_update_ts_) {
        elapsed_nanos = now - long_stdev_update_ts_;
    }
    // check if it's time to update recent history stdev
    if (elapsed_nanos < (NANOS_IN_SECOND * coarsegrain_hist_->secondsInSlot())) {
        return;  // no need to update long-term stdev
    }
    recent_stdev_update_ts_ = now;

    // update recent (one day / 1h slots) history stdev
    coarsegrain_hist_->toSlotList(slots, 0, 0, 2*coarsegrain_hist_->secondsInSlot());
    fds_uint32_t min_slots = longterm_hist_->secondsInSlot() / coarsegrain_hist_->secondsInSlot();
    if (min_slots > (coarsegrain_hist_->numberOfSlots() - 1)) {
        min_slots = (coarsegrain_hist_->numberOfSlots() - 1);
    }
    LOGDEBUG << "min_slots" << min_slots << " sec, slots size "
             << slots.size();
    if (slots.size() < min_slots) {
        // we must have some history to start recording stdev
        return;
    }
    updateStdev(slots, 1, &cap_recent_stdev_, &perf_recent_stdev_,
                &cap_recent_wma_, &perf_recent_wma_);
    LOGDEBUG << "Short term history size " << slots.size()
             << " seconds in slot " << coarsegrain_hist_->secondsInSlot()
             << " capacity wma " << cap_recent_wma_
             << " perf wma " << perf_recent_wma_
             << " capacity stdev " << cap_recent_stdev_
             << " perf stdev " << perf_recent_stdev_;

    // check if it's time to update long term history stdev
    if (elapsed_nanos < (NANOS_IN_SECOND * longterm_hist_->secondsInSlot())) {
        return;  // no need to update long-term stdev
    }
    long_stdev_update_ts_ = now;

    // get 1-day history
    longterm_hist_->toSlotList(slots, 0, 0, FdsStatPushPeriodSec + 3*60);
    if (slots.size() < (longterm_hist_->numberOfSlots() - 1)) {
        // to start reporting long term stdev we must have most of history
        return;
    }
    double units = longterm_hist_->secondsInSlot() / coarsegrain_hist_->secondsInSlot();
    updateStdev(slots, 1, &cap_long_stdev_, &perf_long_stdev_, NULL, NULL);
    LOGDEBUG << "Long term history size " << slots.size()
             << " seconds in slot " << coarsegrain_hist_->secondsInSlot()
             << " short slot units " << units
             << " capacity stdev " << cap_long_stdev_
             << " perf stdev " << perf_long_stdev_;
}

void VolumeStats::updateStdev(const std::vector<StatSlot>& slots,
                              double units_in_slot,
                              double* cap_stdev,
                              double* perf_stdev,
                              double* cap_wma,
                              double* perf_wma) {
    std::vector<double> add_bytes;
    std::vector<double> ops_vec;
    double prev_bytes = 0;
    for (std::vector<StatSlot>::const_iterator cit = slots.cbegin();
         cit != slots.cend();
         cit++) {
        double bytes = StatHelper::getTotalPhysicalBytes(*cit);
        LOGDEBUG << "1 - Volume " << std::hex << volid_ << std::dec << " rel ts "
                 << (*cit).getTimestamp() << " bytes " << static_cast<fds_int64_t>(bytes);

        if (cit == slots.cbegin()) {
            prev_bytes = bytes;
            continue;
        }
        double add_bytes_per_unit = (bytes - prev_bytes) / units_in_slot;
        double ops = StatHelper::getTotalPuts(*cit) + StatHelper::getTotalGets(*cit);
        double ops_per_unit = ops / units_in_slot;

        LOGDEBUG << "Volume " << std::hex << volid_ << std::dec << " rel ts "
                 << (*cit).getTimestamp() << " bytes " << static_cast<fds_int64_t>(bytes)
                 << " prev bytes " << static_cast<fds_int64_t>(prev_bytes)
                 << " add_bytes_per_unit " << static_cast<fds_int64_t>(add_bytes_per_unit)
                 << " ops " << ops << " ops/unit " << ops_per_unit;

        add_bytes.push_back(add_bytes_per_unit);
        ops_vec.push_back(ops_per_unit);
        prev_bytes = bytes;
    }
    fds_verify(add_bytes.size() > 0);
    fds_verify(ops_vec.size() > 0);

    // get weighted rolling average of coarse-grain (1-h) histories
    *cap_stdev = util::fds_get_wstdev(add_bytes);
    *perf_stdev = util::fds_get_wstdev(ops_vec);
    if (cap_wma) {
        double wsum = 0;
        *cap_wma = util::fds_get_wma(add_bytes, &wsum);
    }
    if (perf_wma) {
        double wsum = 0;
        *perf_wma = util::fds_get_wma(ops_vec, &wsum);
    }
}


StatStreamAggregator::StatStreamAggregator(char const *const name,
                                           boost::shared_ptr<FdsConfig> fds_config)
        : Module(name),
          process_tm_(new FdsTimer()),
          process_tm_task_(new VolStatsTimerTask(*process_tm_, this)) {
    const FdsRootDir *root = g_fdsprocess->proc_fdsroot();

    // default config
    hist_config.finestat_slotsec_ = FdsStatPeriodSec;
    hist_config.finestat_slots_ = 65;
    hist_config.coarsestat_slotsec_ = 60*60;  // one hour
    hist_config.coarsestat_slots_ = 26;   // 24 hours + few slots
    hist_config.longstat_slotsec_ = 24*60*60;  // one day
    hist_config.longstat_slots_ = 31;  // 30 days + 1

    // align timestamp to the length of finestat slot
    start_time_ = util::getTimeStampNanos();
    fds_uint64_t freq_nanos = hist_config.finestat_slotsec_ * 1000000000;
    start_time_ = start_time_ / freq_nanos;
    start_time_ = start_time_ * freq_nanos;

    root->fds_mkdir(root->dir_sys_repo_stats().c_str());

    fpi::StatStreamRegistrationMsgPtr minLogReg(new fpi::StatStreamRegistrationMsg());
    minLogReg->id = MinLogRegId;
    minLogReg->method = "log-local";
    minLogReg->sample_freq_seconds = 60;
    minLogReg->duration_seconds = 120;

    fpi::StatStreamRegistrationMsgPtr hourLogReg(new fpi::StatStreamRegistrationMsg());
    hourLogReg->id = HourLogRegId;
    hourLogReg->method = "log-local";
    hourLogReg->sample_freq_seconds = 3600;
    hourLogReg->duration_seconds = 7200;

    registerStream(minLogReg);
    registerStream(hourLogReg);

    // if in firebreak test mode, set different slot sizes for long and coarse
    // grain histories
    fds_bool_t test_firebreak = fds_config->get<bool>("fds.dm.testing.test_firebreak", false);
    if (test_firebreak) {
        hist_config.coarsestat_slotsec_ = fds_config->\
                get<int>("fds.dm.testing.coarseslot_sec", hist_config.coarsestat_slotsec_);
        hist_config.coarsestat_slots_ = fds_config->\
                get<int>("fds.dm.testing.coarse_slots", hist_config.coarsestat_slots_);
        hist_config.longstat_slotsec_ = fds_config->\
                get<int>("fds.dm.testing.longslot_sec", hist_config.longstat_slotsec_);
        hist_config.longstat_slots_ = fds_config->\
                get<int>("fds.dm.testing.long_slots", hist_config.longstat_slots_);
    }

    // this is how often we process 1-min stats
    tmperiod_sec_ = (hist_config.finestat_slots_ - 2) * hist_config.finestat_slotsec_;
    if (hist_config.coarsestat_slotsec_ < tmperiod_sec_) {
        tmperiod_sec_ = 2*60;  // hist_config.coarsestat_slotsec_;
    }
    LOGDEBUG << "Finegrain slot length " << hist_config.finestat_slotsec_ << " sec, "
             << "slots " << hist_config.finestat_slots_ << "; coarsegrain slot length "
             << hist_config.coarsestat_slotsec_ << " sec, slots " << hist_config.coarsestat_slots_
             << " Will update coarse grain stats every " << tmperiod_sec_ << " sec";

    // start the timer
    fds_bool_t ret = process_tm_->scheduleRepeated(process_tm_task_,
                                                   std::chrono::seconds(tmperiod_sec_));
    if (!ret) {
        LOGERROR << "Failed to schedule timer for logging volume stats!";
    }
}

StatStreamAggregator::~StatStreamAggregator() {
    process_tm_->destroy();
}

int StatStreamAggregator::mod_init(SysParams const *const param)
{
    Module::mod_init(param);
    return 0;
}

void StatStreamAggregator::mod_startup()
{
}

void StatStreamAggregator::mod_shutdown()
{
}

Error StatStreamAggregator::attachVolume(fds_volid_t volume_id) {
    Error err(ERR_OK);
    LOGDEBUG << "Will monitor stats for vol " << std::hex
             << volume_id << std::dec;

    // create Volume Stats struct and add it to the map
    read_synchronized(volstats_lock_) {
        volstats_map_t::iterator it  = volstats_map_.find(volume_id);
        if (volstats_map_.end() != it) {
            fds_assert(it->second);
            // for now return an error, but prob it's ok if we
            // try to attach volume we already keeping stats for
            // just continue using them?
            return ERR_VOL_DUPLICATE;
        }
    }
    // actually create volume stats struct
    VolumeStats::ptr new_hist(new VolumeStats(volume_id,
                                              start_time_,
                                              hist_config));
    write_synchronized(volstats_lock_) {
        if (volstats_map_.count(volume_id) == 0) {
            volstats_map_[volume_id] = new_hist;
        }
    }

    return err;
}

VolumeStats::ptr StatStreamAggregator::getVolumeStats(fds_volid_t volid) {
    read_synchronized(volstats_lock_) {
        volstats_map_t::iterator it  = volstats_map_.find(volid);
        if (volstats_map_.end() != it) {
            fds_assert(it->second);
            return it->second;
        }
    }
    return VolumeStats::ptr();
}

Error StatStreamAggregator::detachVolume(fds_volid_t volume_id) {
    Error err(ERR_OK);
    LOGDEBUG << "Will stop monitoring stats for vol " << std::hex
             << volume_id << std::dec;

    // remove volstats struct from the map, the destructor should
    // close log file, etc.
    write_synchronized(volstats_lock_) {
        if (volstats_map_.count(volume_id) > 0) {
            volstats_map_.erase(volume_id);
        }
    }
    return err;
}

Error StatStreamAggregator::registerStream(fpi::StatStreamRegistrationMsgPtr registration) {
    if (!registration->sample_freq_seconds || !registration->duration_seconds) {
        return ERR_INVALID_ARG;
    } else if (registration->sample_freq_seconds != 60 &&
            registration->sample_freq_seconds != 3600) {
        return ERR_INVALID_ARG;
    } else if (registration->duration_seconds % registration->sample_freq_seconds) {
        return ERR_INVALID_ARG;
    }

    LOGDEBUG << "Adding streaming registration with id " << registration->id;

    SCOPEDWRITE(lockStatStreamRegsMap);
    statStreamRegistrations_[registration->id] = registration;
    FdsTimerTaskPtr task(new StatStreamTimerTask(timer_, registration, *this));
    statStreamTaskMap_[registration->id] = task;
    timer_.scheduleRepeated(task, std::chrono::seconds(registration->duration_seconds));
    return ERR_OK;
}

Error StatStreamAggregator::deregisterStream(fds_uint32_t reg_id) {
    Error err(ERR_OK);
    LOGDEBUG << "Removing streaming registration with id " << reg_id;

    SCOPEDWRITE(lockStatStreamRegsMap);
    timer_.cancel(statStreamTaskMap_[reg_id]);
    statStreamTaskMap_.erase(reg_id);
    statStreamRegistrations_.erase(reg_id);
    return err;
}

Error
StatStreamAggregator::handleModuleStatStream(const fpi::StatStreamMsgPtr& stream_msg) {
    Error err(ERR_OK);
    fds_uint64_t remote_start_ts = stream_msg->start_timestamp;
    for (fds_uint32_t i = 0; i < (stream_msg->volstats).size(); ++i) {
        fpi::VolStatList & vstats = (stream_msg->volstats)[i];
        LOGDEBUG << "Received stats for volume " << std::hex << vstats.volume_id
                 << std::dec << " timestamp " << remote_start_ts;
        VolumeStats::ptr volstats = getVolumeStats(vstats.volume_id);
        if (!volstats) {
          LOGWARN << "Volume " << std::hex << vstats.volume_id << std::dec
               << " is not attached to the aggregator! Ignoring stats";
          continue;
        }
        err = (volstats->finegrain_hist_)->mergeSlots(vstats, remote_start_ts);
        fds_verify(err.ok());  // if err, prob de-serialize issue
        LOGDEBUG << *(volstats->finegrain_hist_);
    }
    return err;
}

Error
StatStreamAggregator::writeStatsLog(const fpi::volumeDataPoints& volStatData,
                                                            fds_volid_t vol_id,
                                                            bool isMin /* = true */) {
    Error err(ERR_OK);
    char buf[50];

    const FdsRootDir* root = g_fdsprocess->proc_fdsroot();
    const std::string fileName = root->dir_sys_repo_stats() + std::to_string(vol_id) +
            std::string("/") + (isMin ? "stat_min.log" : "stat_hour.log");

    FILE   *pFile = fopen((const char *)fileName.c_str(), "a+");
    if (!pFile) {
        GLOGWARN << "Error opening stat log file '" << fileName << "'";
        return ERR_NOT_FOUND;
    }

    fprintf(pFile, "%s,", volStatData.volume_name.c_str());
    ctime_r((&volStatData.timestamp), buf);
    *(strchr(buf, '\n')) = '\0';  // Get rid of the \n at the end
    fprintf(pFile, "%s,", buf);

    std::vector<DataPointPair>::const_iterator pos;

    fprintf(pFile, "[");
    for (pos = volStatData.meta_list.begin(); pos != volStatData.meta_list.end(); ++pos)
        fprintf(pFile, " %s: %lld,", pos->key.c_str(), static_cast<fds_int64_t>(pos->value));
    fprintf(pFile, "]");

    fprintf(pFile, "\n");
    fclose(pFile);

    /* rsync the per volume stats */
    if (dataMgr->amIPrimary(vol_id)) {
        DmtColumnPtr nodes = dataMgr->omClient->getDMTNodesForVolume(vol_id);
        fds_verify(nodes->getLength() > 0);

        auto selfSvcUuid = MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid();
        for (fds_uint32_t i = 0; i < nodes->getLength(); i++) {
            LOGDEBUG << " rsync node id: " << (nodes->get(i)).uuid_get_val()
                               << "myuuid: " << selfSvcUuid.svc_uuid;
           if (selfSvcUuid != nodes->get(i).toSvcUuid()) {
              volStatSync(nodes->get(i).uuid_get_val(), vol_id);
           }
        }
    }
    return err;
}


Error
StatStreamAggregator::volStatSync(NodeUuid dm_uuid, fds_volid_t vol_id) {
    Error err(ERR_OK);
    const FdsRootDir* root = g_fdsprocess->proc_fdsroot();
    const std::string src_dir = root->dir_sys_repo_stats() + std::to_string(vol_id) +
                                              std::string("/");
    const fpi::SvcUuid & dmSvcUuid = dm_uuid.toSvcUuid();
    auto svcmgr = MODULEPROVIDER()->getSvcMgr();

    fpi::SvcInfo dmSvcInfo;
    if (!svcmgr->getSvcInfo(dmSvcUuid, dmSvcInfo)) {
        LOGERROR << "Failed to sync vol stat: Failed to get IP address for destination DM "
                << std::hex << dmSvcUuid.svc_uuid << std::dec;
        return ERR_NOT_FOUND;
    }

    std::string node_root = svcmgr->getSvcProperty<std::string>(
        svcmgr->mapToSvcUuid(dmSvcUuid, fpi::FDSP_PLATFORM), "fds_root");
    std::string dst_ip = dmSvcInfo.ip;

    const std::string dst_node = node_root + "sys-repo/vol-stats/" +
                                    std::to_string(vol_id) + std::string("/");
    const std::string rsync_cmd = "sshpass -p passwd rsync -r "
            + src_dir + "  root@" + dst_ip + ":" + dst_node + "";

    LOGDEBUG << "system rsync: " <<  rsync_cmd;

    int retcode = std::system((const char *)rsync_cmd.c_str());
    return err;
}


Error
StatStreamAggregator::getStatStreamRegDetails(const fds_volid_t & volId,
        fpi::SvcUuid & amId, fds_uint32_t & regId) {
    SCOPEDREAD(lockStatStreamRegsMap);
    for (auto entry : statStreamRegistrations_) {
        for (auto vol : entry.second->volumes) {
            if (volId == vol) {
                amId = entry.second->dest;
                regId = entry.second->id;
                return ERR_OK;
            }
        }
    }

    return ERR_NOT_FOUND;
}

//
// merge local stats for a given volume
//
void
StatStreamAggregator::handleModuleStatStream(fds_uint64_t start_timestamp,
                                             fds_volid_t volume_id,
                                             const std::vector<StatSlot>& slots) {
    fds_uint64_t remote_start_ts = start_timestamp;

    LOGDEBUG << "Received stats for volume " << std::hex << volume_id
             << std::dec << " timestamp " << remote_start_ts;

    VolumeStats::ptr volstats = getVolumeStats(volume_id);
    if (!volstats) {
        LOGWARN << "Volume " << std::hex << volume_id << std::dec
           << " is not attached to the aggregator! Ignoring stats";
        return;
    }
    (volstats->finegrain_hist_)->mergeSlots(slots, remote_start_ts);
    LOGDEBUG << *(volstats->finegrain_hist_);
}

//
// called on timer to update coarse grain and long term history for
// volume stats
//
void StatStreamAggregator::processStats() {
    std::vector<fds_volid_t> volumes;
    read_synchronized(volstats_lock_) {
        for (auto i : volstats_map_) {
            volumes.push_back(i.first);
        }
    }
    for (auto volId : volumes) {
        VolumeStats::ptr volStat = getVolumeStats(volId);
        volStat->processStats();
    }
}

fds_uint64_t StatHelper::getTotalPuts(const StatSlot& slot) {
    return slot.getCount(STAT_AM_PUT_OBJ);
}

fds_uint64_t StatHelper::getTotalGets(const StatSlot& slot) {
    return slot.getCount(STAT_AM_GET_OBJ);
}

fds_uint64_t StatHelper::getTotalLogicalBytes(const StatSlot& slot) {
    return slot.getTotal(STAT_DM_CUR_TOTAL_BYTES);
}

fds_uint64_t StatHelper::getTotalPhysicalBytes(const StatSlot& slot) {
    fds_uint64_t logical_bytes = slot.getTotal(STAT_DM_CUR_TOTAL_BYTES);
    fds_uint64_t deduped_bytes =  slot.getTotal(STAT_SM_CUR_DEDUP_BYTES);
    if (logical_bytes < deduped_bytes) {
        // we did not measure something properly, for now ignoring
        LOGWARN << "logical bytes " << logical_bytes << " < deduped bytes "
                << deduped_bytes;
        return 0;
    }
    return (logical_bytes - deduped_bytes);
}

// we do not include user-defined metadata
fds_uint64_t StatHelper::getTotalMetadataBytes(const StatSlot& slot) {
    fds_uint64_t tot_objs = slot.getTotal(STAT_DM_CUR_TOTAL_OBJECTS);
    fds_uint64_t tot_blobs = slot.getTotal(STAT_DM_CUR_TOTAL_BLOBS);
    // approx -- asume 20 bytes for blob name
    fds_uint64_t header_bytes = 20 + 8*3 + 4;
    return (tot_blobs * header_bytes + tot_objs * 24);
}

fds_uint64_t StatHelper::getTotalBlobs(const StatSlot& slot) {
    return slot.getTotal(STAT_DM_CUR_TOTAL_BLOBS);
}

fds_uint64_t StatHelper::getTotalObjects(const StatSlot& slot) {
    return slot.getTotal(STAT_DM_CUR_TOTAL_OBJECTS);
}

double StatHelper::getAverageBytesInBlob(const StatSlot& slot) {
    double tot_bytes = slot.getTotal(STAT_DM_CUR_TOTAL_BYTES);
    double tot_blobs = slot.getTotal(STAT_DM_CUR_TOTAL_BLOBS);
    if (tot_blobs == 0) return 0;
    return tot_bytes / tot_blobs;
}

double StatHelper::getAverageObjectsInBlob(const StatSlot& slot) {
    double tot_objects = slot.getTotal(STAT_DM_CUR_TOTAL_OBJECTS);
    double tot_blobs = slot.getTotal(STAT_DM_CUR_TOTAL_BLOBS);
    if (tot_blobs == 0) return 0;
    return tot_objects / tot_blobs;
}

/**
 * We report gets from SSD as actual SSD gets and gets from cache
 */
fds_uint64_t StatHelper::getTotalSsdGets(const StatSlot& slot) {
    fds_uint64_t ssdGets = slot.getCount(STAT_SM_GET_SSD);
    fds_uint64_t amCacheGets = slot.getCount(STAT_AM_GET_CACHED_OBJ);
    return (ssdGets + amCacheGets);
}

fds_bool_t StatHelper::getQueueFull(const StatSlot& slot) {
    return (slot.getMin(STAT_AM_QUEUE_FULL) > 4);  // random constant
}

void VolStatsTimerTask::runTimerTask() {
    aggr_->processStats();
}

void StatStreamTimerTask::runTimerTask() {
    LOGDEBUG << "Streaming stats for registration '" << reg_->id << "'";

    std::vector<fpi::volumeDataPoints> dataPoints;

    std::vector<fds_volid_t> volumes;
    if (reg_->volumes.empty()) {
        for (auto i : statStreamAggr_.volstats_map_) {
            volumes.push_back(i.first);
        }
        cleanupVolumes(volumes);  // cleanup in case volumes were removed
    } else {
        for (auto i : reg_->volumes) {
            volumes.push_back(i);
        }
    }

    bool logLocal = reg_->method == std::string("log-local");
    for (auto volId : volumes) {
        std::vector<StatSlot> slots;
        std::map<fds_uint64_t, std::vector<fpi::DataPointPair> > volDataPointsMap;  // NOLINT
        VolumeStats::ptr volStat = statStreamAggr_.getVolumeStats(volId);
        if (!volStat) {
            GLOGWARN << "Cannot get stat volume history for id '" << volId << "'";
            continue;
        }
        const std::string & volName = dataMgr->volumeName(volId);

        VolumePerfHistory::ptr & hist = 60 == reg_->sample_freq_seconds ?
                volStat->finegrain_hist_ : volStat->coarsegrain_hist_;
        fds_uint64_t last_ts = (vol_last_ts_.count(volId) > 0) ? vol_last_ts_[volId] : 0;
        vol_last_ts_[volId] = hist->toSlotList(slots, last_ts, 0, 2*FdsStatPushPeriodSec);

        for (auto slot : slots) {
            fds_uint64_t timestamp = hist->getTimestamp((slot).getTimestamp());

            timestamp /= 1000 * 1000 * 1000;

            fpi::DataPointPair putsDP;
            putsDP.key = "Puts";
            putsDP.value = StatHelper::getTotalPuts(slot);
            volDataPointsMap[timestamp].push_back(putsDP);

            fpi::DataPointPair getsDP;
            getsDP.key = "Gets";
            getsDP.value = StatHelper::getTotalGets(slot);
            volDataPointsMap[timestamp].push_back(getsDP);

            fpi::DataPointPair qfullDP;
            qfullDP.key = "Queue Full";
            qfullDP.value = StatHelper::getQueueFull(slot);
            volDataPointsMap[timestamp].push_back(qfullDP);

            fpi::DataPointPair ssdGetsDP;
            ssdGetsDP.key = "SSD Gets";
            ssdGetsDP.value = StatHelper::getTotalSsdGets(slot);
            volDataPointsMap[timestamp].push_back(ssdGetsDP);

            fpi::DataPointPair logicalBytesDP;
            logicalBytesDP.key = "Logical Bytes";
            logicalBytesDP.value = StatHelper::getTotalLogicalBytes(slot);
            volDataPointsMap[timestamp].push_back(logicalBytesDP);

            fpi::DataPointPair physicalBytesDP;
            physicalBytesDP.key = "Physical Bytes";
            physicalBytesDP.value = StatHelper::getTotalPhysicalBytes(slot);
            volDataPointsMap[timestamp].push_back(physicalBytesDP);

            fpi::DataPointPair mdBytesDP;
            mdBytesDP.key = "Metadata Bytes";
            mdBytesDP.value = StatHelper::getTotalMetadataBytes(slot);
            volDataPointsMap[timestamp].push_back(mdBytesDP);

            fpi::DataPointPair blobsDP;
            blobsDP.key = "Blobs";
            blobsDP.value = StatHelper::getTotalBlobs(slot);
            volDataPointsMap[timestamp].push_back(blobsDP);

            fpi::DataPointPair objectsDP;
            objectsDP.key = "Objects";
            objectsDP.value = StatHelper::getTotalObjects(slot);
            volDataPointsMap[timestamp].push_back(objectsDP);

            fpi::DataPointPair aveBlobSizeDP;
            aveBlobSizeDP.key = "Ave Blob Size";
            aveBlobSizeDP.value = StatHelper::getAverageBytesInBlob(slot);
            volDataPointsMap[timestamp].push_back(aveBlobSizeDP);

            fpi::DataPointPair aveObjectsDP;
            aveObjectsDP.key = "Ave Objects per Blob";
            aveObjectsDP.value = StatHelper::getAverageObjectsInBlob(slot);
            volDataPointsMap[timestamp].push_back(aveObjectsDP);

            fpi::DataPointPair recentCapStdevDP;
            recentCapStdevDP.key = "Short Term Capacity Sigma";

            fpi::DataPointPair longCapStdevDP;
            longCapStdevDP.key = "Long Term Capacity Sigma";

            fpi::DataPointPair recentCapWmaDP;
            recentCapWmaDP.key = "Short Term Capacity WMA";

            fpi::DataPointPair recentPerfStdevDP;
            recentPerfStdevDP.key = "Short Term Perf Sigma";

            fpi::DataPointPair longPerfStdevDP;
            longPerfStdevDP.key = "Long Term Perf Sigma";

            fpi::DataPointPair recentPerfWma;
            recentPerfWma.key = "Short Term Perf WMA";

            volStat->getFirebreakMetrics(&recentCapStdevDP.value, &longCapStdevDP.value,
                    &recentCapWmaDP.value, &recentPerfStdevDP.value, &longPerfStdevDP.value,
                    &recentPerfWma.value);

            volDataPointsMap[timestamp].push_back(recentCapStdevDP);
            volDataPointsMap[timestamp].push_back(longCapStdevDP);
            volDataPointsMap[timestamp].push_back(recentCapWmaDP);
            volDataPointsMap[timestamp].push_back(recentPerfStdevDP);
            volDataPointsMap[timestamp].push_back(longPerfStdevDP);
            volDataPointsMap[timestamp].push_back(recentPerfWma);
        }

        for (auto dp : volDataPointsMap) {
            fpi::volumeDataPoints volDataPoint;
            volDataPoint.volume_name = volName;
            volDataPoint.timestamp = dp.first;
            volDataPoint.meta_list = dp.second;

            if (logLocal) {
                statStreamAggr_.writeStatsLog(volDataPoint, volId,
                        60 == reg_->sample_freq_seconds);
            }
            dataPoints.push_back(volDataPoint);
        }
    }

    if (dataPoints.size() == 0) {
        // nothing to send, no attached volumes
        return;
    }

    if (!logLocal) {
        fpi::SvcInfo info;
        if (!MODULEPROVIDER()->getSvcMgr()->getSvcInfo(reg_->dest, info)) {
            GLOGERROR << "Failed to get svc info for uuid: '" << std::hex
                    << reg_->dest.svc_uuid << std::dec << "'";
        } else {
            // XXX: hard-coded to bind to java endpoint in AM
            EpInvokeRpc(fpi::StreamingClient, publishMetaStream, info.ip, 8999,
                    reg_->id, dataPoints);
        }
    }
}
}  // namespace fds
