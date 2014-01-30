/*
 * Copyright 2013 Formation Data Systems, Inc.
 *
 * Template to write probe adapter.  Replace OM with your namespace.
 */
#include <orch-mgr-probe.h>
#include <list>
#include <string>
#include <iostream>
#include <orch-mgr/om-service.h>
#include <OmDataPlacement.h>

namespace fds {

probe_mod_param_t OM_probe_param =
{
    .pr_stat_cnt     = 0,
    .pr_inj_pts_cnt  = 0,
    .pr_inj_act_cnt  = 0,
    .pr_max_sec_tout = 0
};

OM_ProbeMod gl_OM_ProbeMod("OM Probe Adapter",
                           &OM_probe_param, &gl_OMModule);

// pr_new_instance
// ---------------
//
ProbeMod *
OM_ProbeMod::pr_new_instance()
{
    OM_ProbeMod *adapter = new OM_ProbeMod("OM Inst", &OM_probe_param, &gl_OMModule);

    adapter->mod_init(mod_params);
    adapter->mod_startup();
    return adapter;
}

// pr_intercept_request
// --------------------
//
void
OM_ProbeMod::pr_intercept_request(ProbeRequest *req)
{
}

// pr_put
// ------
//
void
OM_ProbeMod::pr_put(ProbeRequest *probe)
{
}

// pr_get
// ------
//
void
OM_ProbeMod::pr_get(ProbeRequest *req)
{
}

// pr_delete
// ---------
//
void
OM_ProbeMod::pr_delete(ProbeRequest *req)
{
}

// pr_verify_request
// -----------------
//
void
OM_ProbeMod::pr_verify_request(ProbeRequest *req)
{
}

// pr_gen_report
// -------------
//
void
OM_ProbeMod::pr_gen_report(std::string *out)
{
}

// mod_init
// --------
//
int
OM_ProbeMod::mod_init(SysParams const *const param)
{
    Module::mod_init(param);
    return 0;
}

// mod_startup
// -----------
//
void
OM_ProbeMod::mod_startup()
{
    JsObjManager  *mgr;
    JsObjTemplate *svc;

    if (pr_parent != NULL) {
        mgr = pr_parent->pr_get_obj_mgr();
        svc = mgr->js_get_template("Run-Setup");
        svc->js_register_template(new UT_OMSetupTemplate(mgr));
    }
}

// mod_shutdown
// ------------
//
void
OM_ProbeMod::mod_shutdown()
{
}

// -------------------------------------------------------------------------------------
// OM REST Test API
// -------------------------------------------------------------------------------------

JsObject *
UT_OM_NodeInfo::js_exec_obj(JsObject *parent, JsObjTemplate *templ, JsObjOutput *out)
{
    int              i, num;
    UT_OM_NodeInfo  *node;
    ut_node_info_t  *info;

    std::list<NodeAgent::pointer> newNodes;
    std::list<NodeAgent::pointer> rmNodes;

    num = parent->js_array_size();
    for (i = 0; i < num; i++) {
        node = static_cast<UT_OM_NodeInfo *>((*parent)[i]);
        info = node->om_node_info();
        std::cout << "Node uuid " << info->nd_uuid
            << ", name " << info->nd_node_name << std::endl;

        ResourceUUID r_uuid(info->nd_uuid);
        if (info->add == true) {
            newNodes.push_back(new NodeAgent(r_uuid));
        } else {
            rmNodes.push_back(new NodeAgent(r_uuid));
        }
    }

    // Update the cluster map
    ClusterMap *cm = static_cast<ClusterMap *>(gl_OMModule.om_clusmap_mod());
    cm->updateMap(newNodes, rmNodes);

    // Recompute the DLT
    DataPlacement *dp = static_cast<DataPlacement *>(gl_OMModule.om_dataplace_mod());
    dp->computeDlt();
    const FdsDlt *dlt = dp->getCurDlt();

    std::cout << "New DLT " << *dlt << std::endl;
    return this;  //  to free this obj
}

}  // namespace fds
