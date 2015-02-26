/*
 * Copyright 2013-2015 by Formations Data Systems, Inc.
 */
#include <string>
#include <vector>
#include <iostream>  // NOLINT
#include <thrift/protocol/TDebugProtocol.h>
#include <net/RpcFunc.h>
#include <om-svc-handler.h>
#include <OmResources.h>
#include <OmDeploy.h>
#include <OmDmtDeploy.h>
#include <OmDataPlacement.h>
#include <OmVolumePlacement.h>
#include <orch-mgr/om-service.h>
#include <util/Log.h>
#include <orchMgr.h>

namespace fds {

template<typename T>
static T
get_config(std::string const& option, T const& default_value)
{ return gModuleProvider->get_fds_config()->get<T>(option, default_value); }

template <typename T, typename Cb>
static std::unique_ptr<TrackerBase<NodeUuid>>
create_tracker(Cb&& cb, std::string event, fds_uint32_t d_w = 0, fds_uint32_t d_t = 0) {
    static std::string const svc_event_prefix("fds.om.svc_event_threshold.");

    size_t window = get_config(svc_event_prefix + event + ".window", d_w);
    size_t threshold = get_config(svc_event_prefix + event + ".threshold", d_t);

    return std::unique_ptr<TrackerBase<NodeUuid>>
        (new TrackerMap<Cb, NodeUuid, T>(std::forward<Cb>(cb), window, threshold));
}

OmSvcHandler::~OmSvcHandler() {}

OmSvcHandler::OmSvcHandler()
{
    om_mod = OM_NodeDomainMod::om_local_domain();
    // REGISTER_FDSP_MSG_HANDLER(fpi::NodeInfoMsg, om_node_info);
    // REGISTER_FDSP_MSG_HANDLER(fpi::NodeSvcInfo, om_node_svc_info);


    /* svc->om response message */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlTestBucket, TestBucket);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlCreateBucket, CreateBucket);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlDeleteBucket, DeleteBucket);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlModifyBucket, ModifyBucket);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlSvcEvent, SvcEvent);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlTokenMigrationAbort, AbortTokenMigration);

    // TODO(bszmyd): Tue 20 Jan 2015 10:24:45 PM PST
    // This isn't probably where this should go, but for now it doesn't make
    // sense anymore for it to go anywhere else. When then dependencies are
    // better determined we should move this.
    // Register event trackers
    init_svc_event_handlers();
}

// Right now all handlers are using the same callable which will down the
// service that is responsible. This can be changed easily.
void OmSvcHandler::init_svc_event_handlers() {

    // callable for EventTracker. Changed from an anonymous function so I could
    // bind different errors to the same logic and retain the error for
    // om_service_down call.
    struct cb {
       void operator()(NodeUuid svc, size_t events) const {
           auto domain = OM_NodeDomainMod::om_local_domain();
           LOGERROR << std::hex << svc << " saw too many " << error << " events [" << events << "]";
           OM_NodeAgent::pointer agent = domain->om_all_agent(svc);

           if (agent) {
               agent->set_node_state(FDS_Node_Down);
               domain->om_service_down(error, svc, agent->om_agent_type());
           } else {
               LOGERROR << "unknown service: " << svc;
           }
       }
       Error               error;
    };

    // Timeout handler (2 within 15 minutes will trigger)
    event_tracker.register_event(ERR_SVC_REQUEST_TIMEOUT,
                                 create_tracker<std::chrono::minutes>(cb{ERR_SVC_REQUEST_TIMEOUT},
                                                                      "timeout", 15, 2));

    // DiskWrite handler (1 within 24 hours will trigger)
    event_tracker.register_event(ERR_DISK_WRITE_FAILED,
                                 create_tracker<std::chrono::hours>(cb{ERR_DISK_WRITE_FAILED},
                                                                    "disk.write_fail", 24, 1));

    // DiskRead handler (1 within 24 hours will trigger)
    event_tracker.register_event(ERR_DISK_READ_FAILED,
                                 create_tracker<std::chrono::hours>(cb{ERR_DISK_READ_FAILED},
                                                                    "disk.read_fail", 24, 1));
}

// om_svc_state_chg
// ----------------
//
void
OmSvcHandler::om_node_svc_info(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                               boost::shared_ptr<fpi::NodeSvcInfo> &msg)
{
    LOGNORMAL << "Service up";
}

// om_node_info
// ------------
//
void
OmSvcHandler::om_node_info(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                           boost::shared_ptr<fpi::NodeInfoMsg> &node)
{
    LOGNORMAL << "Node up";
}

void
OmSvcHandler::TestBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlTestBucket> &msg)
{
    LOGNORMAL << " receive test bucket msg";
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    local->om_test_bucket(hdr, &msg->tbmsg);
}

void
OmSvcHandler::    CreateBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlCreateBucket> &msg)
{
    const FdspCrtVolPtr crt_buck_req(new fpi::FDSP_CreateVolType());
    * crt_buck_req = msg->cv;
    LOGNOTIFY << "Received create bucket " << crt_buck_req->vol_name
              << " from  node uuid: "
              << std::hex << hdr->msg_src_uuid.svc_uuid << std::dec;

    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        if (domain->om_local_domain_up()) {
            local->om_create_vol(nullptr, crt_buck_req, hdr);
        } else {
            LOGWARN << "OM Local Domain is not up yet, rejecting bucket "
                    << " create; try later";
        }
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing create bucket";
    }
}

void
OmSvcHandler::    DeleteBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlDeleteBucket> &msg)
{
    LOGNORMAL << " receive delete bucket from " << hdr->msg_src_uuid.svc_uuid;
    const FdspDelVolPtr del_buck_req(new fpi::FDSP_DeleteVolType());
    *del_buck_req = msg->dv;
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    local->om_delete_vol(nullptr, del_buck_req);
}

void
OmSvcHandler::    ModifyBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlModifyBucket> &msg)
{
    LOGNORMAL << " receive delete bucket from " << hdr->msg_src_uuid.svc_uuid;
    const FdspModVolPtr mod_buck_req(new fpi::FDSP_ModifyVolType());
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    local->om_modify_vol(mod_buck_req);
}

void
OmSvcHandler::    SvcEvent(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlSvcEvent> &msg)
{
    LOGDEBUG << " received " << msg->evt_code
             << " from:" << std::hex << msg->evt_src_svc_uuid.svc_uuid << std::dec;

    // XXX(bszmyd): Thu 22 Jan 2015 12:42:27 PM PST
    // Ignore timeouts from Om. These happen due to one-way messages
    // that cause Svc to timeout the request; e.g. TestBucket and SvcEvent
    if (gl_OmUuid == msg->evt_src_svc_uuid.svc_uuid &&
        ERR_SVC_REQUEST_TIMEOUT == msg->evt_code) {
        return;
    }
    event_tracker.feed_event(msg->evt_code, msg->evt_src_svc_uuid.svc_uuid);
}

void
OmSvcHandler::AbortTokenMigration(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                  boost::shared_ptr<fpi::CtrlTokenMigrationAbort> &msg)
{
    LOGNORMAL << "Received abort token migration msg from "
              << std::hex << hdr->msg_src_uuid.svc_uuid << std::dec;
    OM_Module *om = OM_Module::om_singleton();
    OM_DLTMod *dltMod = om->om_dlt_mod();

    // tell DLT state machine about abort (error state)
    dltMod->dlt_deploy_event(DltErrorFoundEvt(NodeUuid(hdr->msg_src_uuid),
                                              Error(ERR_SM_TOK_MIGRATION_ABORTED)));
}


}  //  namespace fds
