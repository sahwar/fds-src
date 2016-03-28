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
from fdscli.services.fds_auth import *
from fdslib.TestUtils import get_volume_service
from fdslib.TestUtils import get_snapshot_policy_service

from fdscli.model.fds_error import FdsError
from fdscli.model.volume.snapshot_policy import SnapshotPolicy
from fdscli.model.volume.settings.object_settings import ObjectSettings
from fdscli.model.volume.settings.block_settings import BlockSettings
from fdscli.model.volume.settings.nfs_settings import NfsSettings
from fdscli.model.common.size import Size
from fdscli.model.volume.volume import Volume
from fdscli.model.volume.qos_policy import QosPolicy


import re

# This class contains the attributes and methods to test
# volume creation.
class TestVolumeCreate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None, snapshot_policy=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeCreate,
                                             "Volume creation")

        self.passedVolume = volume
        self.snapshot_policy = snapshot_policy

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
            if 'id' not in volume.nd_conf_dict:
                raise Exception('Volume section %s must have "id" keyword.' % volume.nd_conf_dict['vol-name'])

            self.log.info("Create volume %s on OM node %s." %
                          (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name']))

            vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
            status = vol_service.create_volume(self.set_vol_parameters(volume))

            if self.snapshot_policy is not None:
                snapshot_policy_id = self.snapshot_policy
                snapshot_policy_service = get_snapshot_policy_service(self, om_node.nd_conf_dict['ip'])
                default_snap_policies = vol_service.get_data_protection_presets(preset_id=snapshot_policy_id)[0]._DataProtectionPolicyPreset__snapshot_policies
                for each_snap_policy in default_snap_policies:
                    op = snapshot_policy_service.create_snapshot_policy(status.id, each_snap_policy)
                self.log.info("Volume {0} created with preset snapshot policy id {1}".format(status.name, self.snapshot_policy))

            if isinstance(status, FdsError) or type(status).__name__ == 'FdsError':
                pattern = re.compile("500: The specified volume name (.*) already exists.")
                if re.match(pattern, status.message):
                    return True

                self.log.error("Volume %s creation on %s returned status %s." %
                               (volume.nd_conf_dict['vol-name'], om_node.nd_conf_dict['node-name'], status))
                return False
            elif self.passedVolume is not None:
                break

        return True

    def set_vol_parameters(self, vol_obj):
        new_volume = Volume()
        new_volume.name = vol_obj.nd_conf_dict['vol-name']
        new_volume.id = vol_obj.nd_conf_dict['id']

        if 'media' not in vol_obj.nd_conf_dict:
            media = 'hdd'
        else:
            media = vol_obj.nd_conf_dict['media']
        new_volume.media_policy =media.upper()

        if 'access' not in vol_obj.nd_conf_dict or vol_obj.nd_conf_dict['access'] == 'object':
            # set default volume settings to ObjectSettings
            access = ObjectSettings()
        else:
            # if its a block then set the size in BlockSettings
            access = vol_obj.nd_conf_dict['access']
            if access == 'block':
                if 'size' not in vol_obj.nd_conf_dict:
                    raise Exception('Volume section %s must have "size" keyword.' % vol_obj.nd_conf_dict['vol-name'])
                access = BlockSettings()
                access.capacity = Size(size=vol_obj.nd_conf_dict['size'], unit = 'B')
            elif access == 'nfs':
                if 'size' not in vol_obj.nd_conf_dict:
                    raise Exception('Volume section %s must have "size" keyword.' % vol_obj.nd_conf_dict['vol-name'])
                access = NfsSettings()
                access.max_object_size = Size(size=vol_obj.nd_conf_dict['size'], unit = 'B')

        new_volume.settings = access
        if 'policy' in vol_obj.nd_conf_dict:
            # Set QOS policy which is defined in volume definition.
            new_volume.qos_policy = self.get_qos_policy(vol_obj.nd_conf_dict['policy'])

        return new_volume

    def get_qos_policy(self, policy_id):
        fdscfg = self.parameters["fdscfg"]
        qos_policy = QosPolicy()
        policies = fdscfg.rt_get_obj('cfg_vol_pol')
        for policy in policies:
            if 'id' not in policy.nd_conf_dict:
                print('Policy section must have an id')
                sys.exit(1)

            if policy.nd_conf_dict['id'] == policy_id:
                if 'iops_min' in policy.nd_conf_dict:
                    qos_policy.iops_min = policy.nd_conf_dict['iops_min']

                if 'iops_max' in policy.nd_conf_dict:
                    qos_policy.iops_max = policy.nd_conf_dict['iops_max']

                if 'priority' in policy.nd_conf_dict:
                    qos_policy.priority = policy.nd_conf_dict['priority']

        return qos_policy

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
            if (status != 0) != self.expect_to_fail:
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

            if check_localhost(ip):
                cinder_dir = os.path.join(fdscfg.rt_env.get_fds_source(), 'cinder')
            else:
                cinder_dir= os.path.join('/fds/sbin')

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
        Attempt to delete a volume.
        """
        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
        volumes = vol_service.list_volumes()
        for volume in volumes:
        # We arent comapring with ID because volume id is not the same as give in cfg vol definition, new REST API assigns new vol_id
            if self.passedVolume.nd_conf_dict['vol-name'] == volume.name:
                volume_id = volume.id
                break
            else:
                continue

        if volume_id is None:
            self.log.error("Could not find volume %s"%(self.passedVolume.nd_conf_dict['vol-name']))
            return False

        self.log.info("Delete volume %s on OM node %s." %
                          (volume.name, om_node.nd_conf_dict['node-name']))
        vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
        status = vol_service.delete_volume(volume_id)

        if isinstance(status, FdsError) or type(status).__name__ == 'FdsError':
            self.log.error("Delete volume %s on %s returned status as %s." %
                               (volume.name, om_node.nd_conf_dict['node-name'], status))
            return False

        return True


if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()
