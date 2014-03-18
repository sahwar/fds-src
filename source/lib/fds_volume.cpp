/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <fds_volume.h>
#include <string>
#include <queue>
namespace fpi = FDS_ProtocolInterface;

namespace fds {

VolumeDesc::VolumeDesc(const fpi::FDSP_VolumeInfoType& volinfo,
                       fds_volid_t vol_uuid) {
    name = volinfo.vol_name;
    tennantId = volinfo.tennantId;
    localDomainId = volinfo.localDomainId;
    globDomainId = volinfo.globDomainId;
    volUUID = vol_uuid;
    volType = volinfo.volType;
    capacity = volinfo.capacity;
    maxQuota = volinfo.maxQuota;
    replicaCnt = volinfo.defReplicaCnt;
    writeQuorum = volinfo.defWriteQuorum;
    readQuorum = volinfo.defReadQuorum;
    consisProtocol = volinfo.defConsisProtocol;
    volPolicyId = volinfo.volPolicyId;
    archivePolicyId = volinfo.archivePolicyId;
    placementPolicy = volinfo.placementPolicy;
    appWorkload = volinfo.appWorkload;
    backupVolume = volinfo.backupVolume;
    /// FDSP_VolumeInfoTypePtr does not contain policy details
    // but it is so far only called in orchMgr where policy
    // details are explicitly set after the construction, so
    // just fill in zeros here
    iops_min = 0;
    iops_max = 0;
    relativePrio = 0;
    assert(volUUID != invalid_vol_id);
}

VolumeDesc::VolumeDesc(const VolumeDesc& vdesc) {
    name = vdesc.name;
    tennantId = vdesc.tennantId;
    localDomainId = vdesc.localDomainId;
    globDomainId = vdesc.globDomainId;
    volUUID = vdesc.volUUID;
    volType = vdesc.volType;
    capacity = vdesc.capacity;
    maxQuota = vdesc.maxQuota;
    replicaCnt = vdesc.replicaCnt;
    writeQuorum = vdesc.writeQuorum;
    readQuorum = vdesc.readQuorum;
    consisProtocol = vdesc.consisProtocol;
    volPolicyId = vdesc.volPolicyId;
    archivePolicyId = vdesc.archivePolicyId;
    placementPolicy = vdesc.placementPolicy;
    appWorkload = vdesc.appWorkload;
    backupVolume = vdesc.backupVolume;
    iops_min = vdesc.iops_min;
    iops_max = vdesc.iops_max;
    relativePrio = vdesc.relativePrio;
    assert(volUUID != invalid_vol_id);
}

VolumeDesc::VolumeDesc(const fpi::FDSP_VolumeDescType& voldesc) {
    name = voldesc.vol_name;
    tennantId = voldesc.tennantId;
    localDomainId = voldesc.localDomainId;
    globDomainId = voldesc.globDomainId;
    volUUID = voldesc.volUUID;
    volType = voldesc.volType;
    capacity = voldesc.capacity;
    maxQuota = voldesc.maxQuota;
    replicaCnt = voldesc.defReplicaCnt;
    writeQuorum = voldesc.defWriteQuorum;
    readQuorum = voldesc.defReadQuorum;
    consisProtocol = voldesc.defConsisProtocol;
    volPolicyId = voldesc.volPolicyId;
    archivePolicyId = voldesc.archivePolicyId;
    placementPolicy = voldesc.placementPolicy;
    appWorkload = voldesc.appWorkload;
    backupVolume = voldesc.backupVolume;
    iops_min = voldesc.iops_min;
    iops_max = voldesc.iops_max;
    relativePrio = voldesc.rel_prio;
    assert(volUUID != invalid_vol_id);
}

/*
 * Used for testing where we don't have all of these fields.
 */
VolumeDesc::VolumeDesc(const std::string& _name, fds_volid_t _uuid)
        : name(_name),
          volUUID(_uuid) {
    assert(_uuid != invalid_vol_id);

    tennantId = 0;
    localDomainId = 0;
    globDomainId = 0;
    volType = fpi::FDSP_VOL_S3_TYPE;
    capacity = 0;
    maxQuota = 0;
    replicaCnt = 0;
    writeQuorum = 0;
    readQuorum = 0;
    consisProtocol = fpi::FDSP_CONS_PROTO_STRONG;
    volPolicyId = 0;
    archivePolicyId = 0;
    placementPolicy = 0;
    appWorkload = fpi::FDSP_APP_WKLD_TRANSACTION;
    backupVolume = 0;
    iops_min = 0;
    iops_max = 0;
    relativePrio = 0;
    assert(volUUID != invalid_vol_id);
}

VolumeDesc::VolumeDesc(const std::string& _name,
                       fds_volid_t _uuid,
                       fds_uint32_t _iops_min,
                       fds_uint32_t _iops_max,
                       fds_uint32_t _priority)
        : name(_name),
          volUUID(_uuid),
          iops_min(_iops_min),
          iops_max(_iops_max),
          relativePrio(_priority) {
    fds_assert(volUUID != invalid_vol_id);

    tennantId = 0;
    localDomainId = 0;
    globDomainId = 0;
    volType = fpi::FDSP_VOL_S3_TYPE;
    capacity = 0;
    maxQuota = 0;
    replicaCnt = 0;
    writeQuorum = 0;
    readQuorum = 0;
    consisProtocol = fpi::FDSP_CONS_PROTO_STRONG;
    volPolicyId = 0;
    archivePolicyId = 0;
    placementPolicy = 0;
    appWorkload = fpi::FDSP_APP_WKLD_TRANSACTION;
    backupVolume = 0;
}

VolumeDesc::~VolumeDesc() {
}

void VolumeDesc::modifyPolicyInfo(fds_uint64_t _iops_min,
                                  fds_uint64_t _iops_max,
                                  fds_uint32_t _priority)
{
    iops_min = _iops_min;
    iops_max = _iops_max;
    relativePrio = _priority;
}

std::string VolumeDesc::getName() const {
    return name;
}

fds_volid_t VolumeDesc::GetID() const {
    return volUUID;
}

double VolumeDesc::getIopsMin() const {
    return iops_min;
}

double VolumeDesc::getIopsMax() const {
    return iops_max;
}

int VolumeDesc::getPriority() const {
    return relativePrio;
}

std::string VolumeDesc::ToString() {
    return (std::string("Vol<") + getName() +
            std::string(":") + std::to_string(GetID()) +
            std::string(">"));
}

bool VolumeDesc::operator==(const VolumeDesc &rhs) const {
    return (this->volUUID == rhs.volUUID );
}

bool VolumeDesc::operator!=(const VolumeDesc &rhs) const {
    return !(*this == rhs);
}

VolumeDesc& VolumeDesc::operator=(const VolumeDesc& volinfo) {
    this->name = volinfo.name;
    this->tennantId = volinfo.tennantId;
    this->localDomainId = volinfo.localDomainId;
    this->globDomainId = volinfo.globDomainId;
    this->volUUID = volinfo.volUUID;
    this->volType = volinfo.volType;
    this->capacity = volinfo.capacity;
    this->maxQuota = volinfo.maxQuota;
    this->replicaCnt = volinfo.replicaCnt;
    this->writeQuorum = volinfo.writeQuorum;
    this->readQuorum = volinfo.readQuorum;
    this->consisProtocol = volinfo.consisProtocol;
    this->volPolicyId = volinfo.volPolicyId;
    this->archivePolicyId = volinfo.archivePolicyId;
    this->placementPolicy = volinfo.placementPolicy;
    this->appWorkload = volinfo.appWorkload;
    this->backupVolume = volinfo.backupVolume;
    return *this;
}

std::ostream& operator<<(std::ostream& os, const VolumeDesc& vol) {
    return os << "["
              << " uuid:" << vol.volUUID
              << " name:" << vol.name
              << " tenant:" << vol.tennantId
              << " localdomain:" <<vol.localDomainId
              << " global.domain:" << vol.globDomainId
              << " type:" << vol.volType
              << " capacity:" << vol.capacity
              << " quota:" << vol.maxQuota
              << " replica:" << vol.replicaCnt
              << " write.quorum:" << vol.writeQuorum
              << " read.quorum:" << vol.readQuorum
              << " consis.proto:" << vol.consisProtocol
              << " vol.policy.id:" << vol.volPolicyId
              << " archive.policy.id:" << vol.archivePolicyId
              << " placement.policy:" << vol.placementPolicy
              << " app.workload:" << vol.appWorkload
              << " backup.vol:" << vol.backupVolume
              << " iops.min:" << vol.iops_min
              << " iops.max:" << vol.iops_max
              << " rel.prio:" << vol.relativePrio
              << " ]";
}

/************************************************************************************/

FDS_Volume::FDS_Volume()
{
}

FDS_Volume::FDS_Volume(const VolumeDesc& vol_desc)
        : FDS_Volume()
{
    voldesc = new VolumeDesc(vol_desc);
}

FDS_Volume::~FDS_Volume() {
    delete voldesc;
}


FDS_VolumePolicy::FDS_VolumePolicy() {
}

FDS_VolumePolicy::~FDS_VolumePolicy() {
}

uint32_t FDS_VolumePolicy::write(serialize::Serializer* s) const {
    uint32_t b = 0;
    b += s->writeI32(volPolicyId);
    b += s->writeI64(iops_max);
    b += s->writeI64(iops_min);
    b += s->writeI64(thruput);
    b += s->writeI32(relativePrio);
    b += s->writeString(volPolicyName);
    return b;
}

uint32_t FDS_VolumePolicy::read(serialize::Deserializer* d) {
    uint32_t b = 0;
    b += d->readI32(volPolicyId);
    b += d->readI64(iops_max);
    b += d->readI64(iops_min);
    b += d->readI64(thruput);
    b += d->readI32(relativePrio);
    b += d->readString(volPolicyName);
    return b;
}

uint32_t FDS_VolumePolicy::getEstimatedSize() const {
    return 2*4 + 3*8 + 4 + volPolicyName.length();
}

std::ostream& operator<<(std::ostream& os, const FDS_VolumePolicy& policy) {
    os << "["
       << " id:" << policy.volPolicyId
       << " name:" << policy.volPolicyName
       << " iops.max:" << policy.iops_max
       << " iops.min:" << policy.iops_min
       << " thruput:" << policy.thruput
       << " relative.prio:" << policy.relativePrio
       << " ]";
    return os;
}

/***********************************************************************************/

FDS_VolumeQueue::FDS_VolumeQueue(fds_uint32_t q_capacity,
                                 fds_uint64_t _iops_max,
                                 fds_uint64_t _iops_min,
                                 fds_uint32_t prio) :
        iops_max(_iops_max),
        iops_min(_iops_min),
        priority(prio) {
    volQueue = new boost::lockfree::queue<FDS_IOType *> (q_capacity);
    volQState = FDS_VOL_Q_INACTIVE;
}

FDS_VolumeQueue::~FDS_VolumeQueue() {
    delete volQueue;
}

void FDS_VolumeQueue::modifyQosParams(fds_uint64_t _iops_min,
                                      fds_uint64_t _iops_max,
                                      fds_uint32_t _prio)
{
    iops_max = _iops_max;
    iops_min = _iops_min;
    priority = _prio;
}

void FDS_VolumeQueue::activate() {
    volQState = FDS_VOL_Q_ACTIVE;
}


// Quiesce queued IOs on this queue & block any new IOs
void  FDS_VolumeQueue::quiesceIOs() {
    volQState = FDS_VOL_Q_QUIESCING;
}

void   FDS_VolumeQueue::suspendIO() {
    volQState = FDS_VOL_Q_SUSPENDED;
}

void   FDS_VolumeQueue::resumeIO() {
    volQState = FDS_VOL_Q_ACTIVE;
}

void FDS_VolumeQueue::enqueueIO(FDS_IOType *io) {
    if (volQState == FDS_VOL_Q_ACTIVE) {
        while (!volQueue->push(io)){}
    }
}

FDS_IOType   *FDS_VolumeQueue::dequeueIO() {
    FDS_IOType *io = NULL;
    if ( volQState == FDS_VOL_Q_ACTIVE || volQState == FDS_VOL_Q_QUIESCING ) {
        volQueue->pop(io);
        if (volQState == FDS_VOL_Q_QUIESCING && volQueue->empty())  {
            volQState = FDS_VOL_Q_SUSPENDED;
        }
        return io;
    }
    return NULL;
}

}  // namespace fds
