/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

/*
 * Volume metadata class. Describes the per-volume metada
 * that is maintained by a data manager.
 */

#ifndef SOURCE_DATA_MANAGER_VOLUMEMETA_H_
#define SOURCE_DATA_MANAGER_VOLUMEMETA_H_

#include <string>

#include "include/fds_types.h"
#include "include/fds_err.h"
#include "util/Log.h"
#include "lib/VolumeCatalog.h"
#include "util/concurrency/Mutex.h"
#include "include/fds_volume.h"

namespace fds {

  class VolumeMeta {
 private:
    fds_mutex  *vol_mtx;
    VolumeDesc *vol_desc;

    /*
     * The volume catalog maintains mappings from
     * vol/blob/offset to object id.
     */
    VolumeCatalog *vcat;
    /*
     * The time catalog maintains pending changes to
     * the volume catalog.
     */
    TimeCatalog *tcat;

    /*
     * A logger received during instantiation.
     * It is allocated and destroyed by the
     * caller.
     */
    fds_log *dm_log;

    /*
     * This class is non-copyable.
     * Disable copy constructor/assignment operator.
     */
    VolumeMeta(const VolumeMeta& _vm);
    VolumeMeta& operator=(const VolumeMeta rhs);

 public:
    /*
     * Default constructor should NOT be called
     * directly. It is needed for STL data struct
     * support.
     */
    VolumeMeta();
    VolumeMeta(const std::string& _name,
               fds_volid_t _uuid);
    VolumeMeta(const std::string& _name,
               fds_volid_t _uuid,
               fds_log* _dm_log);
    ~VolumeMeta();

    Error OpenTransaction(fds_uint32_t vol_offset,
                          const ObjectID &oid);
    Error QueryVcat(fds_uint32_t vol_offset,
                    ObjectID *oid);
  };

}  // namespace fds

#endif  // SOURCE_DATA_MANAGER_VOLUMEMETA_H_
