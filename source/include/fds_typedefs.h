#ifndef SOURCE_FDS_TYPEDEFS_H_
#define SOURCE_FDS_TYPEDEFS_H_

/**
 * All system wide type definitions.
 * Please include the appropriate header files
 */

#include <string>

#include <fds_types.h>
#include <fds_resource.h>

namespace fds {
    typedef std::string NodeStrName;
    typedef ResourceUUID NodeUuid;

}  // namespace fds

#endif
