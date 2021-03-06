/*
 * Copyright 2013 by Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_PLATFORM_DOMAIN_CONTAINER_H_
#define SOURCE_INCLUDE_PLATFORM_DOMAIN_CONTAINER_H_

#include <string>

#include "am_container.h"
#include "dm_container.h"
#include "om_container.h"
#include "pm_container.h"
#include "sm_container.h"
#include "om_agent.h"

namespace fds
{
    // -------------------------------------------------------------------------------------
    // Common Domain Container
    // -------------------------------------------------------------------------------------
    class DomainContainer
    {
        public:
            typedef boost::intrusive_ptr<DomainContainer> pointer;
            typedef boost::intrusive_ptr<const DomainContainer> const_ptr;

            virtual ~DomainContainer();
            explicit DomainContainer(char const *const name);

            DomainContainer(char const *const name, OmAgent::pointer master,
                            AgentContainer::pointer sm, AgentContainer::pointer dm,
                            AgentContainer::pointer am, AgentContainer::pointer pm,
                            AgentContainer::pointer om);
            /**
             * Register and unregister an agent to the domain.
             * TODO(Vy): retire this function.
             */
            virtual Error dc_register_node(const NodeUuid &uuid, const FdspNodeRegPtr msg,
                                           NodeAgent::pointer *agent);

            virtual void dc_register_node(const ShmObjRO *shm, NodeAgent::pointer *agent, int ro,
                                          int rw, fds_uint32_t mask = 0);

            virtual Error dc_unregister_node(const NodeUuid &uuid, const std::string &name);
            virtual Error dc_unregister_agent(const NodeUuid &uuid, FdspNodeType type);

            /**
             * Return the agent container matching with the node type.
             */
            AgentContainer::pointer dc_container_frm_msg(FdspNodeType node_type);

            /**
             * Get service info in all nodes connecting to this domain.
             */
            void dc_node_svc_info(fpi::DomainNodes &ret);

            /**
             * Find the node agent matching with the given uuid.
             */
            NodeAgent::pointer dc_find_node_agent(const NodeUuid &uuid);

            /**
             * Domain iteration plugin
             */
            inline void dc_foreach_am(ResourceIter *iter)
            {
                dc_am_nodes->rs_foreach(iter);
            }

            inline void dc_foreach_sm(ResourceIter *iter)
            {
                dc_sm_nodes->rs_foreach(iter);
            }

            inline void dc_foreach_dm(ResourceIter *iter)
            {
                dc_dm_nodes->rs_foreach(iter);
            }

            inline void dc_foreach_pm(ResourceIter *iter)
            {
                dc_pm_nodes->rs_foreach(iter);
            }

            inline void dc_foreach_om(ResourceIter *iter)
            {
                dc_om_nodes->rs_foreach(iter);
            }

            /**
             * Get methods for different containers.
             */
            inline SmContainer::pointer dc_get_sm_nodes()
            {
                return agt_cast_ptr<SmContainer>(dc_sm_nodes);
            }

            inline DmContainer::pointer dc_get_dm_nodes()
            {
                return agt_cast_ptr<DmContainer>(dc_dm_nodes);
            }

            inline AmContainer::pointer dc_get_am_nodes()
            {
                return agt_cast_ptr<AmContainer>(dc_am_nodes);
            }

            inline PmContainer::pointer dc_get_pm_nodes()
            {
                return agt_cast_ptr<PmContainer>(dc_pm_nodes);
            }

            inline OmContainer::pointer dc_get_om_nodes()
            {
                return agt_cast_ptr<OmContainer>(dc_om_nodes);
            }

        protected:
            OmAgent::pointer           dc_om_master;
            AgentContainer::pointer    dc_om_nodes;
            AgentContainer::pointer    dc_sm_nodes;
            AgentContainer::pointer    dc_dm_nodes;
            AgentContainer::pointer    dc_am_nodes;
            AgentContainer::pointer    dc_pm_nodes;

        private:
            INTRUSIVE_PTR_DEFS(DomainContainer, rs_refcnt);
    };
}  // namespace fds

#endif  // SOURCE_INCLUDE_PLATFORM_DOMAIN_CONTAINER_H_
