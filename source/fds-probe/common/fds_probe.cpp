/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <fds-probe/fds_probe.h>

namespace fds {

ProbeRequest::ProbeRequest(int stat_cnt, ObjectBuf &buf, ProbeMod &mod)
    : fdsio::Request(true), pr_buf(buf), pr_stat_cnt(stat_cnt), pr_mod(mod)
{
    if (stat_cnt > 0) {
        pr_stats = new fds_uint64_t [stat_cnt];
    } else {
        pr_stats = nullptr;
    }
}

ProbeRequest::~ProbeRequest()
{
    delete [] pr_stats;
}

void
ProbeRequest::pr_request_done()
{
}

void
ProbeRequest::pr_stat_begin(int stat_idx)
{
}

void
ProbeRequest::pr_stat_end(int stat_idx)
{
}

void
ProbeRequest::pr_inj_point(probe_point_e point)
{
    pr_mod.pr_inj_point(point, *this);
}

bool
ProbeRequest::pr_report_stat(probe_stat_rec_t *out, int rec_cnt)
{
    return false;
}

// ----------------------------------------------------------------------------

ProbeMod::ProbeMod(char const *const name,
                   probe_mod_param_t &param,
                   Module            *owner)
    : Module(name), pr_param(param), pr_mod_owner(owner),
      pr_stats_info(nullptr), pr_inj_points(nullptr), pr_inj_actions(nullptr)
{
}

ProbeMod::~ProbeMod()
{
    delete [] pr_stats_info;
    delete [] pr_inj_points;
    delete [] pr_inj_actions;
}

// pr_inj_point
// ------------
//
void
ProbeMod::pr_inj_point(probe_point_e point, ProbeRequest &req)
{
}

// pr_inj_trigger_pct_hit
// ----------------------
//
bool
ProbeMod::pr_inj_trigger_pct_hit(probe_point_e inj, ProbeRequest &req)
{
    return false;
}

// pr_inj_trigger_rand_hit
// -----------------------
//
bool
ProbeMod::pr_inj_trigger_rand_hit(probe_point_e inj, ProbeRequest &req)
{
    return false;
}

// pr_inj_trigger_freq_hit
// -----------------------
//
bool
ProbeMod::pr_inj_trigger_freq_hit(probe_point_e inj, ProbeRequest &req)
{
    return false;
}

// pr_inj_trigger_payload
// ----------------------
//
bool
ProbeMod::pr_inj_trigger_payload(probe_point_e inj, ProbeRequest &req)
{
    return false;
}

// pr_inj_trigger_io_attr
// ----------------------
//
bool
ProbeMod::pr_inj_trigger_io_attr(probe_point_e inj, ProbeRequest &req)
{
    return false;
}

// pr_inj_act_bailout
// ------------------
//
void
ProbeMod::pr_inj_act_bailout(ProbeRequest &req)
{
}

// pr_inj_act_panic
// ----------------
//
void
ProbeMod::pr_inj_act_panic(ProbeRequest &req)
{
}

// pr_inj_act_delay
// ----------------
//
void
ProbeMod::pr_inj_act_delay(ProbeRequest &req)
{
}

// pr_inj_act_corrupt
// ------------------
//
void
ProbeMod::pr_inj_act_corrupt(ProbeRequest &req)
{
}

// ----------------------------------------------------------------------------

ProbeIORequest::ProbeIORequest(int          stat_cnt,
                               ObjectBuf    &buf,
                               ProbeMod     &mod,
                               ObjectID     &oid,
                               fds_uint64_t off,
                               fds_uint64_t vid,
                               fds_uint64_t voff)
    : fdsio::Request(false),
      ProbeRequest(stat_cnt, buf, mod),
      pr_oid(oid), pr_vid(vid), pr_offset(off), pr_voff(voff)
{
}

// pr_alloc_req
// ------------
//
ProbeIORequest *
ProbeMod::pr_alloc_req(ObjectID      &oid,
                       fds_uint64_t  off,
                       fds_uint64_t  vid,
                       fds_uint64_t  voff,
                       ObjectBuf     &buf)
{
    return new ProbeIORequest(0, buf, *this, oid, off, vid, voff);
}

} // namespace fds
