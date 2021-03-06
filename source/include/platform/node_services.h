/*
 * Copyright 2013 by Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_PLATFORM_NODE_SERVICES_H_
#define SOURCE_INCLUDE_PLATFORM_NODE_SERVICES_H_

#include <vector>
#include <string>
#include <ostream>
#include <serialize.h>
#include <fds_resource.h>

namespace fds
{
    struct NodeServices : serialize::Serializable
    {
        ResourceUUID sm, dm, am;

        inline void reset()
        {
            sm.uuid_set_val(0);
            dm.uuid_set_val(0);
            am.uuid_set_val(0);
        }

        virtual uint32_t write(serialize::Serializer *s) const;
        virtual uint32_t read(serialize::Deserializer *s);

        friend std::ostream &operator << (std::ostream &os, const NodeServices& node);
    };
}  // namespace fds

#endif  // SOURCE_INCLUDE_PLATFORM_NODE_SERVICES_H_
