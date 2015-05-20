/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_PLATFORM_NODE_UPGRADE_H_
#define SOURCE_INCLUDE_PLATFORM_NODE_UPGRADE_H_

#include "node_down.h"

namespace fds
{
    class NodeUpgrade : public StateEntry
    {
        public:
            static inline int st_index()
            {
                return 3;
            }
            virtual char const *const st_name() const override
            {
                return "NodeUpgrade";
            }

            NodeUpgrade() : StateEntry(st_index(), NodeDown::st_index())
            {
            }
            virtual int st_handle(EventObj::pointer evt, StateObj::pointer cur) const override;
    };
}  // namespace fds

#endif  // SOURCE_INCLUDE_PLATFORM_NODE_UPGRADE_H_
