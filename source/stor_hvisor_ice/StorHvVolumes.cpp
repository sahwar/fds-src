/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include "StorHvisorNet.h"
#include "StorHvJournal.h"

#include "StorHvVolumes.h"

extern StorHvCtrl *storHvisor;

namespace fds {

StorHvVolume::StorHvVolume(const VolumeDesc& vdesc, StorHvCtrl *sh_ctrl, fds_log *parent_log)
  : FDS_Volume(vdesc), parent_sh(sh_ctrl)
{
  journal_tbl = new StorHvJournal(FDS_READ_WRITE_LOG_ENTRIES);
  vol_catalog_cache = new VolumeCatalogCache(volUUID, sh_ctrl, parent_log);  

  is_valid = true;
}

StorHvVolume::~StorHvVolume()
{
  if (journal_tbl)
    delete journal_tbl;
  if (vol_catalog_cache)
    delete vol_catalog_cache;
}

/* safely destroys journal table and volume catalog cache
 * after this, object is not valid */
void StorHvVolume::destroy()
{
  rwlock.write_lock();
  if (!is_valid) {
    rwlock.write_unlock();
    return;
  }

  /* destroy data */
  delete journal_tbl;
  journal_tbl = NULL;
  delete vol_catalog_cache;
  vol_catalog_cache = NULL;

  is_valid = false;
  rwlock.write_unlock();
}

bool StorHvVolume::isValidLocked() const
{
  return is_valid;
}

void StorHvVolume::readLock()
{
  rwlock.read_lock();
}

void StorHvVolume::readUnlock()
{
  rwlock.read_unlock();
}

StorHvVolumeLock::StorHvVolumeLock(StorHvVolume *vol)
{
  shvol = vol;
  if (shvol) shvol->readLock();
}

StorHvVolumeLock::~StorHvVolumeLock()
{
  if (shvol) shvol->readUnlock();
}

/***** StorHvVolumeTable methods ******/

/* creates its own logger */
StorHvVolumeTable::StorHvVolumeTable(StorHvCtrl *sh_ctrl, fds_log *parent_log)
  : parent_sh(sh_ctrl)
{
  if (parent_log) {
    vt_log = parent_log;
    created_log = false;
  }
  else {
    vt_log = new fds_log("voltab", "logs");
    created_log = true;
  }

  /* register for volume-related control events from OM*/
  if (parent_sh->om_client) {
    parent_sh->om_client->registerEventHandlerForVolEvents(volumeEventHandler);
  }

  /* 
   * Hardcoding creation of volume 1
   * TODO: do not hardcode creation of volume 1, should
   * explicitly call register volume for all volumes 
   */
  VolumeDesc vdesc("default_vol", FDS_DEFAULT_VOL_UUID);
  volume_map[FDS_DEFAULT_VOL_UUID] = new StorHvVolume(vdesc, parent_sh, vt_log);
  FDS_PLOG(vt_log) << "StorHvVolumeTable - constructor registered volume 1";  
}

StorHvVolumeTable::StorHvVolumeTable(StorHvCtrl *sh_ctrl)
  : StorHvVolumeTable(sh_ctrl, NULL) {
}

StorHvVolumeTable::~StorHvVolumeTable()
{
  /*
   * Iterate volume_map and free the volume pointers
   */
  map_rwlock.write_lock();
  for (std::unordered_map<fds_volid_t, StorHvVolume*>::iterator it = volume_map.begin();
       it != volume_map.end();
       ++it)
    {
      StorHvVolume *vol = it->second;
      vol->destroy();
      delete vol;
    }
  volume_map.clear();
  map_rwlock.write_unlock();

  /*
   * Delete log if we created one
   */
  if (created_log) {
    delete vt_log;
  }
}


/*
 * Creates volume if it has not been created yet
 * Does nothing if volume is already registered
 */
Error StorHvVolumeTable::registerVolume(const VolumeDesc& vdesc)
{
  Error err(ERR_OK);
  fds_volid_t vol_uuid = vdesc.GetID();

  map_rwlock.write_lock();
  if (volume_map.count(vol_uuid) == 0) {
    StorHvVolume* new_vol = new StorHvVolume(vdesc, parent_sh, vt_log);
    if (new_vol) {
      volume_map[vol_uuid] = new_vol;
    }
    else {
      err = ERR_INVALID_ARG; // TODO: need more error types
    }
  }
  map_rwlock.write_unlock();

  FDS_PLOG(vt_log) << "StorHvVolumeTable - Register new volume " << vol_uuid
                   << " result: " << err.GetErrstr();  
  
  return err;
}

/*
 * Removes volume from the map, returns error if volume does not exist
 */
Error StorHvVolumeTable::removeVolume(fds_volid_t vol_uuid)
{
  Error err(ERR_OK);
  StorHvVolume *vol = NULL;

  map_rwlock.write_lock();
  if (volume_map.count(vol_uuid) == 0) {
    FDS_PLOG(vt_log) << "StorHvVolumeTable - removeVolume called for non-existing volume "
                     << vol_uuid;
    err = ERR_INVALID_ARG;
    map_rwlock.write_unlock();
    return err;
  }

  vol = volume_map[vol_uuid];
  volume_map.erase(vol_uuid);
  map_rwlock.write_unlock();

  /* call destroy first to ensure that no readers accessing volume
   * when destroying its data */
  vol->destroy();
  delete vol;
  
  FDS_PLOG(vt_log) << "StorHvVolumeTable - Removed volume "
                   << vol_uuid;
 
  return err;
}

/*
 * Returns SH volume object or NULL if the volume has not been created
 * Not thread-safe, so use StorHvVolumeLock to lock the volume and check
 * if volume object is still valid via StorHvVolume::isValidLocked()
 * before using the volume object
 */
StorHvVolume* StorHvVolumeTable::getVolume(fds_volid_t vol_uuid)
{
  StorHvVolume *ret_vol = NULL;

  map_rwlock.read_lock();
  if (volume_map.count(vol_uuid) > 0) {
    ret_vol = volume_map[vol_uuid];
  }
  else {
    FDS_PLOG(vt_log) << "StorHvVolumeTable::getVolume - Volume " << vol_uuid
                     << " does not exist";    
  }
  map_rwlock.read_unlock();

  return ret_vol;
}

/*
 * Returns the locked volume object. Guarantees that returned volume
 * object is valid (i.e., can safely access journal table and volume catalog cache)
 * Call StorHvVolume::readUnlock() when finished using volume object
 */
StorHvVolume* StorHvVolumeTable::getLockedVolume(fds_volid_t vol_uuid)
{
  StorHvVolume *ret_vol = getVolume(vol_uuid);

  if (ret_vol) {
    ret_vol->readLock();
    if (!ret_vol->isValidLocked())
    {
      ret_vol->readUnlock();
      FDS_PLOG(vt_log) << "StorHvVolumeTable::getLockedVolume - Volume " << vol_uuid
                       << "does not exist anymore, must have beed destroyed";
      return NULL;
    }    
  }

  return ret_vol;
}

/*
 * Handler for volume-related control message from OM
 */
void StorHvVolumeTable::volumeEventHandler(fds_volid_t vol_uuid,
                                           VolumeDesc *vdb,
                                           fds_vol_notify_t vol_action)
{
  switch (vol_action) {
  case fds_notify_vol_attatch:
    FDS_PLOG(storHvisor->GetLog()) << "StorHvVolumeTable - Received volume attach event from OM"
                                   << " for volume " << vol_uuid;

    storHvisor->vol_table->registerVolume(vdb ? *vdb : VolumeDesc("", vol_uuid));
    break;
  case fds_notify_vol_detach:
    FDS_PLOG(storHvisor->GetLog()) << "StorHvVolumeTable - Received volume detach event from OM"
                                   << " for volume " << vol_uuid;
    storHvisor->vol_table->removeVolume(vol_uuid);
    break;
  default:
    FDS_PLOG(storHvisor->GetLog()) << "StorHvVolumeTable - Received unexpected volume event from OM"
                                   << " for volume " << vol_uuid;
  } 
}

/* print detailed info into log */
void StorHvVolumeTable::dump()
{
  map_rwlock.read_lock();
  for (std::unordered_map<fds_volid_t, StorHvVolume*>::iterator it = volume_map.begin(); 
       it != volume_map.end(); 
       ++it)
    {
      FDS_PLOG(vt_log) << "StorHvVolumeTable - existing volume" 
                       << "UUID " << it->second->volUUID
                       << ", name " << it->second->vol_name
                       << ", type " << it->second->volType;
    }
  map_rwlock.read_unlock();
}

} // namespace fds
