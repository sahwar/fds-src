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
import sys

from fds.services.node_service import NodeService
from fds.services.fds_auth import FdsAuth
from fds.services.users_service import UsersService
from fds.model.node_state import NodeState
from fds.model.service import Service 
from fds.model.domain import Domain

sys.path.insert(0, '../../scale-framework')
import config

env.user=config.SSH_USER
env.password=config.SSH_PASSWORD

random.seed(time.time())
logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

class SMService(object):
    def __init__(self):
        self.fds_log = '/tmp'
        fdsauth = FdsAuth()
        fdsauth.login()
        self.nservice = NodeService(fdsauth)
        self.node_list = self.nservice.list_nodes()
        self.node_state = NodeState()
        om_ip = config.FDS_DEFAULT_HOST

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
        log.info(SMService.start.__name__)
        nodeNewState = NodeState()
        nodeNewState.am=True

        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                self.nservice.start_service(node.id, node.services['SM'][0].id)
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
        log.info(SMService.stop.__name__)
        nodeNewState = NodeState()
        nodeNewState.am=False

        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                self.nservice.stop_service(node.id, node.services['SM'][0].id)

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
        log.info(SMService.kill.__name__)
        log.info('Killing StorMgr service')
        env.host_string = node_ip
        sudo('pkill -9 StorMgr')

        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                if node.services['SM'][0].status !=  'ACTIVE':
		        log.warn('Failed to kill StorMgr service on node {}'.format(node.ip_v4_address))
		        return False 

		else:
			log.info('killed StorMgr service on node {}'.format(node.ip_v4_address))
		        return True

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
        log.info(SMService.add.__name__)
        log.info('Adding SM service')
	fdsauth2 = FdsAuth()
	fdsauth2.login()
        newNodeService = Service()
	newNodeService.auto_name="SM"
	newNodeService.type="FDSP_STOR_MGR"
	newNodeService.status="ACTIVE"

        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                self.nservice.add_service(node.id, newNodeService)

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
        log.info(SMService.remove.__name__)
        log.info('Removing SM service')
        nodeNewState = NodeState()
        nodeNewState.am=False

        for node in self.node_list:
            if node.ip_v4_address == node_ip:
                self.nservice.remove_service(node.id, node.services['SM'][0].id)
                if node.services['SM'][0].status ==  'INACTIVE':
			         log.info('Removed SM service from node {}'.format(node.ip_v4_address))
			         return True

                else:
			         log.warn('Failed to remove SM service from node {}'.format(node.ip_v4_address))
			         return False 
