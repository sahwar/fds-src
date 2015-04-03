/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <arpa/inet.h>

#include <fdsp/common_types.h>
#include <fdsp/ConfigurationService.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

#include <NetSession.h>
#include <fds_typedefs.h>
#include <string>
#include <vector>
#include <util/Log.h>
#include <OmResources.h>
#include <convert.h>
#include <orchMgr.h>
#include <util/stringutils.h>
#include <util/timeutils.h>
#include <net/PlatNetSvcHandler.h>

using namespace ::apache::thrift;  //NOLINT
using namespace ::apache::thrift::protocol;  //NOLINT
using namespace ::apache::thrift::transport;  //NOLINT
using namespace ::apache::thrift::server;  //NOLINT
using namespace ::apache::thrift::concurrency;  //NOLINT

using namespace  ::apis;  //NOLINT

namespace fds {
// class OrchMgr;

class ConfigurationServiceHandler : virtual public ConfigurationServiceIf {
    OrchMgr* om;
    kvstore::ConfigDB* configDB;

  public:
    explicit ConfigurationServiceHandler(OrchMgr* om) : om(om) {
        configDB = om->getConfigDB();
    }

    void apiException(std::string message, ErrorCode code = INTERNAL_SERVER_ERROR) {
        LOGERROR << "exception: " << message;
        ApiException e;
        e.message = message;
        e.errorCode    = code;
        throw e;
    }

    void checkDomainStatus() {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        if (!domain->om_local_domain_up()) {
            apiException("local domain not up", SERVICE_NOT_READY);
        }
    }

    // stubs to keep cpp compiler happy - BEGIN
    int64_t createLocalDomain(const std::string& domainName, const std::string& domainSite) { return 0;}
    void listLocalDomains(std::vector<LocalDomain> & _return, const int32_t ignore) {}
    void updateLocalDomainName(const std::string& oldDomainName, const std::string& newDomainName) {}
    void updateLocalDomainSite(const std::string& domainName, const std::string& newSiteName) {}
    void setThrottle(const std::string& domainName, const double throttleLevel) {}
    void setScavenger(const std::string& domainName, const std::string& scavengerAction) {}
    void shutdownLocalDomain(const std::string& domainName) {}
    void deleteLocalDomain(const std::string& domainName) {}
    void activateLocalDomainServices(const std::string& domainName, const bool sm, const bool dm, const bool am) {}
    void listLocalDomainServices(std::vector<FDSP_Node_Info_Type>& _return, const std::string& domainName) {}
    void removeLocalDomainServices(const std::string& domainName, const bool sm, const bool dm, const bool am) {}
    int64_t createTenant(const std::string& identifier) { return 0;}
    void listTenants(std::vector<Tenant> & _return, const int32_t ignore) {}
    int64_t createUser(const std::string& identifier, const std::string& passwordHash, const std::string& secret, const bool isFdsAdmin) { return 0;} //NOLINT
    void assignUserToTenant(const int64_t userId, const int64_t tenantId) {}
    void revokeUserFromTenant(const int64_t userId, const int64_t tenantId) {}
    void allUsers(std::vector<User>& _return, const int64_t ignore) {}
    void listUsersForTenant(std::vector<User> & _return, const int64_t tenantId) {}
    void updateUser(const int64_t userId, const std::string& identifier, const std::string& passwordHash, const std::string& secret, const bool isFdsAdmin) {} //NOLINT
    int64_t configurationVersion(const int64_t ignore) { return 0;}
    void createVolume(const std::string& domainName, const std::string& volumeName, const VolumeSettings& volumeSettings, const int64_t tenantId) {}  //NOLINT
    int64_t getVolumeId(const std::string& volumeName) {return 0;}
    void getVolumeName(std::string& _return, const int64_t volumeId) {}
    void deleteVolume(const std::string& domainName, const std::string& volumeName) {}  //NOLINT
    void statVolume(VolumeDescriptor& _return, const std::string& domainName, const std::string& volumeName) {}  //NOLINT
    void listVolumes(std::vector<VolumeDescriptor> & _return, const std::string& domainName) {}  //NOLINT
    int32_t registerStream(const std::string& url, const std::string& http_method, const std::vector<std::string> & volume_names, const int32_t sample_freq_seconds, const int32_t duration_seconds) { return 0;} //NOLINT
    void getStreamRegistrations(std::vector<apis::StreamingRegistrationMsg> & _return, const int32_t ignore) {} //NOLINT
    void deregisterStream(const int32_t registration_id) {}
    int64_t createSnapshotPolicy(const  fds::apis::SnapshotPolicy& policy) {return 0;} //NOLINT
    void listSnapshotPolicies(std::vector< fds::apis::SnapshotPolicy> & _return, const int64_t unused) {} //NOLINT
    void deleteSnapshotPolicy(const int64_t id) {} //NOLINT
    void attachSnapshotPolicy(const int64_t volumeId, const int64_t policyId) {} //NOLINT
    void listSnapshotPoliciesForVolume(std::vector< fds::apis::SnapshotPolicy> & _return, const int64_t volumeId) {} //NOLINT
    void detachSnapshotPolicy(const int64_t volumeId, const int64_t policyId) {} //NOLINT
    void listVolumesForSnapshotPolicy(std::vector<int64_t> & _return, const int64_t policyId) {} //NOLINT
    void listSnapshots(std::vector< ::FDS_ProtocolInterface::Snapshot> & _return, const int64_t volumeId) {} //NOLINT
    void restoreClone(const int64_t volumeId, const int64_t snapshotId) {} //NOLINT
    int64_t cloneVolume(const int64_t volumeId, const int64_t fdsp_PolicyInfoId, const std::string& clonedVolumeName, const int64_t timelineTime) { return 0;} //NOLINT
    void createSnapshot(const int64_t volumeId, const std::string& snapshotName, const int64_t retentionTime, const int64_t timelineTime) {} //NOLINT

    // stubs to keep cpp compiler happy - END

    /**
    * Create a Local Domain.
    *
    * @param domainName - Name of the new Local Domain. Must be unique within Global Domain.
    * @param domainSite - Name of the new Local Domain's site.
    *
    * @return int64_t - ID of the newly created Local Domain.
    */
    int64_t createLocalDomain(boost::shared_ptr<std::string>& domainName, boost::shared_ptr<std::string>& domainSite) {
        int64_t id = configDB->createLocalDomain(*domainName, *domainSite);
        if (id <= 0) {
            LOGERROR << "Some issue in Local Domain creation: " << *domainName;
            apiException("Error creating Local Domain.");
        } else {
            LOGNOTIFY << "Local Domain creation succeded. " << id << ": " << *domainName;
        }

        return id;
    }

    /**
    * List the currently defined Local Domains.
    *
    * @param _return - Output vecotor of current Local Domains.
    *
    * @return void.
    */
    void listLocalDomains(std::vector<LocalDomain>& _return, boost::shared_ptr<int32_t>& ignore) {
        configDB->listLocalDomains(_return);
    }

    /**
    * Rename the given Local Domain.
    *
    * @param oldDomainName - Current name of the Local Domain.
    * @param newDomainName - New name of the Local Domain.
    *
    * @return void.
    */
    void updateLocalDomainName(boost::shared_ptr<std::string>& oldDomainName, boost::shared_ptr<std::string>& newDomainName) {
        apiException("updateLocalDomainName not implemented.");
    }

    /**
    * Rename the given Local Domain's site.
    *
    * @param domainName - Name of the Local Domain whose site is to be changed.
    * @param newSiteName - New name of the Local Domain's site.
    *
    * @return void.
    */
    void updateLocalDomainSite(boost::shared_ptr<std::string>& domainName, boost::shared_ptr<std::string>& newSiteName) {
        apiException("updateLocalDomainSite not implemented.");
    }

    /**
    * Set the throttle level of the given Local Domain.
    *
    * @param domainName - Name of the Local Domain whose throttle level is to be set.
    * @param throtleLevel - New throttel level for the Local Domain.
    *
    * @return void.
    */
    void setThrottle(boost::shared_ptr<std::string>& domainName, boost::shared_ptr<double>& throttleLevel) {
        try {
            assert((*throttleLevel >= -10) &&
                    (*throttleLevel <= 10));

            /*
             * Currently (3/21/2015) we only have support for one Local Domain. So the specified name is ignored.
             * At some point we should be able to look up the DomainContainer based on Domain ID (or name).
             */
            OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
            local->om_set_throttle_lvl(static_cast<float>(*throttleLevel));
        }
        catch(...) {
            LOGERROR << "Orch Mgr encountered exception while "
                            << "processing set throttle level";
            apiException("setThrottle caused exception.");
        }
    }

    /**
    * Set the given Local Domain's scavenger action.
    *
    * @param domainName - Name of the Local Domain whose scavenger action is to be set.
    * @param scavengerAction - New scavenger action for the Local Domain. One of "enable", "disable", "start", "stop".
    *
    * @return void.
    */
    void setScavenger(boost::shared_ptr<std::string>& domainName, boost::shared_ptr<std::string>& scavengerAction) {
        FDSP_ScavengerCmd cmd;

        if (*scavengerAction == "enable") {
            cmd = FDS_ProtocolInterface::FDSP_SCAVENGER_ENABLE;
            LOGNOTIFY << "Received scavenger enable command";
        } else if (*scavengerAction == "disable") {
            cmd = FDS_ProtocolInterface::FDSP_SCAVENGER_DISABLE;
            LOGNOTIFY << "Received scavenger disable command";
        } else if (*scavengerAction == "start") {
            cmd =FDS_ProtocolInterface::FDSP_SCAVENGER_START;
            LOGNOTIFY << "Received scavenger start command";
        } else if (*scavengerAction == "stop") {
            cmd =FDS_ProtocolInterface::FDSP_SCAVENGER_STOP;
            LOGNOTIFY << "Received scavenger stop command";
        } else {
            apiException("Unrecognized scavenger action: " + *scavengerAction);
        };

        /*
         * Currently (3/21/2015) we only have support for one Local Domain. So the specified name is ignored.
         * At some point we should be able to look up the DomainContainer based on Domain ID (or name).
         */
        // send scavenger start message to all SMs
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        local->om_bcast_scavenger_cmd(cmd);
    }

    /**
    * Shutdown the named Local Domain.
    *
    * @return void.
    */
    void shutdownLocalDomain(boost::shared_ptr<std::string>& domainName) {
        int64_t domainID = configDB->getIdOfLocalDomain(*domainName);

        if (domainID <= 0) {
            LOGERROR << "Local Domain not found: " << domainName;
            apiException("Error shutting down Local Domain " + *domainName + ". Local Domain not found.");
        }

        /*
         * Currently (3/21/2015) we only have support for one Local Domain. So the specified name is ignored.
         * At some point we should be able to look up the DomainContainer based on Domain ID (or name).
         */

        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();

        try {
            LOGNORMAL << "Received shutdown for Local Domain " << domainName;

            domain->om_shutdown_domain();
        }
        catch(...) {
            LOGERROR << "Orch Mgr encountered exception while "
                            << "processing shutdown local domain";
            apiException("Error shutting down Local Domain " + *domainName + " Services. Broadcast shutdown failed.");
        }

    }

    /**
    * Delete the Local Domain.
    *
    * @param domainName - Name of the Local Domain to be deleted.
    *
    * @return void.
    */
    void deleteLocalDomain(boost::shared_ptr<std::string>& domainName) {
        apiException("deleteLocalDomain not implemented.");
    }

    /**
    * Activate all defined Services for all Nodes defined for the named Local Domain.
    *
    * If all Service flags are set to False, it will
    * be interpreted to mean activate all Services currently defined for the Node. If there are
    * no Services currently defined for the node, it will be interpreted to mean activate all
    * Services on the Node (SM, DM, and AM), and define all Services for the Node.
    *
    * @param domainName - Name of the Local Domain whose services are to be activated.
    * @param sm - An fds_bool_t indicating whether the SM Service should be activated (True) or not (False)
    * @param dm - An fds_bool_t indicating whether the DM Service should be activated (True) or not (False)
    * @param am - An fds_bool_t indicating whether the AM Service should be activated (True) or not (False)
    *
    * @return void.
    */
    void activateLocalDomainServices(boost::shared_ptr<std::string>& domainName,
                                     boost::shared_ptr<fds_bool_t>& sm,
                                     boost::shared_ptr<fds_bool_t>& dm,
                                     boost::shared_ptr<fds_bool_t>& am) {
        int64_t domainID = configDB->getIdOfLocalDomain(*domainName);

        if (domainID <= 0) {
            LOGERROR << "Local Domain not found: " << domainName;
            apiException("Error activating Local Domain " + *domainName + " Services. Local Domain not found.");
        }

        /*
         * Currently (3/21/2015) we only have support for one Local Domain. So the specified name is ignored.
         * At some point we should be able to look up the DomainContainer based on Domain ID (or name).
         */

        OM_NodeContainer *localDomain = OM_NodeDomainMod::om_loc_domain_ctrl();

        try {
            LOGNORMAL << "Received activate services for Local Domain " << domainName;
            LOGNORMAL << "SM: " << *sm << "; DM: " << *dm << "; AM: " << *am;

            localDomain->om_cond_bcast_activate_services(*sm, *dm, *am);
        }
        catch(...) {
            LOGERROR << "Orch Mgr encountered exception while "
                            << "processing activate all node services";
            apiException("Error activating Local Domain " + *domainName + " Services. Broadcast activate services failed.");
        }

    }

    /**
    * List all defined Services for all Nodes defined for the named Local Domain.
    *
    * @param _return - Output vector of Services.
    * @param domainName - Name of the Local Domain whose services are to be activated.
    *
    * @return void.
    */
    void listLocalDomainServices(std::vector<FDSP_Node_Info_Type>& _return, boost::shared_ptr<std::string>& domainName) {
        // Currently (3/18/2015) only support for one Local Domain. So the specified name is ignored.
        // The following from FDSP_ConfigPathReqHandler::ListServices;

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();

        local->om_sm_nodes()->
                agent_foreach<std::vector<FDSP_Node_Info_Type> &>(_return, add_to_vector);
        local->om_am_nodes()->
                agent_foreach<std::vector<FDSP_Node_Info_Type> &>(_return, add_to_vector);
        local->om_dm_nodes()->
                agent_foreach<std::vector<FDSP_Node_Info_Type> &>(_return, add_to_vector);
        local->om_pm_nodes()->
                agent_foreach<std::vector<FDSP_Node_Info_Type> &>(_return, add_to_vector);
    }

    /**
    * Remove all defined Services from all Nodes defined for the named Local Domain.
    *
    * If all Service flags are set to False, it will
    * be interpreted to mean remove all Services currently defined for the Node.
    * Removal means that the Service is unregistered from the Domain and shutdown.
    *
    * @param domainName - Name of the Local Domain whose services are to be removed.
    * @param sm - An fds_bool_t indicating whether the SM Service should be removed (True) or not (False)
    * @param dm - An fds_bool_t indicating whether the DM Service should be removed (True) or not (False)
    * @param am - An fds_bool_t indicating whether the AM Service should be removed (True) or not (False)
    *
    * @return void.
    */
    void removeLocalDomainServices(boost::shared_ptr<std::string>& domainName,
                                   boost::shared_ptr<fds_bool_t>& sm,
                                   boost::shared_ptr<fds_bool_t>& dm,
                                   boost::shared_ptr<fds_bool_t>& am) {
        int64_t domainID = configDB->getIdOfLocalDomain(*domainName);

        if (domainID <= 0) {
            LOGERROR << "Local Domain not found: " << domainName;
            apiException("Error removing Local Domain " + *domainName + " Services. Local Domain not found.");
        }

        /*
         * Currently (3/21/2015) we only have support for one Local Domain. So the specified name is ignored.
         * At some point we should be able to look up the DomainContainer based on Domain ID (or name).
         */

        OM_NodeContainer *localDomain = OM_NodeDomainMod::om_loc_domain_ctrl();

        try {
            LOGNORMAL << "Received remove services for Local Domain " << domainName;
            LOGNORMAL << "SM: " << *sm << "; DM: " << *dm << "; AM: " << *am;

            localDomain->om_cond_bcast_remove_services(*sm, *dm, *am);
        }
        catch(...) {
            LOGERROR << "Orch Mgr encountered exception while "
                            << "processing remove all node services";
            apiException("Error removing Local Domain " + *domainName + " Services. Broadcast remove services failed.");
        }

    }

    int64_t createTenant(boost::shared_ptr<std::string>& identifier) {
        return configDB->createTenant(*identifier);
    }

    void listTenants(std::vector<Tenant> & _return, boost::shared_ptr<int32_t>& ignore) {
        configDB->listTenants(_return);
    }

    int64_t createUser(boost::shared_ptr<std::string>& identifier,
                       boost::shared_ptr<std::string>& passwordHash,
                       boost::shared_ptr<std::string>& secret,
                       boost::shared_ptr<bool>& isFdsAdmin) {
        return configDB->createUser(*identifier, *passwordHash, *secret, *isFdsAdmin);
    }

    void assignUserToTenant(boost::shared_ptr<int64_t>& userId,
                            boost::shared_ptr<int64_t>& tenantId) {
        configDB->assignUserToTenant(*userId, *tenantId);
    }

    void revokeUserFromTenant(boost::shared_ptr<int64_t>& userId,
                              boost::shared_ptr<int64_t>& tenantId) {
        configDB->revokeUserFromTenant(*userId, *tenantId);
    }

    void allUsers(std::vector<User> & _return, boost::shared_ptr<int64_t>& ignore) {
        configDB->listUsers(_return);
    }

    void listUsersForTenant(std::vector<User> & _return, boost::shared_ptr<int64_t>& tenantId) {
        configDB->listUsersForTenant(_return, *tenantId);
    }

    void updateUser(boost::shared_ptr<int64_t>& userId,
                    boost::shared_ptr<std::string>& identifier,
                    boost::shared_ptr<std::string>& passwordHash,
                    boost::shared_ptr<std::string>& secret,
                    boost::shared_ptr<bool>& isFdsAdmin) {
        configDB->updateUser(*userId, *identifier, *passwordHash, *secret, *isFdsAdmin);
    }

    int64_t configurationVersion(boost::shared_ptr<int64_t>& ignore) {
        int64_t ver =  configDB->getLastModTimeStamp();
        // LOGDEBUG << "config version : " << ver;
        return ver;
    }

    void createVolume(boost::shared_ptr<std::string>& domainName,
                      boost::shared_ptr<std::string>& volumeName,
                      boost::shared_ptr<VolumeSettings>& volumeSettings,
                      boost::shared_ptr<int64_t>& tenantId) {
        LOGNOTIFY << " domain: " << *domainName
                  << " volume: " << *volumeName
                  << " tenant: " << *tenantId;

        checkDomainStatus();

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        VolumeInfo::pointer vol = volContainer->get_volume(*volumeName);
        Error err = volContainer->getVolumeStatus(*volumeName);
        if (err == ERR_OK) apiException("volume already exists", RESOURCE_ALREADY_EXISTS);

        fpi::FDSP_MsgHdrTypePtr header;
        fpi::FDSP_CreateVolTypePtr request;
        convert::getFDSPCreateVolRequest(header, request,
                                         *domainName, *volumeName, *volumeSettings);
        request->vol_info.tennantId = *tenantId;
        err = volContainer->om_create_vol(header, request, nullptr);
        if (err != ERR_OK) apiException("error creating volume");

        // wait for the volume to be active upto 30 seconds
        int count = 60;

        do {
            usleep(500000);  // 0.5s
            vol = volContainer->get_volume(*volumeName);
            count--;
        } while (count > 0 && vol && !vol->isStateActive());

        if (!vol || !vol->isStateActive()) {
            LOGERROR << "some issue in volume creation";
            apiException("error creating volume");
        }
    }

    int64_t getVolumeId(boost::shared_ptr<std::string>& volumeName) {
        checkDomainStatus();

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        VolumeInfo::pointer  vol = VolumeInfo::vol_cast_ptr(volContainer->rs_get_resource(volumeName->c_str())); //NOLINT
        if (vol) {
            return vol->rs_get_uuid().uuid_get_val();
        } else {
            LOGWARN << "unable to get volume info for vol:" << *volumeName;
            return 0;
        }
    }

    void getVolumeName(std::string& volumeName, boost::shared_ptr<int64_t>& volumeId) {
        checkDomainStatus();

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        VolumeInfo::pointer  vol = VolumeInfo::vol_cast_ptr(volContainer->rs_get_resource(*volumeId)); //NOLINT
        if (vol) {
            volumeName =  vol->vol_get_name();
        } else {
            LOGWARN << "unable to get volume info for vol:" << *volumeId;
        }
    }

    void deleteVolume(boost::shared_ptr<std::string>& domainName,
                      boost::shared_ptr<std::string>& volumeName) {
        checkDomainStatus();

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        Error err = volContainer->getVolumeStatus(*volumeName);

        if (err != ERR_OK) apiException("volume does NOT exist", MISSING_RESOURCE);

        fpi::FDSP_MsgHdrTypePtr header;
        fpi::FDSP_DeleteVolTypePtr request;
        convert::getFDSPDeleteVolRequest(header, request, *domainName, *volumeName);
        err = volContainer->om_delete_vol(header, request);
    }

    void statVolume(VolumeDescriptor& volDescriptor,
                    boost::shared_ptr<std::string>& domainName,
                    boost::shared_ptr<std::string>& volumeName) {
        checkDomainStatus();
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        Error err = volContainer->getVolumeStatus(*volumeName);
        if (err != ERR_OK) apiException("volume NOT found", MISSING_RESOURCE);

        VolumeInfo::pointer  vol = volContainer->get_volume(*volumeName);

        convert::getVolumeDescriptor(volDescriptor, vol);
    }

    void listVolumes(std::vector<VolumeDescriptor> & _return,
                     boost::shared_ptr<std::string>& domainName) {
        checkDomainStatus();

        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();

        LOGDEBUG << "just Active volumes";
        volContainer->vol_up_foreach<std::vector<VolumeDescriptor> &>(_return, [] (std::vector<VolumeDescriptor> &vec, VolumeInfo::pointer vol) { //NOLINT
                LOGDEBUG << (vol->vol_get_properties()->isSnapshot()
                            ? "snapshot" : "volume")
                         << " - " << vol->vol_get_name()
                         << ":" << vol->vol_get_properties()->getStateName();

                if (!vol->vol_get_properties()->isSnapshot()) {
                    VolumeDescriptor volDescriptor;
                    convert::getVolumeDescriptor(volDescriptor, vol);
                    vec.push_back(volDescriptor);
                }
            });
    }

    int32_t registerStream(boost::shared_ptr<std::string>& url,
                           boost::shared_ptr<std::string>& http_method,
                           boost::shared_ptr<std::vector<std::string> >& volume_names,
                           boost::shared_ptr<int32_t>& sample_freq_seconds,
                           boost::shared_ptr<int32_t>& duration_seconds) {
        int32_t regId = configDB->getNewStreamRegistrationId();
        apis::StreamingRegistrationMsg regMsg;
        regMsg.id = regId;
        regMsg.url = *url;
        regMsg.http_method = *http_method;
        regMsg.sample_freq_seconds = *sample_freq_seconds;
        regMsg.duration_seconds = *duration_seconds;

        LOGDEBUG << "Received register stream for frequency "
                 << regMsg.sample_freq_seconds << " seconds"
                 << " duration " << regMsg.duration_seconds << " seconds";

        regMsg.volume_names.reserve(volume_names->size());
        for (uint i = 0; i < volume_names->size(); i++) {
            regMsg.volume_names.push_back(volume_names->at(i));
        }

        if (configDB->addStreamRegistration(regMsg)) {
            OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
            local->om_bcast_stream_register_cmd(regId, false);
            return regId;
        }
        return 0;
    }

    void getStreamRegistrations(std::vector<apis::StreamingRegistrationMsg> & _return,
                                boost::shared_ptr<int32_t>& ignore) { //NOLINT
        configDB->getStreamRegistrations(_return);
    }

    void deregisterStream(boost::shared_ptr<int32_t>& registration_id) {
        configDB->removeStreamRegistration(*registration_id);
    }

    int64_t createSnapshotPolicy(boost::shared_ptr<fds::apis::SnapshotPolicy>& policy) {
        if (om->enableSnapshotSchedule && configDB->createSnapshotPolicy(*policy)) {
            om->snapshotMgr->addPolicy(*policy);
            return policy->id;
        }
        return -1;
    }

    void listSnapshotPolicies(std::vector<fds::apis::SnapshotPolicy> & _return,
                      boost::shared_ptr<int64_t>& unused) {
        configDB->listSnapshotPolicies(_return);
    }

    void deleteSnapshotPolicy(boost::shared_ptr<int64_t>& id) {
        if (om->enableSnapshotSchedule) {
            configDB->deleteSnapshotPolicy(*id);
            om->snapshotMgr->removePolicy(*id);
        }
    }

    void attachSnapshotPolicy(boost::shared_ptr<int64_t>& volumeId,
                              boost::shared_ptr<int64_t>& policyId) {
        configDB->attachSnapshotPolicy(*volumeId, *policyId);
    }

    void listSnapshotPoliciesForVolume(std::vector<fds::apis::SnapshotPolicy> & _return,
                                       boost::shared_ptr<int64_t>& volumeId) {
        configDB->listSnapshotPoliciesForVolume(_return, *volumeId);
    }

    void detachSnapshotPolicy(boost::shared_ptr<int64_t>& volumeId,
                              boost::shared_ptr<int64_t>& policyId) {
        configDB->detachSnapshotPolicy(*volumeId, *policyId);
    }

    void listVolumesForSnapshotPolicy(std::vector<int64_t> & _return,
                              boost::shared_ptr<int64_t>& policyId) {
        configDB->listVolumesForSnapshotPolicy(_return, *policyId);
    }

    void listSnapshots(std::vector<fpi::Snapshot> & _return,
                       boost::shared_ptr<int64_t>& volumeId) {
        configDB->listSnapshots(_return, *volumeId);
    }

    void restoreClone(boost::shared_ptr<int64_t>& volumeId,
                         boost::shared_ptr<int64_t>& snapshotId) {
    }

    int64_t cloneVolume(boost::shared_ptr<int64_t>& volumeId,
                        boost::shared_ptr<int64_t>& volPolicyId,
                        boost::shared_ptr<std::string>& clonedVolumeName,
                        boost::shared_ptr<int64_t>& timelineTime) {
        checkDomainStatus();
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        VolPolicyMgr      *volPolicyMgr = om->om_policy_mgr();
        VolumeInfo::pointer  parentVol, vol;

        vol = volContainer->get_volume(*clonedVolumeName);
        if (vol != NULL) {
            LOGWARN << "volume with same name already exists : " << *clonedVolumeName;
            apiException("volume with same name already exists");
        }

        parentVol = VolumeInfo::vol_cast_ptr(volContainer->rs_get_resource(*volumeId));
        if (parentVol == NULL) {
            LOGWARN << "unable to locate source volume info : " << *volumeId;
            apiException("unable to locate source volume info");
        }

        VolumeDesc desc(*(parentVol->vol_get_properties()));

        desc.volUUID = configDB->getNewVolumeId();
        if (invalid_vol_id == desc.volUUID) {
            LOGWARN << "unable to generate a new vol id";
            apiException("unable to generate a new vol id");
        }
        desc.name = *clonedVolumeName;
        if (*volPolicyId > 0) {
            desc.volPolicyId = *volPolicyId;
        }
        desc.backupVolume = invalid_vol_id;
        desc.fSnapshot = false;
        desc.srcVolumeId = *volumeId;
        desc.timelineTime = *timelineTime;
        desc.createTime = util::getTimeStampMillis();

        if (parentVol->vol_get_properties()->lookupVolumeId == invalid_vol_id) {
            desc.lookupVolumeId = *volumeId;
        } else {
            desc.lookupVolumeId = parentVol->vol_get_properties()->lookupVolumeId;
        }

        desc.qosQueueId = invalid_vol_id;
        volPolicyMgr->fillVolumeDescPolicy(&desc);
        LOGDEBUG << "adding a clone request..";
        volContainer->addVolume(desc);

        // wait for the volume to be active upto 30 seconds
        int count = 60;
        do {
            usleep(500000);  // 0.5s
            vol = volContainer->get_volume(*clonedVolumeName);
            count--;
        } while (count > 0 && vol && !vol->isStateActive());

        if (!vol || !vol->isStateActive()) {
            LOGERROR << "some issue in volume cloning";
            apiException("error creating volume");
        } else {
            // volume created successfully ,
            // now create a base snapshot. [FS-471]
            // we have to do this here because only OM can create a new
            // volume id
            boost::shared_ptr<int64_t> sp_volId(new int64_t(vol->rs_get_uuid().uuid_get_val()));
            boost::shared_ptr<std::string> sp_snapName(new std::string(
                util::strformat("snap0_%s_%d", clonedVolumeName->c_str(),
                                util::getTimeStampNanos())));
            boost::shared_ptr<int64_t> sp_retentionTime(new int64_t(0));
            boost::shared_ptr<int64_t> sp_timelineTime(new int64_t(0));
            createSnapshot(sp_volId, sp_snapName, sp_retentionTime, sp_timelineTime);
        }

        return vol->vol_get_properties()->volUUID;
    }

    void createSnapshot(boost::shared_ptr<int64_t>& volumeId,
                        boost::shared_ptr<std::string>& snapshotName,
                        boost::shared_ptr<int64_t>& retentionTime,
                        boost::shared_ptr<int64_t>& timelineTime) {
                    // create the structure
        fpi::Snapshot snapshot;
        snapshot.snapshotName = util::strlower(*snapshotName);
        snapshot.volumeId = *volumeId;
        snapshot.snapshotId = configDB->getNewVolumeId();
        if (invalid_vol_id == snapshot.snapshotId) {
            LOGWARN << "unable to generate a new snapshot id";
            apiException("unable to generate a new snapshot id");
        }
        snapshot.snapshotPolicyId = 0;
        snapshot.creationTimestamp = util::getTimeStampMillis();
        snapshot.retentionTimeSeconds = *retentionTime;
        snapshot.timelineTime = *timelineTime;

        snapshot.state = fpi::ResourceState::Loading;
        LOGDEBUG << "snapshot request for volumeid:" << snapshot.volumeId
                 << " name:" << snapshot.snapshotName;


        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volContainer = local->om_vol_mgr();
        fds::Error err = volContainer->addSnapshot(snapshot);
        if ( !err.ok() ) {
            LOGWARN << "snapshot add failed : " << err;
            apiException(err.GetErrstr());
        }
        // add this snapshot to the retention manager ...
        if (om->enableSnapshotSchedule) {
            om->snapshotMgr->deleteScheduler->addSnapshot(snapshot);
        }
    }
};

std::thread* runConfigService(OrchMgr* om) {
    int port = 9090;
    LOGNORMAL << "about to start config service @ " << port;
    boost::shared_ptr<ConfigurationServiceHandler> handler(new ConfigurationServiceHandler(om));  //NOLINT
    boost::shared_ptr<TProcessor> processor(new ConfigurationServiceProcessor(handler));  //NOLINT
    boost::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));  //NOLINT
    boost::shared_ptr<TTransportFactory> transportFactory(new TFramedTransportFactory());
    boost::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());  //NOLINT

    // TODO(Andrew): Use a single OM processing thread for now...
    boost::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(1);
    boost::shared_ptr<PosixThreadFactory> threadFactory = boost::shared_ptr<PosixThreadFactory>(
        new PosixThreadFactory());
    threadManager->threadFactory(threadFactory);
    threadManager->start();

    // TNonblockingServer *server = new TNonblockingServer(processor,
    //                                                 protocolFactory,
    //                                                  port,
    //                                                  threadManager);
    TThreadedServer* server = new TThreadedServer(processor,
                                                  serverTransport,
                                                  transportFactory,
                                                  protocolFactory);
    return new std::thread ( [server] {
            LOGNOTIFY << "starting config service";
            server->serve();
            LOGCRITICAL << "stopping ... config service";
        });
}

}  // namespace fds
