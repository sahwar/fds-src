/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <list>
#include <iostream>

#include <OmResources.h>
#include <OmDataPlacement.h>

namespace fds {

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
    TRACEFUNC;
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

fds_uint32_t
ClusterMap::getNumNonfailedMembers(fpi::FDSP_MgrIdType svc_type) const {
    TRACEFUNC;
    fds_uint32_t count = 0;
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            {
                for (const_sm_iterator it = cbegin_sm();
                     it != cend_sm();
                     ++it) {
                    if (( ((*it).second)->node_state() == fpi::FDS_Node_Up ) ||
                        ( ((*it).second)->node_state() == fpi::FDS_Node_Discovered )) {
                        ++count;
                    }
                }
            }
            break;
        case fpi::FDSP_DATA_MGR:
            {
                for (const_dm_iterator it = cbegin_dm();
                     it != cend_dm();
                     ++it) {
                    if (( ((*it).second)->node_state() == fpi::FDS_Node_Up ) ||
                        ( ((*it).second)->node_state() == fpi::FDS_Node_Discovered )) {
                        ++count;
                    }
                }
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return count;
}

fds_uint64_t
ClusterMap::getTotalStorWeight() const {
    TRACEFUNC;
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
    TRACEFUNC;
    LOGNOTIFY << "Will reset added, removed, resyncDms list";
    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            addedSMs.clear();
            removedSMs.clear();
            break;
        case fpi::FDSP_DATA_MGR:
            addedDMs.clear();
            removedDMs.clear();
            resyncDMs.clear();
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

Error
ClusterMap::updateMap(fpi::FDSP_MgrIdType svc_type,
                      const NodeList &addNodes,
                      const NodeList &rmNodes,
					  const NodeList &dmResyncNodes) {
    Error    err(ERR_OK);
    NodeUuid uuid;
    fds_uint32_t removed;

    fds_verify((svc_type == fpi::FDSP_STOR_MGR) ||
               (svc_type == fpi::FDSP_DATA_MGR));

    fds_mutex::scoped_lock l(mapMutex);

    // Remove nodes from the map
    for (NodeList::const_iterator it = rmNodes.cbegin();it != rmNodes.cend(); it++)
    {
        uuid = (*it)->get_uuid();
        LOGNOTIFY << "Updating cluster map with removed svc:"
                  << std::hex << uuid.uuid_get_val() << std::dec;

        if (svc_type == fpi::FDSP_STOR_MGR) {
            removed = curSmMap.erase(uuid);
            // For now, assume it's incorrect to try and remove
            // a node that doesn't exist
//            fds_verify(removed == 1);
            removedSMs.insert(uuid);
        } else {
            removed = curDmMap.erase(uuid);
            // For now, assume it's incorrect to try and remove
            // a node that doesn't exist
//            fds_verify(removed == 1);
            removedDMs.insert(uuid);
        }
    }

    // Add nodes to the map
    for (NodeList::const_iterator it = addNodes.cbegin(); it != addNodes.cend(); it++)
    {
        uuid = (*it)->get_uuid();
        LOGNOTIFY << "Updating cluster map with added svc:"
                  << std::hex << uuid.uuid_get_val() << std::dec;
        if (svc_type == fpi::FDSP_STOR_MGR) {
            curSmMap[uuid] = (*it);
            addedSMs.insert(uuid);
        } else {
            curDmMap[uuid] = (*it);
            addedDMs.insert(uuid);
        }
    }

	for (NodeList::const_iterator it = dmResyncNodes.cbegin();
		 it != dmResyncNodes.cend();
		 it++) {
		uuid = (*it)->get_uuid();
		if (svc_type == fpi::FDSP_STOR_MGR) {
			fds_assert(!"Invalid use case");
		} else {
		    curDmMap[uuid] = (*it);
		    resyncDMs.insert(uuid);
		}
	}

    // Increase the version following the update
    version++;

    return err;
}

Error
ClusterMap::updateMap(fpi::FDSP_MgrIdType svc_type,
                      const NodeList &addNodes,
                      const NodeList &rmNodes) {
    TRACEFUNC;
    NodeList dummy;
    return (updateMap(svc_type, addNodes, rmNodes, dummy));
}

//
// Only add to pending removed nodes. Node should not
// exist in cluster map at this point. This method is used on OM
// bringup from persistent state when not all nodes came up and
// we want to remove those nodes from persisted DLT
//
void
ClusterMap::addSvcPendingRemoval(fpi::FDSP_MgrIdType svc_type,
                                const NodeUuid& rm_uuid)
{
    TRACEFUNC;
    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            fds_verify(curSmMap.count(rm_uuid) == 0);
            if (removedSMs.count(rm_uuid) == 0) {
                removedSMs.insert(rm_uuid);
            } else {
                LOGNOTIFY << "Uuid:" << std::hex << rm_uuid.uuid_get_val() << std::hex
                          << " is already in removedSMs list";
            }
            break;
        case fpi::FDSP_DATA_MGR:
            fds_verify(curDmMap.count(rm_uuid) == 0);
            if (removedDMs.count(rm_uuid) == 0) {
                removedDMs.insert(rm_uuid);
            } else {
                LOGNOTIFY << "Uuid:" << std::hex << rm_uuid.uuid_get_val() << std::hex
                          << " is already in removedDMs list";
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

// remove service from pending added svc map
void
ClusterMap::rmAddedSvcFromAllMaps(fpi::FDSP_MgrIdType svc_type,
                                  const NodeUuid& svc_uuid) {
    TRACEFUNC;
    LOGNOTIFY << "Attempt to remove svc uuid:"
              << std::hex << svc_uuid.uuid_get_val() << std::dec
              << " from added svcs list and current svcs map";
    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            if ( addedSMs.find(svc_uuid) != addedSMs.end() &&
                 curSmMap.find(svc_uuid) != curSmMap.end() )
            {

                LOGNOTIFY << "Removed svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " from added svcs and current svcs map";
                addedSMs.erase(svc_uuid);
                curSmMap.erase(svc_uuid);
            } else {
                LOGWARN << "Svc:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " not present in either addedSM list or curSmMap!";
            }
            break;
        case fpi::FDSP_DATA_MGR:

            if ( (addedSMs.find(svc_uuid) != addedSMs.end() ||
                  resyncDMs.find(svc_uuid) != resyncDMs.end()) &&
                 curDmMap.find(svc_uuid) != curDmMap.end() )
            {
                LOGNOTIFY << "Removed svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " from added svcs, resyncDMs list and curDmMap";

                addedSMs.erase(svc_uuid);
                resyncDMs.erase(svc_uuid);
                curDmMap.erase(svc_uuid);
            } else {
                LOGWARN << "Svc:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " not present in either addedDM,resyncDM list or curDmMap!";
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

// remove service from pending remove svc map
void
ClusterMap::rmSvcPendingRemoval(fpi::FDSP_MgrIdType svc_type,
                                    const NodeUuid& svc_uuid) {
    TRACEFUNC;
    LOGNOTIFY << "Attempt to remove svc uuid:"
              << std::hex << svc_uuid.uuid_get_val() << std::dec
              << " from removed svcs list and current svcs map (if present)";

    fds_mutex::scoped_lock l(mapMutex);

    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            if ( removedSMs.find(svc_uuid) != removedSMs.end() )
            {
                LOGNOTIFY << "Removing svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " from removed svcs list";
                removedSMs.erase(svc_uuid);
            }
            break;
        case fpi::FDSP_DATA_MGR:
            if ( removedDMs.find(svc_uuid) != removedDMs.end() )
            {
                LOGNOTIFY << "Removing svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " from removed svcs list";
                removedDMs.erase(svc_uuid);
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

// reset service from pending added svc map
// but do not remove from cluster map
void
ClusterMap::rmSvcPendingAdd(fpi::FDSP_MgrIdType svc_type,
                            const NodeUuid& svc_uuid)
{
    TRACEFUNC;
    LOGNOTIFY << "Attempt to remove svc uuid:"
              << std::hex << svc_uuid.uuid_get_val() << std::dec
              << " from added svcs list";

    fds_mutex::scoped_lock l(mapMutex);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            if (addedSMs.count(svc_uuid) != 0) {
                addedSMs.erase(svc_uuid);
            } else {
                LOGNOTIFY << "Svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " not present in added list";
            }
            break;
        case fpi::FDSP_DATA_MGR:
            if (addedDMs.count(svc_uuid) != 0) {
                addedDMs.erase(svc_uuid);
            } else {
                LOGNOTIFY << "Svc uuid:"
                          << std::hex << svc_uuid.uuid_get_val() << std::dec
                          << " not present in added list";
            }

            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
}

std::unordered_set<NodeUuid, UuidHash>
ClusterMap::getAddedServices(fpi::FDSP_MgrIdType svc_type) const {
    TRACEFUNC;
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
    TRACEFUNC;
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

std::unordered_set<NodeUuid, UuidHash>
ClusterMap::getDmResyncServices() const {
    TRACEFUNC;
    return resyncDMs;
}

NodeUuidSet
ClusterMap::getNonfailedServices(fpi::FDSP_MgrIdType svc_type) const {
    NodeUuidSet retSet;
    bool fIgnoreFailedSvcs = MODULEPROVIDER()->get_fds_config()->get<bool>
                             ("fds.feature_toggle.om.ignore_failed_svcs", true);
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            {
                for (const_sm_iterator it = cbegin_sm();
                     it != cend_sm();
                     ++it) {
                    if (( ((*it).second)->node_state() == fpi::FDS_Node_Up ) ||
                        ( ((*it).second)->node_state() == fpi::FDS_Node_Discovered )) {
                        retSet.insert(((*it).second)->get_uuid());

                    } else if (fIgnoreFailedSvcs) {
                        LOGNOTIFY << "Ignoring failed SM svc:" << (*it).second->get_uuid();
                        retSet.insert(((*it).second)->get_uuid());
                    }
                }
            }
            break;
        case fpi::FDSP_DATA_MGR:
            {
                for (const_dm_iterator it = cbegin_dm();
                     it != cend_dm();
                     ++it) {
                    if (( ((*it).second)->node_state() == fpi::FDS_Node_Up ) ||
                        ( ((*it).second)->node_state() == fpi::FDS_Node_Discovered )) {
                        retSet.insert(((*it).second)->get_uuid());

                    } else if (fIgnoreFailedSvcs) {
                        LOGNOTIFY << "Ignoring failed DM svc:" << (*it).second->get_uuid();
                        retSet.insert(((*it).second)->get_uuid());
                    }
                }
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return retSet;
}

NodeUuidSet
ClusterMap::getFailedServices(fpi::FDSP_MgrIdType svc_type) const {
    NodeUuidSet retSet;
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            {
                for (const_sm_iterator it = cbegin_sm();
                     it != cend_sm();
                     ++it) {
                    if (( ((*it).second)->node_state() != fpi::FDS_Node_Up ) &&
                        ( ((*it).second)->node_state() != fpi::FDS_Node_Discovered )) {
                        retSet.insert(((*it).second)->get_uuid());
                    }
                }
            }
            break;
        case fpi::FDSP_DATA_MGR:
            {
                for (const_dm_iterator it = cbegin_dm();
                     it != cend_dm();
                     ++it) {
                    if (( ((*it).second)->node_state() != fpi::FDS_Node_Up ) &&
                        ( ((*it).second)->node_state() != fpi::FDS_Node_Discovered )) {
                        retSet.insert(((*it).second)->get_uuid());
                    }
                }
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return retSet;
}

NodeUuidSet
ClusterMap::getServiceUuids(fpi::FDSP_MgrIdType svc_type) const {
    NodeUuidSet retSet;
    switch (svc_type) {
        case fpi::FDSP_STOR_MGR:
            {
                for (const_sm_iterator it = cbegin_sm();
                     it != cend_sm();
                     ++it) {
                    retSet.insert(((*it).second)->get_uuid());
                }
            }
            break;
        case fpi::FDSP_DATA_MGR:
            {
                for (const_dm_iterator it = cbegin_dm();
                     it != cend_dm();
                     ++it) {
                    retSet.insert(((*it).second)->get_uuid());
                }
            }
            break;
        default:
            fds_panic("Unknown MgrIdType %u", svc_type);
    }
    return retSet;
}

fds_bool_t
ClusterMap::serviceAddExists(fpi::FDSP_MgrIdType svc_type, const NodeUuid& svc_uuid)
{
	switch (svc_type) {
		case (fpi::FDSP_STOR_MGR):
			if (addedSMs.count(svc_uuid) > 0) {
				return true;
			}
			break;
		case (fpi::FDSP_DATA_MGR):
			if (addedDMs.count(svc_uuid) > 0) {
				return true;
			}
			break;
        default:
        	// Clustermap only cares about DM or SM
        	break;
	}
	return false;
}
}  // namespace fds
