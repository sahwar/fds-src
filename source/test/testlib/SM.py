from __future__ import with_statement
from fabric import tasks
from fabric.api import *
from fabric.contrib.console import confirm
from fabric.contrib.files import *
from fabric.network import disconnect_all
from fabric.tasks import execute

import string
import pdb
from StringIO import StringIO
import re
import fabric_helper
import logging
import random
import time
import string

from fds.services.node_service import NodeService
from fds.services.fds_auth import FdsAuth
from fds.services.users_service import UsersService
from fds.model.node_state import NodeState
from fds.model.domain import Domain

#Need to get this from config.py file
env.user='root'
env.password='passwd'

random.seed(time.time())
logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

class sm_service(object):
    def __init__(self):
	#TODO: get this from config.py
        self.fds_log = '/tmp'
        fdsauth = FdsAuth()
        fdsauth.login()
        self.nservice = NodeService(fdsauth)
        self.node_list = self.nservice.list_nodes()
        self.node_state = NodeState()
	    #TODO:  Get OM IP address from ./fdscli.conf/config.py file
        om_ip = "127.0.0.1"
        for node in self.node_list:
            if node.ip_v4_address == om_ip:
                env.host_string = node.ip_v4_address

    def start(self, node_ip):
        '''
        Start SM service

	    Attributes:
	    -----------
	    node_ip:  str
		The IP address of the node to start SM service.

	    Returns:
	    -----------
	    Boolean
        '''
        log.info(sm_service.start.__name__)
        nodeNewState = NodeState()
        nodeNewState.sm=True

        for node in self.node_list:
	        #need to comment out the line below when done
            #log.info('Node = {}, Status = {}'.format(node.ip_v4_address, node.services['SM'][0].status))
            if node.ip_v4_address == node_ip:
                self.nservice.activate_node(node.id, nodeNewState)
                if node.services['SM'][0].status ==  'ACTIVE':
			         log.info('SM service has started on node {}'.format(node.ip_v4_address))
			         return True

                else:
			         log.warn('SM service has NOT started on node {}'.format(node.ip_v4_address))
			         return False 


    def stop(self, node_ip):
        '''
        Stop SM service

    	Attributes:
    	-----------
    	node_ip:  str
		The IP address of the node to stop SM service.

    	Returns:
    	-----------
    	Boolean

        '''
	
        log.info(sm_service.stop.__name__)
        nodeNewState = NodeState()
        nodeNewState.sm=False

        for node in self.node_list:
	    #need to comment out the line below when done
            #log.info('Node = {}, Status = {}'.format(node.ip_v4_address, node.services['SM'][0].status))
            if node.ip_v4_address == node_ip:
                self.nservice.deactivate_node(node.id, nodeNewState)

                if node.services['SM'][0].status !=  'ACTIVE':
			         log.info('SM service is no longer running on node {}'.format(node.ip_v4_address))
			         return True

                else:
			         log.warn('SM service is STILL running on node {}'.format(node.ip_v4_address))
			         return False


    def kill(self, node_ip):
        '''
        Kill SM service

    	Attributes:
    	-----------
    	node_ip:  str
		The IP address of the node to kill SM service.

    	Returns:
    	-----------
    	Boolean
        '''
        log.info(sm_service.kill.__name__)
        log.info('Killing StorMgr service')
        env.host_string = node_ip
        sudo('pkill -9 StorMgr > {}/cli.out 2>&1'.format(self.fds_log))

        for node in self.node_list:
            #need to comment out the line below when done
            #log.info('Node = {}, Status = {}'.format(node.ip_v4_address, node.services['SM'][0].status))
            if node.ip_v4_address == node_ip:
                if node.services['SM'][0].status !=  'ACTIVE':
			         log.info('killed StorMgr service on node {}'.format(node.ip_v4_address))
			         return True

                else:
			         log.warn('Failed to kill StorMgr service on node {}'.format(node.ip_v4_address))
			         return False 

    def add(self, node_ip):
        '''
        Add SM service

        Attributes:
        -----------
        node_ip:  str
                The IP address of the node to add SM service.

        Returns:
        -----------
        Boolean
        '''
        log.info(am_service.add.__name__)
        log.info('Adding SM service')
        env.host_string = node_ip

        #TODO:  add code to add SM service here
        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                if node.services['SM'][0].status ==  'ACTIVE':
                                 log.info('Added SM service to node {}'.format(node.ip_v4_address))
                                 return True

                else:
                                 log.warn('Failed to add SM service to node {}'.format(node.ip_v4_address))
                                 return False




    def remove(self, node_ip):
        '''
        Remove SM service

        Attributes:
        -----------
        node_ip:  str
                The IP address of the node to remove SM service.

        Returns:
        -----------
        Boolean
        '''
        log.info(am_service.add.__name__)
        log.info('Removing SM service')
        env.host_string = node_ip

        #TODO:  add code to remove SM service here
        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                if node.services['SM'][0].status ==  'ACTIVE':
                                 log.info('Removed SM service from node {}'.format(node.ip_v4_address))
                                 return True

                else:
                                 log.warn('Failed to remove SM service from node {}'.format(node.ip_v4_address))
                                 return False

