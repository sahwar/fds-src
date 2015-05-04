from fabric.api import *
from fabric.contrib.files import *
import random
import time
import logging
import shlex
import os
import pdb
from StringIO import StringIO

random.seed(time.time())
logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

class FdsFabricHelper():
    def __init__(self, fds_service, fds_node):
	env.user='root'
	env.password='passwd'
	env.host_string=fds_node
	self.node_service=fds_service
	self.fds_bin = '/fds/bin'
	self.fds_sbin = '/fds/sbin'
	self.fdsconsole = '{}/fdsconsole.py'.format(self.fds_sbin)


    def get_service_pid(self):
	'''
	This function takes ip address and FDS service and returns its PID

	ATTRIBUTES:
	----------
	service -       FDS service (e.g AMAgent, DataMgr, etc)
	ip_address -    node IP address

	Returns:
	--------
	Returns the PID of the service
	'''
	#env.user = 'hlim' 
	#env.password = 'Testlab' 
	#env.host_string = node_ip_address

	#with settings(hide('running','commands', 'stdout', 'stderr')):
	with settings(warn_only=True and hide('running','commands', 'stdout', 'stderr')):

		service_pid = run('pgrep {}'.format(self.node_service))
		if service_pid.return_code == 0:
			return service_pid

		else:
			log.warning("Unable to locate {} service PID".format(self.node_service))


    def get_platform_uuid(self, service_pid):
	'''
	This function takes FDS service pid and returns its platform uuid

	ATTRIBUTES:
	----------
	service_pid - FDS service pid 

	Returns:
	--------
	Returns platform uuid 
	'''

	if service_pid > 0:
		with settings(warn_only=True and hide('running','commands', 'stdout', 'stderr')):

			service_uuid = run('ps -www --no-headers {}'.format(service_pid))
			if service_uuid.return_code == 0:
				uuid = shlex.split(service_uuid)
				svc_uuid = uuid[8].split('=')
				return svc_uuid[1]

			else:
				log.warning("Unable to locate {} platform service uuid".format(self.node_service))

	else:
		log.warning("{} service is not running".format(self.node_service))

    def get_node_uuid(self, node_ip):
	'''
	This function queries and returns node_uuid

	ATTRIBUTES:
	----------
	Takes node_ip address

	Returns:
	--------
	Returns node uuid 
	'''

	if exists('{}'.format(self.fdsconsole)):
		with settings(warn_only=True and hide('running','commands', 'stdout', 'stderr')):
			with cd('{}'.format(self.fds_sbin)):
				sudo('./fdsconsole.py accesslevel admin')
				#run('./fdsconsole.py domain listServices local', stdout=self.sio)
				cmd_output = run('./fdsconsole.py domain listServices local')
				n_uuid = cmd_output.split()
				
				for i in range(len(n_uuid)): 
					print n_uuid[i]
					if n_uuid[i] == node_ip:
						if n_uuid[i+2] == 'pm':
							node_uuid = n_uuid[i-2]
							return node_uuid

	else:
		log.warning("Unable to locate fdsconsole.py")
	

