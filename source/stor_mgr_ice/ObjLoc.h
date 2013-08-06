#ifndef __OBJ_LOC_H__
#define __OBJ_LOC_H__
#include "include/fds_types.h"

using namespace fds;

typedef enum {
FDS_DATA_LOC_TIER_SSD,
FDS_DATA_LOC_TIER_HD,
FDS_DATA_LOC_TIER_DRAM
} fds_data_tier_t;

#define FDS_MAX_VOL_IDS  32

class FDSDataLocEntryType {
public:
 fds_char_t    shard_file_name[64];
 fds_long_t    shard_file_offset; // This is the present offset at which append should start
 fds_uint32_t  disk_num;
};


class FDSObjLocEntryType {
public:
 ObjectID     object_id;
 fds_uint32_t	     obj_state; // 0 --> deleted, 1+ --> number of volumes pointing to this obj
 fds_data_tier_t     obj_tier;
 FDSDataLocEntryType object_location;
 fds_uint32_t        vol_list[FDS_MAX_VOL_IDS]; /* Association entry: List of volumes pointing to this object */
} ;


// fds_object_location_entry_t fds_object_loc_cache[4096];
#endif
