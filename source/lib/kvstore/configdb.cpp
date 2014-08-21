/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <map>
#include <string>
#include <vector>

#include <kvstore/configdb.h>
#include <util/Log.h>
#include <stdlib.h>
#include <fdsp_utils.h>
#include <util/timeutils.h>
#include <algorithm>
namespace fds { namespace kvstore {
using redis::Reply;
using redis::RedisException;

ConfigException::ConfigException(const std::string& msg) : msg(msg) {
}

const char* ConfigException::what() const noexcept{
    return msg.c_str();
}

ConfigDB::ConfigDB(const std::string& host,
                   uint port,
                   uint poolsize) : KVStore(host, port, poolsize) {
    LOGNORMAL << "instantiating configdb";
}

ConfigDB::~ConfigDB() {
    LOGNORMAL << "destroying configdb";
}

fds_uint64_t ConfigDB::getLastModTimeStamp() {
    try {
        return r.get("config.lastmod").getLong();
    } catch(const RedisException& e) {
        LOGERROR << e.what();
    }

    return 0;
}

fds_uint64_t ConfigDB::getConfigVersion() {
    try {
        return r.get("config.version").getLong();
    } catch(const RedisException& e) {
        LOGERROR << e.what();
    }

    return 0;
}

void ConfigDB::setModified() {
    try {
        r.sendCommand("set config.lastmod %ld", fds::util::getTimeStampMillis());
        r.incr("config.version");
    } catch(const RedisException& e) {
        LOGERROR << e.what();
    }
}

// domains
std::string ConfigDB::getGlobalDomain() {
    try {
        return r.get("global.domain").getString();
    } catch(const RedisException& e) {
        LOGERROR << e.what();
        return "";
    }
}

bool ConfigDB::setGlobalDomain(ConstString globalDomain) {
    TRACKMOD();
    try {
        return r.set("global.domain", globalDomain).isOk();
    } catch(const RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::addLocalDomain(ConstString name, int localDomain, int globalDomain) {
    TRACKMOD();
    try {
        Reply reply = r.sendCommand("sadd %d:domains %d", globalDomain, localDomain);
        if (!reply.wasModified()) {
            LOGWARN << "domain id: "<< localDomain <<" already exists for global :" <<globalDomain;
        }
        return r.sendCommand("hmset domain:%d id %d name %s", localDomain, localDomain, name.c_str()).isOk(); //NOLINT
    } catch(const RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::getLocalDomains(std::map<int, std::string>& mapDomains, int globalDomain) {
    try {
        Reply reply = r.sendCommand("smembers %d:domains", globalDomain);
        std::vector<long long> vec; //NOLINT
        reply.toVector(vec);

        for (uint i = 0; i < vec.size(); i++) {
            reply = r.sendCommand("hget domain:%d name", static_cast<int>(vec[i]));
            mapDomains[static_cast<int>(vec[i])] = reply.getString();
        }
        return true;
    } catch(const RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

// volumes
bool ConfigDB::addVolume(const VolumeDesc& vol) {
    TRACKMOD();
    // add the volume to the volume list for the domain
    try {
        Reply reply = r.sendCommand("sadd %d:volumes %ld", vol.localDomainId, vol.volUUID);
        if (!reply.wasModified()) {
            LOGWARN << "volume [" << vol.volUUID
                    << "] already exists for domain ["
                    << vol.localDomainId << "]";
        }

        // add the volume data
        reply = r.sendCommand("hmset vol:%ld uuid %ld"
                              " name %s"
                              " tennant.id %d"
                              " local.domain.id %d"
                              " global.domain.id %d"
                              " type %d"
                              " capacity %.3f"
                              " quota.max %.2f"
                              " replica.count %d"
                              " objsize.max %d"
                              " write.quorum %d"
                              " read.quorum %d"
                              " conistency.protocol %d"
                              " volume.policy.id %d"
                              " archive.policy.id %d"
                              " placement.policy.id %d"
                              " app.workload %d"
                              " media.policy %d"
                              " backup.vol.id %ld"
                              " iops.min %.3f"
                              " iops.max %.3f"
                              " relative.priority %d",
                              vol.volUUID, vol.volUUID,
                              vol.name.c_str(),
                              vol.tennantId,
                              vol.localDomainId,
                              vol.globDomainId,
                              vol.volType,
                              vol.capacity,
                              vol.maxQuota,
                              vol.replicaCnt,
                              vol.maxObjSizeInBytes,
                              vol.writeQuorum,
                              vol.readQuorum,
                              vol.consisProtocol,
                              vol.volPolicyId,
                              vol.archivePolicyId,
                              vol.placementPolicy,
                              vol.appWorkload,
                              vol.mediaPolicy,
                              vol.backupVolume,
                              vol.iops_min,
                              vol.iops_max,
                              vol.relativePrio);
        if (reply.isOk()) return true;
        LOGWARN << "msg: " << reply.getString();
    } catch(RedisException& e) {
        LOGERROR << e.what();
        TRACKMOD();
    }
    return false;
}

bool ConfigDB::updateVolume(const VolumeDesc& volumeDesc) {
    TRACKMOD();
    try {
        return addVolume(volumeDesc);
    } catch(const RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::deleteVolume(fds_volid_t volumeId, int localDomainId) {
    TRACKMOD();
    try {
        Reply reply = r.sendCommand("srem %d:volumes %ld", localDomainId, volumeId);
        if (!reply.wasModified()) {
            LOGWARN << "volume [" << volumeId
                    << "] does NOT exist for domain ["
                    << localDomainId << "]";
        }

        // del the volume data
        reply = r.sendCommand("del vol:%ld", volumeId);
        return reply.isOk();
    } catch(RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::volumeExists(fds_volid_t volumeId) {
    try {
        Reply reply = r.sendCommand("exists vol:%ld", volumeId);
        return reply.getLong()== 1;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}
bool ConfigDB::volumeExists(ConstString volumeName, int localDomain){ return false; }
bool ConfigDB::getVolumeIds(std::vector<fds_volid_t>& volIds, int localDomain) {
    std::vector<long long> volumeIds; //NOLINT

    try {
        Reply reply = r.sendCommand("smembers %d:volumes", localDomain);
        reply.toVector(volumeIds);

        if (volumeIds.empty()) {
            LOGWARN << "no volumes found for domain [" << localDomain << "]";
            return false;
        }

        for (uint i = 0; i < volumeIds.size(); i++) {
            volIds.push_back(volumeIds[i]);
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::getVolumes(std::vector<VolumeDesc>& volumes, int localDomain) {
    std::vector<long long> volumeIds; //NOLINT

    try {
        Reply reply = r.sendCommand("smembers %d:volumes", localDomain);
        reply.toVector(volumeIds);

        if (volumeIds.empty()) {
            LOGWARN << "no volumes found for domain [" << localDomain << "]";
            return false;
        }

        for (uint i = 0; i < volumeIds.size(); i++) {
            VolumeDesc volume("" , 1);  // dummy init
            getVolume(volumeIds[i], volume);
            volumes.push_back(volume);
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::getVolume(fds_volid_t volumeId, VolumeDesc& vol) {
    try {
        Reply reply = r.sendCommand("hgetall vol:%ld", volumeId);
        StringList strings;
        reply.toVector(strings);

        if (strings.empty()) {
            LOGWARN << "unable to find volume [" << volumeId <<"]";
            return false;
        }

        for (uint i = 0; i < strings.size(); i+= 2) {
            std::string& key = strings[i];
            std::string& value = strings[i+1];

            if (key == "uuid") {vol.volUUID = strtoull(value.c_str(), NULL, 10);}
            else if (key == "name") { vol.name = value; }
            else if (key == "tennant.id") { vol.tennantId = atoi(value.c_str()); }
            else if (key == "local.domain.id") {vol.localDomainId = atoi(value.c_str());}
            else if (key == "global.domain.id") { vol.globDomainId = atoi(value.c_str());}
            else if (key == "type") { vol.volType = (fpi::FDSP_VolType)atoi(value.c_str()); }
            else if (key == "capacity") { vol.capacity = strtod (value.c_str(), NULL);}
            else if (key == "quota.max") { vol.maxQuota = strtod (value.c_str(), NULL);}
            else if (key == "replica.count") {vol.replicaCnt = atoi(value.c_str());}
            else if (key == "objsize.max") {vol.maxObjSizeInBytes = atoi(value.c_str());}
            else if (key == "write.quorum") {vol.writeQuorum = atoi(value.c_str());}
            else if (key == "read.quorum") {vol.readQuorum = atoi(value.c_str());}
            else if (key == "conistency.protocol") {vol.consisProtocol = (fpi::FDSP_ConsisProtoType)atoi(value.c_str());} //NOLINT
            else if (key == "volume.policy.id") {vol.volPolicyId = atoi(value.c_str());}
            else if (key == "archive.policy.id") {vol.archivePolicyId = atoi(value.c_str());}
            else if (key == "placement.policy.id") {vol.placementPolicy = atoi(value.c_str());}
            else if (key == "app.workload") {vol.appWorkload = (fpi::FDSP_AppWorkload)atoi(value.c_str());} //NOLINT
            else if (key == "media.policy") {vol.mediaPolicy = (fpi::FDSP_MediaPolicy)atoi(value.c_str());} //NOLINT
            else if (key == "backup.vol.id") {vol.backupVolume = atol(value.c_str());}
            else if (key == "iops.min") {vol.iops_min = strtod (value.c_str(), NULL);}
            else if (key == "iops.max") {vol.iops_max = strtod (value.c_str(), NULL);}
            else if (key == "relative.priority") {vol.relativePrio = atoi(value.c_str());}
            else { //NOLINT
                LOGWARN << "unknown key for volume [" << volumeId <<"] - " << key;
                fds_assert(!"unknown key");
            }
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

// dlt

fds_uint64_t ConfigDB::getDltVersionForType(const std::string type, int localDomain) {
    try {
        Reply reply = r.sendCommand("get %d:dlt:%s", localDomain, type.c_str());
        if (!reply.isNil()) {
            return reply.getLong();
        } else {
            LOGNORMAL << "no dlt set for type:" << type;
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return 0;
}

bool ConfigDB::setDltType(fds_uint64_t version, const std::string type, int localDomain) {
    try {
        Reply reply = r.sendCommand("set %d:dlt:%s %ld", localDomain, type.c_str(), version);
        return reply.isOk();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::storeDlt(const DLT& dlt, const std::string type, int localDomain) {
    try {
        // fds_uint64_t version = dlt.getVersion();
        Reply reply = r.sendCommand("sadd %d:dlts %ld", localDomain, dlt.getVersion());
        if (!reply.wasModified()) {
            LOGWARN << "dlt [" << dlt.getVersion()
                    << "] is already in for domain [" << localDomain << "]";
        }

        std::string serializedData, hexCoded;
        const_cast<DLT&>(dlt).getSerialized(serializedData);
        r.encodeHex(serializedData, hexCoded);

        reply = r.sendCommand("set %d:dlt:%ld %s", localDomain, dlt.getVersion(), hexCoded.c_str());
        bool fSuccess = reply.isOk();

        if (fSuccess && !type.empty()) {
            reply = r.sendCommand("set %d:dlt:%s %ld", localDomain, type.c_str(), dlt.getVersion());
            if (!reply.isOk()) {
                LOGWARN << "error setting type " << dlt.getVersion() << ":" << type;
            }
        }
        return fSuccess;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::getDlt(DLT& dlt, fds_uint64_t version, int localDomain) {
    try {
        Reply reply = r.sendCommand("get %d:dlt:%ld", localDomain, version);
        std::string serializedData, hexCoded(reply.getString());
        if (hexCoded.length() < 10) {
            LOGERROR << "very less data for dlt : ["
                     << version << "] : size = " << hexCoded.length();
            return false;
        }
        LOGDEBUG << "dlt : [" << version << "] : size = " << hexCoded.length();
        r.decodeHex(hexCoded, serializedData);
        dlt.loadSerialized(serializedData);
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::loadDlts(DLTManager& dltMgr, int localDomain) {
    try {
        Reply reply = r.sendCommand("smembers %d:dlts", localDomain);
        std::vector<long long> dltVersions; //NOLINT
        reply.toVector(dltVersions);

        if (dltVersions.empty()) return false;

        for (uint i = 0; i < dltVersions.size(); i++) {
            DLT dlt(16, 4, 0, false);  // dummy init
            getDlt(dlt, dltVersions[i], localDomain);
            dltMgr.add(dlt);
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::storeDlts(DLTManager& dltMgr, int localDomain) {
    try {
        std::vector<fds_uint64_t> vecVersions;
        vecVersions = dltMgr.getDltVersions();

        for (uint i = 0; i < vecVersions.size(); i++) {
            const DLT* dlt = dltMgr.getDLT(vecVersions.size());
            if (!storeDlt(*dlt, "",  localDomain)) {
                LOGERROR << "unable to store dlt : ["
                         << dlt->getVersion() << "] for domain ["<< localDomain <<"]";
            }
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

// dmt
fds_uint64_t ConfigDB::getDmtVersionForType(const std::string type, int localDomain) {
    try {
        Reply reply = r.sendCommand("get %d:dmt:%s", localDomain, type.c_str());
        if (!reply.isNil()) {
            return reply.getLong();
        } else {
            LOGNORMAL << "no dmt set for type:" << type;
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return 0;
}

bool ConfigDB::setDmtType(fds_uint64_t version, const std::string type, int localDomain) {
    try {
        Reply reply = r.sendCommand("set %d:dmt:%s %ld", localDomain, type.c_str(), version);
        return reply.isOk();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::storeDmt(const DMT& dmt, const std::string type, int localDomain) {
    try {
        // fds_uint64_t version = dmt.getVersion();
        Reply reply = r.sendCommand("sadd %d:dmts %ld", localDomain, dmt.getVersion());
        if (!reply.wasModified()) {
            LOGWARN << "dmt [" << dmt.getVersion()
                    << "] is already in for domain ["
                    << localDomain << "]";
        }

        std::string serializedData, hexCoded;
        const_cast<DMT&>(dmt).getSerialized(serializedData);
        r.encodeHex(serializedData, hexCoded);

        reply = r.sendCommand("set %d:dmt:%ld %s", localDomain, dmt.getVersion(), hexCoded.c_str());
        bool fSuccess = reply.isOk();

        if (fSuccess && !type.empty()) {
            reply = r.sendCommand("set %d:dmt:%s %ld", localDomain, type.c_str(), dmt.getVersion());
            if (!reply.isOk()) {
                LOGWARN << "error setting type " << dmt.getVersion() << ":" << type;
            }
        }
        return fSuccess;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::getDmt(DMT& dmt, fds_uint64_t version, int localDomain) {
    try {
        Reply reply = r.sendCommand("get %d:dmt:%ld", localDomain, version);
        std::string serializedData, hexCoded(reply.getString());
        if (hexCoded.length() < 10) {
            LOGERROR << "very less data for dmt : ["
                     << version << "] : size = " << hexCoded.length();
            return false;
        }
        LOGDEBUG << "dmt : [" << version << "] : size = " << hexCoded.length();
        r.decodeHex(hexCoded, serializedData);
        dmt.loadSerialized(serializedData);
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

// nodes
bool ConfigDB::addNode(const NodeInvData& node) {
    TRACKMOD();
    // add the volume to the volume list for the domain
    int domainId = 0;  // TODO(prem)
    try {
        Reply reply = r.sendCommand("sadd %d:cluster:nodes %ld", domainId, node.nd_uuid);

        if (!reply.wasModified()) {
            LOGWARN << "node [" << node.nd_uuid
                    << "] already exists for domain [" << domainId << "]";
        }

        reply = r.sendCommand("hmset node:%ld uuid %ld"
                              " capacity %ld"
                              " ipnum %ld"
                              " ip %s"
                              " data.port %d"
                              " ctrl.port %d"
                              " migration.port %d"
                              " disk.type %d"
                              " name %s"
                              " type %d"
                              " state %d"
                              " dlt.version %ld"
                              " disk.capacity %ld"
                              " disk.iops.max %d"
                              " disk.iops.min %d"
                              " disk.latency.max %d"
                              " disk.latency.min %d"
                              " ssd.iops.max %d"
                              " ssd.iops.min %d"
                              " ssd.capacity %d"
                              " ssd.latency.max %d"
                              " ssd.latency.min %d",
                              node.nd_uuid, node.nd_uuid,
                              node.nd_gbyte_cap,
                              node.nd_ip_addr,
                              node.nd_ip_str.c_str(),
                              node.nd_data_port,
                              node.nd_ctrl_port,
                              node.nd_migration_port,
                              node.nd_disk_type,
                              node.nd_node_name.c_str(),
                              node.nd_node_type,
                              node.nd_node_state,
                              node.nd_dlt_version,
                              node.nd_capability.disk_capacity,
                              node.nd_capability.disk_iops_max,
                              node.nd_capability.disk_iops_min,
                              node.nd_capability.disk_latency_max,
                              node.nd_capability.disk_latency_min,
                              node.nd_capability.ssd_iops_max,
                              node.nd_capability.ssd_iops_min,
                              node.nd_capability.ssd_capacity,
                              node.nd_capability.ssd_latency_max,
                              node.nd_capability.ssd_latency_min);
        if (reply.isOk()) return true;
        LOGWARN << "msg: " << reply.getString();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::updateNode(const NodeInvData& node) {
    try {
        return addNode(node);
    } catch(const RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::removeNode(const NodeUuid& uuid) {
    TRACKMOD();
    try {
        int domainId = 0;  // TODO(prem)
        Reply reply = r.sendCommand("srem %d:cluster:nodes %ld", domainId, uuid);

        if (!reply.wasModified()) {
            LOGWARN << "node [" << uuid << "] does not exist for domain [" << domainId << "]";
        }

        // Now remove the node data
        reply = r.sendCommand("del node:%ld", uuid);
        return reply.isOk();
    } catch(RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::getNode(const NodeUuid& uuid, NodeInvData& node) {
    try {
        Reply reply = r.sendCommand("hgetall node:%ld", uuid);
        StringList strings;
        reply.toVector(strings);

        if (strings.empty()) {
            LOGWARN << "unable to find node [" << std::hex << uuid << std::dec << "]";
            return false;
        }

        for (uint i = 0; i < strings.size(); i+= 2) {
            std::string& key = strings[i];
            std::string& value = strings[i+1];
            node_capability_t& cap = node.nd_capability;
            if (key == "uuid") { node.nd_uuid = strtoull(value.c_str(), NULL, 10); }
            else if (key == "capacity") { node.nd_gbyte_cap  = atol(value.c_str()); }
            else if (key == "ipnum") { node.nd_ip_addr = atoi(value.c_str()); }
            else if (key == "ip") { node.nd_ip_str = value; }
            else if (key == "data.port") { node.nd_data_port = atoi(value.c_str()); }
            else if (key == "ctrl.port") { node.nd_ctrl_port = atoi(value.c_str()); }
            else if (key == "migration.port") { node.nd_migration_port  = atoi(value.c_str()); }
            else if (key == "disk.type") { node.nd_disk_type = atoi(value.c_str()); }
            else if (key == "name") { node.nd_node_name = value; }
            else if (key == "type") { node.nd_node_type = (fpi::FDSP_MgrIdType) atoi(value.c_str()); } //NOLINT
            else if (key == "state") { node.nd_node_state  = (fpi::FDSP_NodeState) atoi(value.c_str()); } //NOLINT
            else if (key == "dlt.version") { node.nd_dlt_version  = atol(value.c_str()); }
            else if (key == "disk.capacity") { cap.disk_capacity = atol(value.c_str()); }
            else if (key == "disk.iops.max") { cap.disk_iops_max  = atoi(value.c_str()); }
            else if (key == "disk.iops.min") { cap.disk_iops_min = atoi(value.c_str()); }
            else if (key == "disk.latency.max") { cap.disk_latency_max  = atoi(value.c_str()); }
            else if (key == "disk.latency.min") { cap.disk_latency_min = atoi(value.c_str()); }
            else if (key == "ssd.iops.max") { cap.ssd_iops_max = atoi(value.c_str()); }
            else if (key == "ssd.iops.min") { cap.ssd_iops_min = atoi(value.c_str()); }
            else if (key == "ssd.capacity") { cap.ssd_capacity = atoi(value.c_str()); }
            else if (key == "ssd.latency.max") { cap.ssd_latency_max = atoi(value.c_str()); }
            else if (key == "ssd.latency.min") { cap.ssd_latency_min = atoi(value.c_str()); }
            else { //NOLINT
                LOGWARN << "unknown key [" << key << "] for node [" << uuid << "]";
            }
        }
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::nodeExists(const NodeUuid& uuid) {
    try {
        Reply reply = r.sendCommand("exists node:%ld", uuid);
        return reply.isOk();
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::getNodeIds(std::unordered_set<NodeUuid, UuidHash>& nodes, int localDomain) {
    std::vector<long long> nodeIds; //NOLINT

    try {
        Reply reply = r.sendCommand("smembers %d:cluster:nodes", localDomain);
        reply.toVector(nodeIds);

        if (nodeIds.empty()) {
            LOGWARN << "no nodes found for domain [" << localDomain << "]";
            return false;
        }

        for (uint i = 0; i < nodeIds.size(); i++) {
            LOGDEBUG << "ConfigDB::getNodeIds node "
                     << std::hex << nodeIds[i] << std::dec;
            nodes.insert(NodeUuid(nodeIds[i]));
        }

        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

bool ConfigDB::getAllNodes(std::vector<NodeInvData>& nodes, int localDomain) {
    std::vector<long long> nodeIds; //NOLINT

    try {
        Reply reply = r.sendCommand("smembers %d:cluster:nodes", localDomain);
        reply.toVector(nodeIds);

        if (nodeIds.empty()) {
            LOGWARN << "no nodes found for domain [" << localDomain << "]";
            return false;
        }

        for (uint i = 0; i < nodeIds.size(); i++) {
            NodeInvData node;
            getNode(nodeIds[i], node);
            nodes.push_back(node);
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

std::string ConfigDB::getNodeName(const NodeUuid& uuid) {
    try {
        Reply reply = r.sendCommand("hget node:%ld name", uuid);
        return reply.getString();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return "";
}

bool ConfigDB::getNodeServices(const NodeUuid& uuid, NodeServices& services) {
    try{
        Reply reply = r.sendCommand("get %ld:services", uuid);
        if (reply.isNil()) return false;
        services.loadSerialized(reply.getString());
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::setNodeServices(const NodeUuid& uuid, const NodeServices& services) {
    try{
        std::string serialized;
        services.getSerialized(serialized);
        Reply reply = r.sendCommand("set %ld:services %b",
                                    uuid, serialized.data(), serialized.length());
        return reply.isOk();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

uint ConfigDB::getNodeNameCounter() {
    try{
        Reply reply = r.sendCommand("incr node:name.counter");
        return reply.getLong();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return 0;
}

// volume policies

bool ConfigDB::getPolicy(fds_uint32_t volPolicyId, FDS_VolumePolicy& policy, int localDomain) { //NOLINT
    try{
        Reply reply = r.sendCommand("get %d:volpolicy:%ld", localDomain, volPolicyId);
        if (reply.isNil()) return false;
        policy.loadSerialized(reply.getString());
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::addPolicy(const FDS_VolumePolicy& policy, int localDomain) {
    TRACKMOD();
    try{
        std::string serialized;

        Reply reply = r.sendCommand("sadd %d:volpolicies %ld", localDomain, policy.volPolicyId);
        if (!reply.wasModified()) {
            LOGWARN << "unable to add policy [" << policy.volPolicyId << " to domain [" << localDomain <<"]"; //NOLINT
        }
        policy.getSerialized(serialized);
        reply = r.sendCommand("set %d:volpolicy:%ld %b", localDomain, policy.volPolicyId, serialized.data(), serialized.length()); //NOLINT
        return reply.isOk();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::updatePolicy(const FDS_VolumePolicy& policy, int localDomain) {
    TRACKMOD();
    try {
        return addPolicy(policy, localDomain);
    } catch(const RedisException& e) {
        LOGERROR << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::deletePolicy(fds_uint32_t volPolicyId, int localDomain) {
    TRACKMOD();
    try{
        Reply reply = r.sendCommand("del %d:volpolicy:%ld", localDomain, volPolicyId);
        return reply.wasModified();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::getPolicies(std::vector<FDS_VolumePolicy>& policies, int localDomain) {
    try {
        std::vector<long long> volumePolicyIds; //NOLINT

        Reply reply = r.sendCommand("smembers %d:volpolicies", localDomain);
        reply.toVector(volumePolicyIds);

        if (volumePolicyIds.empty()) {
            LOGWARN << "no volPolcies found for domain [" << localDomain << "]";
            return false;
        }

        FDS_VolumePolicy policy;
        for (uint i = 0; i < volumePolicyIds.size(); i++) {
            if (getPolicy(volumePolicyIds[i], policy, localDomain)) {
                policies.push_back(policy);
            }
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

// stat streaming registrations
int32_t ConfigDB::getNewStreamRegistrationId() {
    try {
        Reply reply = r.sendCommand("incr streamreg:id");
        return reply.getLong();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return -1;
}

bool ConfigDB::addStreamRegistration(fpi::StreamingRegistrationMsg& streamReg) {
    TRACKMOD();
    try {
        boost::shared_ptr<std::string> serialized;

        Reply reply = r.sendCommand("sadd streamregs %d", streamReg.id);
        if (!reply.wasModified()) {
            LOGWARN << "unable to add streamreg [" << streamReg.id;
        }

        fds::serializeFdspMsg(streamReg, serialized);

        reply = r.sendCommand("set streamreg:%d %b", streamReg.id,
                              serialized->data(), serialized->length());
        return reply.isOk();
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::getStreamRegistration(int regId, fpi::StreamingRegistrationMsg& streamReg) {
    try {
        Reply reply = r.sendCommand("get streamreg:%d", regId);
        if (reply.isNil()) return false;
        fds::deserializeFdspMsg(reply.getString(), streamReg);
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }
    return false;
}

bool ConfigDB::removeStreamRegistration(int regId) {
    TRACKMOD();
    try {
        Reply reply = r.sendCommand("srem streamregs %d", regId);
        if (!reply.wasModified()) {
            LOGWARN << "unable to remove streamreg [" << regId << "] from set"
                    << " mebbe it does not exist";
        }

        reply = r.sendCommand("del streamreg:%d", regId);
        if (reply.getLong() == 0) {
            LOGWARN << "no items deleted";
        }
        return true;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }
    return false;
}

bool ConfigDB::getStreamRegistrations(std::vector<fpi::StreamingRegistrationMsg>& vecReg) {
    try {
        std::vector<long long> regIds; //NOLINT

        Reply reply = r.sendCommand("smembers streamregs");
        reply.toVector(regIds);

        if (regIds.empty()) {
            LOGWARN << "no stream registrations found ";
            return false;
        }

        fpi::StreamingRegistrationMsg regMsg;
        for (uint i = 0; i < regIds.size(); i++) {
            if (getStreamRegistration(regIds[i], regMsg)) {
                vecReg.push_back(regMsg);
            } else {
                LOGWARN << "unable to get reg: " << regIds[i];
            }
        }
        return true;
    } catch(RedisException& e) {
        LOGERROR << e.what();
    }
    return false;
}

int64_t ConfigDB::createTenant(const std::string& identifier) {
    TRACKMOD();
    try {
        // check if the tenant already exists
        std::string idLower = identifier;
        std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

        Reply reply = r.sendCommand("sismember tenant:list %b", idLower.data(), idLower.length());
        if (reply.getLong() == 1) {
            // the tenant already exists
            std::vector<fds::apis::Tenant> tenants;
            listTenants(tenants);
            for (const auto& tenant : tenants) {
                if (tenant.identifier == identifier) {
                    LOGWARN << "trying to add existing tenant : " << tenant.id;
                    NOMOD();
                    return tenant.id;
                }
            }
            LOGWARN << "tenant info missing : " << identifier;
            NOMOD();
            return 0;
        }

        // get new id
        reply = r.sendCommand("incr tenant:nextid");

        fds::apis::Tenant tenant;
        tenant.id = reply.getLong();
        tenant.identifier = identifier;

        r.sendCommand("sadd tenant:list %b", idLower.data(), idLower.length());

        // serialize
        boost::shared_ptr<std::string> serialized;
        fds::serializeFdspMsg(tenant, serialized);

        r.sendCommand("hset tenants %ld %b",tenant.id, serialized->data(), serialized->length()); //NOLINT
        return tenant.id;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
    }

    return 0;
}

bool ConfigDB::listTenants(std::vector<fds::apis::Tenant>& tenants) {
    fds::apis::Tenant tenant;

    try {
        Reply reply = r.sendCommand("hvals tenants");
        StringList strings;
        reply.toVector(strings);

        for (const auto& value : strings) {
            fds::deserializeFdspMsg(value, tenant);
            tenants.push_back(tenant);
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        return false;
    }
    return true;
}

int64_t ConfigDB::createUser(const std::string& identifier, const std::string& secret, bool isAdmin) { //NOLINT
    TRACKMOD();
    try {
        // check if the user already exists
        std::string idLower = identifier;
        std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

        Reply reply = r.sendCommand("sismember user:list %b", idLower.data(), idLower.length());
        if (reply.getLong() == 1) {
            // the user already exists
            std::vector<fds::apis::User> users;
            listUsers(users);
            for (const auto& user : users) {
                if (user.identifier == identifier) {
                    LOGWARN << "trying to add existing user : " << user.id;
                    NOMOD();
                    return user.id;
                }
            }
            LOGWARN << "user info missing : " << identifier;
            NOMOD();
            return 0;
        }

        fds::apis::User user;
        // get new id
        reply = r.sendCommand("incr user:nextid");
        user.id = reply.getLong();
        user.identifier = identifier;
        user.secret = secret;
        user.isFdsAdmin = isAdmin;

        r.sendCommand("sadd user:list %b", idLower.data(), idLower.length());

        // serialize
        boost::shared_ptr<std::string> serialized;
        fds::serializeFdspMsg(user, serialized);

        r.sendCommand("hset users %ld %b",user.id, serialized->data(), serialized->length()); //NOLINT
        return user.id;
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
    }

    return 0;
}

bool ConfigDB::listUsers(std::vector<fds::apis::User>& users) {
    fds::apis::User user;

    try {
        Reply reply = r.sendCommand("hvals users");
        StringList strings;
        reply.toVector(strings);

        for (const auto& value : strings) {
            fds::deserializeFdspMsg(value, user);
            users.push_back(user);
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        return false;
    }
    return true;
}

bool ConfigDB::getUser(int64_t userId, fds::apis::User& user) {
    try {
        Reply reply = r.sendCommand("hget users %ld", userId);

        if (reply.isNil()) {
            LOGWARN << "userinfo does not exist, userid: " << userId;
            return false;
        }

        fds::deserializeFdspMsg(reply.getString(), user);
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        return false;
    }
    return true;
}

bool ConfigDB::assignUserToTenant(int64_t userId, int64_t tenantId) {
    TRACKMOD();
    try {
        Reply reply = r.sendCommand("sadd tenant:%ld:users %ld", tenantId, userId);
        if (!reply.wasModified()) {
            LOGWARN << "user: " << userId << " is already assigned to tenant: " << tenantId;
            NOMOD();
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
        return false;
    }
    return true;
}

bool ConfigDB::revokeUserFromTenant(int64_t userId, int64_t tenantId) {
    TRACKMOD();
    try {
        Reply reply = r.sendCommand("sremove tenant:%ld:users %ld", tenantId, userId);
        if (!reply.wasModified()) {
            LOGWARN << "user: " << userId << " was NOT assigned to tenant: " << tenantId;
            NOMOD();
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
        return false;
    }
    return true;
}

bool ConfigDB::listUsersForTenant(std::vector<fds::apis::User>& users, int64_t tenantId) {
    fds::apis::User user;
    try {
        // get the list of users assigned to the tenant
        Reply reply = r.sendCommand("smembers tenant:%ld:users", tenantId);
        StringList strings;
        reply.toVector(strings);
        std::string userlist;
        userlist.reserve(2048);
        userlist.append("hmget users");

        for (const auto& value : strings) {
            userlist.append(" ");
            userlist.append(value);
        }

        LOGDEBUG << "users for tenant:" << tenantId << " are [" << userlist << "]";

        reply = r.sendCommand(userlist.c_str());
        strings.clear();
        reply.toVector(strings);
        for (const auto& value : strings) {
            fds::deserializeFdspMsg(value, user);
            users.push_back(user);
        }
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        return false;
    }
    return true;
}

bool ConfigDB::updateUser(int64_t  userId, const std::string& identifier, const std::string& secret, bool isFdsAdmin) { //NOLINT
    TRACKMOD();
    try {
        fds::apis::User user;
        if (!getUser(userId, user)) {
            LOGWARN << "user does not exist, userid: " << userId;
            NOMOD();
            throw ConfigException("user id is invalid");
        }

        std::string idLower = identifier;
        std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

        // check if the identifier is changing ..
        if (0 != strcasecmp(identifier.c_str(), user.identifier.c_str())) {
            Reply reply = r.sendCommand("sismember user:list %b", idLower.data(), idLower.length());
            if (reply.getLong() == 1) {
                LOGWARN << "another user exists with identifier: " << identifier;
                NOMOD();
                throw ConfigException("another user exists with identifier");
            }
        }

        // now set the new user info
        user.identifier = identifier;
        user.secret = secret;
        user.isFdsAdmin = isFdsAdmin;

        r.sendCommand("sadd user:list %b", idLower.data(), idLower.length());

        // serialize
        boost::shared_ptr<std::string> serialized;
        fds::serializeFdspMsg(user, serialized);

        r.sendCommand("hset users %ld %b",user.id, serialized->data(), serialized->length()); //NOLINT
    } catch(const RedisException& e) {
        LOGCRITICAL << "error with redis " << e.what();
        NOMOD();
        return false;
    }
    return true;
}

}  // namespace kvstore
}  // namespace fds
