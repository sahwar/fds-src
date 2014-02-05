/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <orch-mgr/om-service.h>
#include <OmDeploy.h>
#include <OmDataPlacement.h>

namespace fds {

OM_Module::OM_Module(char const *const name)
    : Module(name)
{
    om_data_place = new DataPlacement(PlacementAlgorithm::
                                      AlgorithmTypes::ConsistHash,  // Use RR
                                      6,  // width of 6 = 64 tokens
                                      4);  // depth of 4 = 4 replicas
    /*
     * TODO: Let's use member variables rather than globals.
     * Members are a better OO-design.
     */
    static Module *om_mods[] = {
        &gl_OMNodeDomainMod,
        &gl_OMClusMapMod,
        om_data_place,
        &gl_OMDltMod,
        NULL
    };
    mod_intern     = om_mods;
    om_dlt         = &gl_OMDltMod;
    om_clus_map    = &gl_OMClusMapMod;
    om_node_domain = &gl_OMNodeDomainMod;
}

OM_Module::~OM_Module() {
    delete om_data_place;
}

int
OM_Module::mod_init(SysParams const *const param)
{
    Module::mod_init(param);

    return 0;
}

void
OM_Module::mod_startup()
{
}

void
OM_Module::mod_shutdown()
{
}

}  // namespace fds
