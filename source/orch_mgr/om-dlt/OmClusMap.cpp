/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <list>
#include <iostream>

#include <OmResources.h>
#include <OmDataPlacement.h>

namespace fds {

ClusterMap gl_OMClusMapMod;

ClusterMap::ClusterMap()
        : Module("OM Cluster Map"),
          version(0),
          mapMutex("cluster map mutex") {
}

ClusterMap::~ClusterMap() {
}

ClusterMap::const_sm_iterator
ClusterMap::cbegin_sm() const {
    return curSmMap.cbegin();
}

ClusterMap::const_sm_iterator
ClusterMap::cend_sm() const {
    return curSmMap.cend();
}

ClusterMap::const_dm_iterator
ClusterMap::cbegin_dm() const {
    return curDmMap.cbegin();
}

ClusterMap::const_dm_iterator
ClusterMap::cend_dm() const {
    return curDmMap.cend();
}

int
ClusterMap::mod_init(SysParams const *const param) {
    Module::mod_init(param);
    LOGNOTIFY << "ClusterMap init is called ";
    return 0;
}

void
ClusterMap::mod_startup() {
}

void
ClusterMap::mod_shutdown() {
}

fds_uint32_t
ClusterMap::getNumMembers(fpi::FDSP_MgrIdType svc_type) const {
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            return curSmMap.size();
        case fpi::FDSP_DATA_MGR:
            return curDmMap.size();
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return 0;
}

fds_uint64_t
ClusterMap::getTotalStorWeight() const {
    fds_uint64_t total_weight = 0;
    for (const_sm_iterator it = cbegin_sm();
         it != cend_sm();
         ++it) {
        total_weight += ((*it).second)->node_stor_weight();
    }
    return total_weight;
}

void
ClusterMap::resetPendServices(fpi::FDSP_MgrIdType svc_type) {
    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            addedSMs.clear();
            removedSMs.clear();
            break;
        case fpi::FDSP_DATA_MGR:
            addedDMs.clear();
            removedDMs.clear();
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

Error
ClusterMap::updateMap(fpi::FDSP_MgrIdType svc_type,
                      const NodeList &addNodes,
                      const NodeList &rmNodes) {
    Error    err(ERR_OK);
    NodeUuid uuid;
    fds_uint32_t removed;

    fds_verify((svc_type == fpi::FDSP_STOR_MGR) ||
               (svc_type == fpi::FDSP_DATA_MGR));

    fds_mutex::scoped_lock l(mapMutex);

    // Remove nodes from the map
    for (NodeList::const_iterator it = rmNodes.cbegin();
         it != rmNodes.cend();
         it++) {
        uuid = (*it)->get_uuid();
        if (svc_type == fpi::FDSP_STOR_MGR) {
            removed = curSmMap.erase(uuid);
            // For now, assume it's incorrect to try and remove
            // a node that doesn't exist
            fds_verify(removed == 1);
            removedSMs.insert(uuid);
        } else {
            removed = curDmMap.erase(uuid);
            // For now, assume it's incorrect to try and remove
            // a node that doesn't exist
            fds_verify(removed == 1);
            removedDMs.insert(uuid);
        }
    }

    // Add nodes to the map
    for (NodeList::const_iterator it = addNodes.cbegin();
         it != addNodes.cend();
         it++) {
        uuid = (*it)->get_uuid();
        if (svc_type == fpi::FDSP_STOR_MGR) {
            // For now, assume it's incorrect to add a node
            // that already exists
            fds_verify(curSmMap.count(uuid) == 0);
            curSmMap[uuid] = (*it);
            addedSMs.insert(uuid);
        } else {
            // For now, assume it's incorrect to add a node
            // that already exists
            fds_verify(curDmMap.count(uuid) == 0);
            curDmMap[uuid] = (*it);
            addedDMs.insert(uuid);
        }
    }
    // Increase the version following the update
    version++;
    return err;
}

//
// Only add to pending removed nodes. Node should not
// exist in cluster map at this point. This method is used on OM
// bringup from persistent state when not all nodes came up and
// we want to remove those nodes from persisted DLT
//
void
ClusterMap::addPendingRmService(fpi::FDSP_MgrIdType svc_type,
                                const NodeUuid& rm_uuid)
{
    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            fds_verify(curSmMap.count(rm_uuid) == 0);
            removedSMs.insert(rm_uuid);
            break;
        case fpi::FDSP_DATA_MGR:
            fds_verify(curDmMap.count(rm_uuid) == 0);
            removedDMs.insert(rm_uuid);
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

std::unordered_set<NodeUuid, UuidHash>
ClusterMap::getAddedServices(fpi::FDSP_MgrIdType svc_type) const {
    /*
     * TODO: We should ensure that we're not
     * in the process of updating the cluster map
     * as this list may be in the process of being
     * modifed.
     */
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            return addedSMs;
        case fpi::FDSP_DATA_MGR:
            return addedDMs;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return std::unordered_set<NodeUuid, UuidHash>();
}

std::unordered_set<NodeUuid, UuidHash>
ClusterMap::getRemovedServices(fpi::FDSP_MgrIdType svc_type) const {
    /*
     * TODO: We should ensure that we're not
     * in the process of updating the cluster map
     * as this list may be in the process of being
     * modifed.
     */
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            return removedSMs;
        case fpi::FDSP_DATA_MGR:
            return removedDMs;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return std::unordered_set<NodeUuid, UuidHash>();
}

}  // namespace fds
