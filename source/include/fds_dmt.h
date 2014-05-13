/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_FDS_DMT_H_
#define SOURCE_INCLUDE_FDS_DMT_H_

#include <string>
#include <vector>
#include <set>
#include <map>

#include <fds_types.h>
#include <fds_error.h>
#include <concurrency/RwLock.h>
#include <serialize.h>
#include <util/Log.h>
#include <fds_placement_table.h>

namespace fds {

#define DMT_VER_INVALID 0  /**< Defines 0 as invalid DMT version */

    typedef TableColumn DmtColumn;
    typedef boost::shared_ptr<DmtColumn> DmtColumnPtr;
    typedef boost::shared_ptr<std::vector<DmtColumnPtr>> DmtTablePtr;

    class DMT: public serialize::Serializable {
  public:
        DMT(fds_uint32_t _width,
            fds_uint32_t _depth,
            fds_uint64_t _version,
            fds_bool_t alloc_cols = true);

        virtual ~DMT();

        /**
         * Accessors
         */
        fds_uint64_t getVersion() const; /**< Gets DMT version */
        fds_uint32_t getDepth() const;  /**< Gets num replicas */
        fds_uint32_t getNumColumns() const;  /**< Gets num columns */

        /**
         * Get node group that is responsible for given volume 'volume_id',
         * or volume range in the given DMT column 'col_index'
         * @return array of Node uuids where the node under index 0
         * is the primary node
         */
        DmtColumnPtr getNodeGroup(fds_uint32_t col_index) const;
        DmtColumnPtr getNodeGroup(fds_volid_t volume_id) const;

        /**
         * Set the node group or particular cell in the DMT
         */
        void setNodeGroup(fds_uint32_t col_index, const DmtColumn& nodes);
        void setNode(fds_uint32_t col_index,
                     fds_uint32_t row_index,
                     const NodeUuid& uuid);

        /**
         * For serializing and de-serializing
         */
        uint32_t virtual write(serialize::Serializer* s) const;
        uint32_t virtual read(serialize::Deserializer* d);

        friend std::ostream& operator<< (std::ostream &out, const DMT& dmt);

        /**
         * Print the DMT into the log. If log-level is debug, prints
         * node uuids in every cell.
         */
        void dump() const;

        /**
         * Checks if DMT is valid
         * Invalid cases:
         *    -- A column has repeating node uuids (non-unique)
         *    -- A cell in a DMT has an invalid Service UUID
         * @return ERR_OK of ERR_DMT_INVALID
         */
        Error verify() const;

  private:  // methods
        /**
         * Returns a set of nodes in DMT
         */
        void getUniqueNodes(std::set<fds_uint64_t>* ret_nodes) const;

  private:
        fds_uint64_t version;  /**< DMT version */
        fds_uint32_t depth;  /**< DMT column size */
        fds_uint32_t width;  /**< num columns = 2^width */
        fds_uint32_t columns;  /**< number of columns = 2^width */

        DmtTablePtr dmt_table;  /**< DMT table */
    };

    typedef boost::shared_ptr<DMT> DMTPtr;
    typedef enum {
        DMT_COMMITTED,
        DMT_TARGET,
        DMT_OLD,
    } DMTType;

    /**
     * Manages multiple DMT versions. Differentiates
     * between target and commited DMTs
     */
    class DMTManager {
  public:
        /**
         * DMTManager always able to maintain commited and target
         * DMTs. If 'history_dmts' > 0, then DMTManager will keep
         * extra history of 'history_dmts'
         */
        explicit DMTManager(fds_uint32_t history_dmts = 0);
        virtual ~DMTManager();

        Error add(DMT* dmt, DMTType dmt_type);
        Error addSerialized(std::string& data, DMTType dmt_type);

        /**
         * Sets given version of DMT as commited
         */
        void commitDMT();
        inline fds_uint64_t getCommittedVersion() const {
            return committed_version;
        }
        inline fds_uint64_t getTargetVersion() const {
            return target_version;
        }
        inline fds_bool_t hasCommittedDMT() const {
            return (committed_version != DMT_VER_INVALID);
        };
        inline fds_bool_t hasTargetDMT() const {
            return (target_version != DMT_VER_INVALID);
        };

        /**
         * Shortcut to get node group for a given volume 'volume_id'
         * from commited DMT. Asserts if there is no commited DMT
         */
        DmtColumnPtr getCommittedNodeGroup(fds_volid_t volume_id);

        /**
         * Returns DMT of given type, type must be either commited
         * or target. Asserts if there is no DMT of requested type.
         */
        DMTPtr getDMT(DMTType dmt_type);
        /**
         * Returns DMT of a given version; returns NULL if version
         * is not stored in DMTManager
         */
        DMTPtr getDMT(fds_uint64_t version);

        friend std::ostream& operator<< (std::ostream &out,
                                         const DMTManager& dmt_mgr);

  private:
        fds_uint32_t max_dmts;
        fds_uint64_t committed_version;  /**< version of commited DMT */
        fds_uint64_t target_version;  /**< version of target DMT or invalid */
        std::map<fds_uint64_t, DMTPtr> dmt_map;  /**< version to DMT map */
        fds_rwlock dmt_lock;  /**< lock protecting dmt_map */
    };
    typedef boost::shared_ptr<DMTManager> DMTManagerPtr;


}  // namespace fds

#endif  // SOURCE_INCLUDE_FDS_DMT_H_
