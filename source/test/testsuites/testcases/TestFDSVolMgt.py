#!/usr/bin/python
#
# Copyright 2015 by Formation Data Systems, Inc.
#
# Test cases associated with volume management.
#

# FDS test-case pattern requirements.
import unittest
import traceback
import TestCase
from fdslib.TestUtils import findNodeFromInv
from fdslib.TestUtils import check_localhost
# Module-specific requirements
import sys
import os
import socket


# This class contains the attributes and methods to test
# volume creation.
class TestVolumeCreate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeCreate,
                                             "Volume creation")

        self.passedVolume = volume

    def test_VolumeCreate(self):
        """
        Test Case:
        Attempt to create a volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # Currently, all volumes are created using our one well-known OM.
        om_node = fdscfg.rt_om_node
        log_dir = fdscfg.rt_env.get_log_dir()

        volumes = fdscfg.rt_get_obj('cfg_volumes')
        for volume in volumes:
            # If we were passed a volume, create that one and exit.
            if self.passedVolume is not None:
                volume = self.passedVolume

            cmd = (' volume create %s' % volume.nd_conf_dict['vol-name'])

            if 'id' not in volume.nd_conf_dict:
                raise Exception('Volume section %s must have "id" keyword.' % volume.nd_conf_dict['vol-name'])
            cmd = cmd + (' --tenant-id %s' % volume.nd_conf_dict['id'])

            if 'access' not in volume.nd_conf_dict:
                access = 'object'
            else:
                access = volume.nd_conf_dict['access']

            # Size only makes sense for block volumes
            if 'block' == access:
                if 'size' not in volume.nd_conf_dict:
                    raise Exception('Volume section %s must have "size" keyword.' % volume.nd_conf_dict['vol-name'])
                cmd = cmd + (' --blk-dev-size %s' % volume.nd_conf_dict['size'])

            cmd = cmd + (' --vol-type %s' % access)
            if 'media' not in volume.nd_conf_dict:
                media = 'hdd'
            else:
                media = volume.nd_conf_dict['media']

            cmd = cmd + (' --media-policy %s' % media)

            self.log.info("Create volume %s on OM node %s." %
                          (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py %s > %s/cli.out 2>&1 &) \"' %
                                                (cmd, log_dir if om_node.nd_agent.env_install else "."),
                                                fds_tools=True)

            if status != 0:
                self.log.error("Volume %s creation on %s returned status %d." %
                               (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name'], status))
                return False
            elif self.passedVolume is not None:
                break

        return True


# This class contains the attributes and methods to test
# volume attachment.
class TestVolumeAttach(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None, node=None, expect_to_fail=False):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeAttach,
                                             "Attach volume")

        self.passedVolume = volume
        self.passedNode = node
        self.expect_to_fail = expect_to_fail

    def test_VolumeAttach(self):
        """
        Test Case:
        Attempt to attach a volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # Currently, all volumes are attached using our one well-known OM
        # in case a node hosting an AM is not passed as an argument.
        om_node = fdscfg.rt_om_node
        log_dir = fdscfg.rt_env.get_log_dir()

        volumes = fdscfg.rt_get_obj('cfg_volumes')
        for volume in volumes:
            # If we were passed a volume, attach that one and exit.
            if self.passedVolume is not None:
                volume = self.passedVolume

            # If a node is passed, attach volume to AM on this node.
            if self.passedNode is not None:
                am_node = self.passedNode
            else:
                am_node = volume.nd_am_node.nd_conf_dict['node-name']

            volName = volume.nd_conf_dict['vol-name']

            # Object volumes currently attach implicitly
            if 'block' != volume.nd_conf_dict['access']:
                break

            nodes = fdscfg.rt_obj.cfg_nodes
            node = findNodeFromInv(nodes, am_node)

            ip = node.nd_conf_dict['ip']
            offset = 3809
            port = int(node.nd_conf_dict['fds_port']) + offset
            self.log.info("Attach volume %s on node %s." % (volName, am_node))
            cmd = ('attach %s:%s %s' % (ip, port, volName))
            if check_localhost(ip):
                cinder_dir = os.path.join(fdscfg.rt_env.get_fds_source(), 'cinder')
            else:
                cinder_dir= os.path.join('/fds/sbin')
            status, stdout = om_node.nd_agent.exec_wait('bash -c \"(nohup %s/nbdadm.py  %s) \"' %
                                                        (cinder_dir, cmd), return_stdin=True)
            if (status != 0) or self.expect_to_fail:
                self.log.error("Attach volume %s on %s returned status %d." %
                               (volName, am_node, status))
                return False

            #stdout has the nbd device to which the volume was attached
            volume.nd_conf_dict['nbd-dev'] = stdout
            self.log.info("volume = %s and it's nbd device = %s" %
                          (volume.nd_conf_dict['vol-name'], volume.nd_conf_dict['nbd-dev']))

            if self.passedVolume is not None:
                break

        return True

# This class contains the attributes and methods to test
# volume detachment.
class TestVolumeDetach(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None, node=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeDetach,
                                             "Detach volume")

        self.passedVolume = volume
        self.passedNode = node

    def test_VolumeDetach(self):
        """
        Test Case:
        Attempt to detach a volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # Currently, all volumes are assumed to be attached to one well-known
        # OM in case a node hosting an AM is not passed as an argument.
        om_node = fdscfg.rt_om_node
        log_dir = fdscfg.rt_env.get_log_dir()

        volumes = fdscfg.rt_get_obj('cfg_volumes')
        for volume in volumes:
            # If we were passed a volume, detach that one and exit.
            if self.passedVolume is not None:
                volume = self.passedVolume

            if self.passedNode is not None:
                am_node = self.passedNode
            else:
                am_node = volume.nd_am_node.nd_conf_dict['node-name']

            volName = volume.nd_conf_dict['vol-name']

            # Object volumes currently detach implicitly
            if 'block' != volume.nd_conf_dict['access']:
                break

            nodes = fdscfg.rt_obj.cfg_nodes
            for node in nodes:
                if am_node == node.nd_conf_dict['node-name']:
                    break;
            ip = node.nd_conf_dict['ip']
            if not check_localhost(ip):
                log_dir = '/tmp'

            offset = 3809
            port = int(node.nd_conf_dict['fds_port']) + offset
            self.log.info("Detach volume %s on node %s." % (volName, am_node))
            cmd = ('detach %s' % (volName))


            cinder_dir = os.path.join(fdscfg.rt_env.get_fds_source(), 'source/cinder')
            status = om_node.nd_agent.exec_wait('bash -c \"(nohup %s/nbdadm.py  %s >> %s/nbdadm.out 2>&1 &) \"' %
                                                (cinder_dir, cmd, log_dir if om_node.nd_agent.env_install else "."),
                                                fds_tools=True)

            if status != 0:
                self.log.error("Detach volume %s on %s returned status %d." %
                               (volName, am_node, status))
                return False
            elif self.passedVolume is not None:
                break

        return True

# This class contains the attributes and methods to test
# volume delete.
class TestVolumeDelete(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeDelete,
                                             "Delete volume")

        self.passedVolume = volume

    def test_VolumeDelete(self):
        """
        Test Case:
        Attempt to attach a volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # Currently, all volumes are attached using our one well-known OM.
        om_node = fdscfg.rt_om_node
        log_dir = fdscfg.rt_env.get_log_dir()

        volumes = fdscfg.rt_get_obj('cfg_volumes')
        for volume in volumes:
            # If we were passed a volume, attach that one and exit.
            if self.passedVolume is not None:
                volume = self.passedVolume

            cmd = (' volume delete %s' %
               (volume.nd_conf_dict['vol-name']))

            self.log.info("Delete volume %s on OM node %s." %
                          (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py %s > %s/cli.out 2>&1) \"' %
                                                (cmd, log_dir if om_node.nd_agent.env_install else "."),
                                                fds_tools=True)

            if status != 0:
                self.log.error("Delete volume %s on %s returned status %d." %
                               (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name'], status))
                return False
            elif self.passedVolume is not None:
                break

        return True


if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()