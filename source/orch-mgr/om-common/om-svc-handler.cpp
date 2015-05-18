/*
 * Copyright 2013-2015 by Formations Data Systems, Inc.
 */
#include <string>
#include <vector>
#include <iostream>  // NOLINT
#include <thrift/protocol/TDebugProtocol.h>
#include "om-svc-handler.h"
#include "OmResources.h"
#include "OmDeploy.h"
#include "OmDmtDeploy.h"
#include "OmDataPlacement.h"
#include "OmVolumePlacement.h"
#include "orch-mgr/om-service.h"
#include "util/Log.h"
#include "orchMgr.h"
#include "kvstore/redis.h"
#include "kvstore/configdb.h"
#include <net/SvcMgr.h>

namespace fds {

template<typename T>
static T
get_config(std::string const& option, T const& default_value)
{ 
    return MODULEPROVIDER()->get_fds_config()->get<T>(option, default_value); 
}

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

OmSvcHandler::OmSvcHandler(CommonModuleProviderIf *provider)
: PlatNetSvcHandler(provider)
{
    om_mod = OM_NodeDomainMod::om_local_domain();

    /* svc->om response message */
    REGISTER_FDSP_MSG_HANDLER(fpi::GetVolumeDescriptor, getVolumeDescriptor);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlSvcEvent, SvcEvent);
//    REGISTER_FDSP_MSG_HANDLER(fpi::NodeInfoMsg, om_node_info);
//    REGISTER_FDSP_MSG_HANDLER(fpi::NodeSvcInfo, registerService);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlTokenMigrationAbort, AbortTokenMigration);
}

int OmSvcHandler::mod_init(SysParams const *const param)
{
    this->configDB = gl_orch_mgr->getConfigDB();
    // TODO(bszmyd): Tue 20 Jan 2015 10:24:45 PM PST
    // This isn't probably where this should go, but for now it doesn't make
    // sense anymore for it to go anywhere else. When then dependencies are
    // better determined we should move this.
    // Register event trackers
    init_svc_event_handlers();
    return 0;
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
           OM_NodeAgent::pointer agent = domain->om_all_agent(svc);

           if (agent) {

               /*
                * FS-1424 P. Tinius 03/24/2015
                * No need to spam the log with the log errors, when the
                * real issue
                */
               LOGERROR << std::hex << svc << " saw too many " << std::dec << error
                        << " events [" << events << "]";

               agent->set_node_state(fpi::FDS_Node_Down);
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
OmSvcHandler::om_node_svc_info(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                               boost::shared_ptr<fpi::NodeSvcInfo> &svc)
{
    LOGNORMAL << "Node Service Info received for " << svc.get()->node_auto_name;
}

// om_node_info
// ------------
//
void
OmSvcHandler::om_node_info(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                           boost::shared_ptr<fpi::NodeInfoMsg> &node)
{
    LOGNORMAL << "Node Info received for " << node.get()->node_loc.svc_auto_name;
}

void
OmSvcHandler::getVolumeDescriptor(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                 boost::shared_ptr<fpi::GetVolumeDescriptor> &msg)
{
    LOGNORMAL << " receive getVolumeDescriptor msg";
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    local->om_get_volume_descriptor(hdr, msg->volume_name);
}

void
OmSvcHandler::SvcEvent(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                       boost::shared_ptr<fpi::CtrlSvcEvent> &msg)
{

    // XXX(bszmyd): Thu 22 Jan 2015 12:42:27 PM PST
    // Ignore timeouts from Om. These happen due to one-way messages
    // that cause Svc to timeout the request; e.g. GetVolumeDescriptor and SvcEvent
    if (gl_OmUuid == msg->evt_src_svc_uuid.svc_uuid &&
        ERR_SVC_REQUEST_TIMEOUT == msg->evt_code) {
        return;
    }

    /*
     * FS-1424 P. Tinius 03/24/2015
     * Move this log message to after the filter out check, no need to spam
     * the log for filtered out events.
     */
    LOGDEBUG << " received " << msg->evt_code
             << " from:" << std::hex << msg->evt_src_svc_uuid.svc_uuid << std::dec;
    event_tracker.feed_event(msg->evt_code, msg->evt_src_svc_uuid.svc_uuid);
}

void OmSvcHandler::registerService(boost::shared_ptr<fpi::SvcInfo>& svcInfo)
{
    LOGDEBUG << "Register service request. Svcinfo: " << fds::logString(*svcInfo);
    OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
    Error err = domain->om_register_service(svcInfo);
    if (!err.ok()) {
        LOGERROR << logString(*svcInfo) << err;
        throw fpi::OmRegisterException();
    }
}

/**
 * Allows the pulling of the DLT. Returns DLT_VER_INVALID if there's no committed DLT yet.
 */

void OmSvcHandler::getDLT( ::FDS_ProtocolInterface::CtrlNotifyDLTUpdate& dlt, boost::shared_ptr<int64_t>& nullarg) {
	OM_Module *om = OM_Module::om_singleton();
	DataPlacement *dp = om->om_dataplace_mod();
	std::string data_buffer;
	DLT const *dtp = NULL;
    FDS_ProtocolInterface::FDSP_DLT_Data_Type dlt_val;
	if (!(dp->getCommitedDlt())){
		LOGDEBUG << "Not sending DLT to new node, because no "
                << " committed DLT yet";
        dlt.__set_dlt_version(DLT_VER_INVALID);

	} else {
		LOGDEBUG << "Should have DLT to send";
		dtp = dp->getCommitedDlt();
		dtp->getSerialized(data_buffer);
		dlt.__set_dlt_version(dp->getCommitedDltVersion());
		dlt_val.__set_dlt_data(data_buffer);
		dlt.__set_dlt_data(dlt_val);
	}
}

/**
 * Allows the pulling of the DMT. Returns DMT_VER_INVALID if there's no committed DMT yet.
 */

void OmSvcHandler::getDMT( ::FDS_ProtocolInterface::CtrlNotifyDMTUpdate& dmt, boost::shared_ptr<int64_t>& nullarg) {
	OM_Module *om = OM_Module::om_singleton();
	VolumePlacement* vp = om->om_volplace_mod();
	std::string data_buffer;
    if (vp->hasCommittedDMT()) {
    	DMTPtr dp = vp->getCommittedDMT();
    	LOGDEBUG << "Should have DMT to send";
    	(*dp).getSerialized(data_buffer);

    	::FDS_ProtocolInterface::FDSP_DMT_Data_Type fdt;
    	fdt.__set_dmt_data(data_buffer);

    	dmt.__set_dmt_version(vp->getCommittedDMTVersion());
    	dmt.__set_dmt_data(fdt);
    } else {
        LOGDEBUG << "Not sending DMT to new node, because no "
                << " committed DMT yet";
        dmt.__set_dmt_version(DMT_VER_INVALID);
    }
}

void OmSvcHandler::getSvcInfo(fpi::SvcInfo &_return,
                              boost::shared_ptr< fpi::SvcUuid>& svcUuid)
{
    bool ret = MODULEPROVIDER()->getSvcMgr()->getSvcInfo(*svcUuid, _return);
    if (!ret) {
        LOGWARN << "Failed to lookup svcuuid: " << svcUuid->svc_uuid;
        throw fpi::SvcLookupException();
    }
}

void OmSvcHandler::AbortTokenMigration(boost::shared_ptr<fpi::AsyncHdr> &hdr,
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
