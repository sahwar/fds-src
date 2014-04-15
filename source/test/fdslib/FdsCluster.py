#!/usr/bin/env python

# Copyright 2014 by Formation Data Systems, Inc.
#
import sys
sys.path.append('/home/nbayyana/fds-src/source/test/fdslib/pyfdsp')
import socket
from FDS_ProtocolInterface.ttypes import *
import struct

import FdsSetup as inst
import BringUpCfg as fdscfg
from FdsNode import FdsNode
from FdsService import SMService
from FdsService import DMService
from FdsService import AMService
from FdsService import OMService


class NodeNotFoundException(Exception):
    pass


class CheckerFailedException(Exception):
    pass


class FdsCluster:

    def __init__(self, config_file):
        """
        Takes FdsConfig object
        """
        self.config = fdscfg.FdsConfigFile(config_file, verbose=True)
        self.config.config_parse()
        self.env = inst.FdsEnv('.')

        self.nodes = {}
        
        # Find the node with om set and run om
        self.__om_node_id = None
        self.__om_svc = None
        self.__run_om()

    def start_ams(self):
        """
        Stars AMs.
        TODO(Rao): This isn't needed.  Running AM should be specified in the config
        """
        for am in self.config.cfg_am:
            am.am_start_service()
    
    def add_node(self, node_id, activate_services = []):
        """
        Adds node.
        node_id: Id from the config
        """
        # Find node config
        node_cfg = self.__get_node_config(node_id)

        # Add the node
        if self.nodes.has_key(node_id):
            raise Exception('Node {} already exits'.format(node_id))
        self.nodes[node_id] = FdsNode(node_cfg)

        # On OM node we don't need initiate ssh connection.
        # It's already done in the constructor
        if node_cfg.nd_conf_dict['node-name'] != self.__om_node_id:
            node_cfg.nd_connect_rmt_agent(self.env)
            node_cfg.nd_rmt_agent.ssh_setup_env('')
            
        # Run platform daemon
        node_cfg.nd_start_platform()

        # activate services
        for service_id in activate_services:
            # NOTE: Passing node_cfg as service config
            self.add_service(node_id, service_id, node_cfg)

        # TODO(Rao): If AM is specified activate at the end
            
    def remove_node(self, node_id):
        cli = self.config.config_cli()
        cli.run_cli('--activate-nodes abc -k 1 -e {}'.format(service_id))


    def get_service(self, node_id, service_id):
        """
        Returns service identified by service_id
        """
        return self.nodes[node_id].services[service_id]

    def add_service(self, node_id, service_id, node_cfg):
        """
        Adds service identified by service_id
        """
        if self.nodes.has_key(node_id) is False:
            raise Exception("Node {} isn't added yet".format(node_id))

        # Activate service
        cli = self.config.cfg_cli
        cli.run_cli('--activate-nodes abc -k 1 -e {}'.format(service_id))

        # Query OM to get list of services.  From this list we will pick out
        # service we are interested in
        svc_info = self.__get_service_info(service_id, node_cfg)

        # TODO(Rao): This part should should move into FdsNode class
        if service_id == 'sm':
            self.nodes[node_id].add_service(service_id, SMService(svc_info))
        elif service_id == 'dm':
            self.nodes[node_id].add_service(service_id, DMService(svc_info))
        elif service_id == 'am':
            self.nodes[node_id].add_service(service_id, AMService(svc_info))
        elif service_id == 'om':
            self.nodes[node_id].add_service(service_id, OMService(svc_info))
        else:
            raise Exception("Uknown service: {}".format(service_id))



    def run_checker(self):
        """
        Runs checker process against the cluster
        TODO(Rao): Implement this
        """
        pass

    def run_dirbased_checker(self, dir = None):
        """
        Runs directory based checker on the cluster
        """
        cmd = 'checker'
        if dir:
            cmd = 'checker --fds.checker.path={}'.format(dir)
        status = self.__get_node_config(self.__om_node_id).nd_rmt_agent.ssh_exec_fds(
            cmd, wait_compl=True)
        if status is not 0:
            raise CheckerFailedException()

    def __get_service_info(self, service_id, node_cfg):
        """
        Returns the service info.
        TODO(Rao): Get the service info from OM, when OM supports getting serivce info by
        Node name.  For now, we are doing the same math that platform does to generate
        port numbers
        """
        svc_info = FDSP_Node_Info_Type()
        svc_info.ip_lo_addr = struct.unpack("!I", socket.inet_aton(node_cfg.nd_conf_dict['ip']))[0]
        plat_port = int(node_cfg.nd_conf_dict['fds_port'])

        if service_id is 'sm':
            base_port = plat_port + 10;
        elif service_id is 'dm':
            base_port = plat_port + 20
        elif service_id is 'am':
            base_port = plat_port + 30
        elif service_id is 'om':
            base_port = 8903
        svc_info.control_port = base_port
        svc_info.data_port = base_port + 2
        svc_info.migration_port = base_port + 3
        return svc_info

    def __get_node_config(self, node_id):
        """
        returns node config identified by node_id
        """
        for n in self.config.cfg_nodes:
            if n.nd_conf_dict['node-name'] == node_id:
                return n
        raise NodeNotFoundException(str(node_id))

    def __run_om(self):
        """
        Parses the node cofig and finds the node with OM and runs OM
        """
        for n in self.config.cfg_nodes:
            if n.nd_run_om():
                n.nd_connect_rmt_agent(self.env)
                n.nd_rmt_agent.ssh_setup_env('')
                n.nd_start_om()
                # cache om node id
                self.__om_node_id = n.nd_conf_dict['node-name']
                om_info = self.__get_service_info('om', n)
                self.__om_svc = OMService(om_info)
                return 
        raise NodeNotFoundException("OM node not found")

