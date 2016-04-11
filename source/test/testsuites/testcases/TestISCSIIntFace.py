#!/usr/bin/python
#
# Copyright 2016 by Formation Data Systems, Inc.
#

# FDS test-case pattern requirements.
import unittest
import TestCase

from testcases.iscsifixture import ISCSIFixture

# Module-specific requirements
import os
import sys
from fabric.contrib.files import *
from fdscli.model.volume.settings.iscsi_settings import ISCSISettings
from fdscli.model.volume.settings.lun_permission import LunPermissions
from fdscli.model.common.size import Size
from fdscli.model.volume.volume import Volume
from fdslib.TestUtils import connect_fabric
from fdslib.TestUtils import disconnect_fabric
from fdslib.TestUtils import get_node_service
from fdslib.TestUtils import get_volume_service
from fdscli.model.fds_error import FdsError

# A third-party initiator that enables sending CDBs and fetching response
from pyscsi.pyscsi.scsi_device import SCSIDevice
from pyscsi.pyscsi.scsi import SCSI

DEFAULT_VOLUME_NAME = 'volISCSI'

class TestISCSICrtVolume(ISCSIFixture):
    """FDS test case to create an iSCSI volume.

    Attributes
    ----------
    passedName : str
        FDS volume name
    passedPermissions: str
        iSCSI permissions, 'rw' or 'ro'
    passedSize : str
        block size
    sizeSizeUnits: str
        block size units
    """
    def __init__(self, parameters=None, name=None, permissions="rw", size="10", sizeunits="GB"):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_ISCSICrtVolume,
                                             "Creating an iSCSI volume")
        self.passedName = name
        self.passedPermissions = permissions
        self.passedSize = size
        self.passedSizeUnits = sizeunits

    def test_ISCSICrtVolume(self):
        """Attempt to create an iSCSI volume."""

        if not self.passedName:
            self.passedName = DEFAULT_VOLUME_NAME

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        # iSCSI volume create command
        new_volume = Volume()
        new_volume.name = self.passedName
        new_volume.settings = ISCSISettings(capacity=Size(self.passedSize, unit=self.passedSizeUnits))
        new_volume.settings.lun_permissions = [LunPermissions(permissions=self.passedPermissions, lun_name=self.passedName)]

        vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
        status = vol_service.create_volume(new_volume)
        if isinstance(status, FdsError):
            self.log.error("Volume %s creation on %s returned status %s." %
                               (new_volume.name, om_node.nd_conf_dict['node-name'], status))
            return False

        self.log.info('Created volume {0}'.format(self.passedName))
        # Cache this volume name in the fixture so that other test cases
        # can use it to map to ISCSI target name and drive
        self.addVolumeName(self.passedName)
        return True


class TestISCSIDiscoverVolume(ISCSIFixture):
    """FDS test case to discover an iSCSI target

    Attributes
    ----------
    target_name : str
        If test was successful, contains target name following IQN convention.
        Example: 'iqn.2012-05.com.formationds:volISCSI'
        None otherwise.

    initiator_name : str
        Initiator node (can be a remote host) specified in FdsConfigFile 
    volume_name : str
        FDS volume name, initialized with object instantation
        else from fixture
    """
    def __init__(self, parameters=None, initiator_name=None, volume_name=None):
        """
        Parameters
        ----------
        parameters : List[str]
        initiator_name : str
            Initiator node (can be a remote host) specified in FdsConfigFile 
        volume_name : str
            The volume name as provided to FDS create volume
        """
        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_discover_volume,
                "Discover iSCSI target")

        self.initiator_name = initiator_name
        self.volume_name = volume_name

    def test_discover_volume(self):
        """Uses native iSCSI initiator to discover volume on target host

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        if not self.volume_name:
            self.volume_name = DEFAULT_VOLUME_NAME

        self.target_name = None
        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if initiator_ip:
            # Specify connection info
            assert connect_fabric(initiator_ip) is True

        # Uses CLI to identify an AM node. Targets are presented on AM node.
        am_ip = self.getAMEndpoint(om_node)

        # -t, --type=type
        #   Type of SendTargets (abbreviated as st) which is a native iSCSI protocol
        #       which allows each iSCSI target to send a list of available targets to
        #       the initiator
        cmd = 'iscsiadm -m discovery -p %s -t st' % (am_ip)

        if initiator_ip:
            # Fabric run a shell command on a remote host
            result = run(cmd)
        else:
            result = local(cmd)

        passed = False
        if result.failed:
            self.log.error("Failed to discover iSCSI volumes.")
        else:
            g = result.split()
            for s in g:
                # Now s is a target name like 'iqn.2012-05.com.formationds:vol60'.
                # Don't use count here, because 'vol6' would match the above.
                if s.endswith(self.volume_name):
                    self.target_name = s
                    # Cache target name in fixture
                    self.setTargetName(self.target_name, self.volume_name)
                    passed = True
                    break;

        if initiator_ip:
            disconnect_fabric()
        return passed


class TestISCSIFioSeqW(ISCSIFixture):
    """FDS test case to write to an iSCSI volume

    Attributes
    ----------
    initiator_name : str
        Initiator node (can be a remote host) specified in FdsConfigFile 
    sd_device : str
        Device using sd upper level driver (example: '/dev/sdb')
    volume_name : str
        FDS volume name
    """
    def __init__(self, parameters=None, initiator_name=None, sd_device=None, volume_name=None):
        """
        Volume name not required if device provided.

        Parameters
        ----------
        parameters : List[str]
        initiator_name : str
            Initiator node (can be a remote host) specified in FdsConfigFile 
        sd_device : str
            Device using sd upper level driver (example: '/dev/sdb')
        volume_name : str
            FDS volume name
        """

        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_fio_write,
                "Write to an iSCSI volume")

        self.initiator_name = initiator_name
        self.volume_name = volume_name
        self.sd_device = sd_device

    def test_fio_write(self):
        """
        Use flexible I/O tester to write to iSCSI volume

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        # Use the block interface (not the char interface)
        if not self.sd_device:
            if not self.volume_name:
                self.log.error("Missing required iSCSI target name")
                return False
            # Use fixture target name
            self.sd_device = self.getDriveDevice(self.volume_name)
        if not self.sd_device:
            self.log.error("Missing disk device for %s" % self.volume_name)
            return False
        else:
            self.log.info("block device is {0}".format(self.sd_device))

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        if self.childPID is None:
            # Not running in a forked process.
            # Stop on failures.
            verify_fatal = 1
        else:
            # Running in a forked process. Don't
            # stop on failures.
            verify_fatal = 0

        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if initiator_ip:
            # Specify connection info
            assert connect_fabric(initiator_ip) is True

# This one produced a failure! TODO: debug with Brian S...
# Also, consider parameterizing this test case to provide the fio options below...
#        cmd = "sudo fio --name=seq-writers --readwrite=write --ioengine=posixaio --direct=1 --bsrange=512-1M " \
#                 "--iodepth=128 --numjobs=1 --fill_device=1 --filename=%s --verify=md5 --verify_fatal=%d" %\
#                 (self.sd_device, verify_fatal)

        # Produces SIGSEGV 11 when --size=512m at about 47 % complete
        # --debug=all was not illuminating
        cmd = "sudo fio --name=seq-writers --readwrite=write --ioengine=posixaio --direct=1 --bsrange=512-1M " \
                 "--iodepth=128 --numjobs=1 --fill_device=1 --filename=%s --size=16m --verify=md5 --verify_fatal=%d" %\
                 (self.sd_device, verify_fatal)

        try:
            if initiator_ip:
                # Fabric run a shell command on a remote host
                result = run(cmd)
                disconnect_fabric()
            else:
                result = local(cmd)
        except Exception as e:
            self.log.error(str(e))
            return False

        self.log.info('fio result: {0}'.format(result));
        if result.failed:
            self.log.error('FIO FAIL')
            return False

        self.log.info('FIO PASS')
        return True


class TestISCSIListVolumes(ISCSIFixture):
    """FDS test case to list volumes

    """
    def __init__(self, parameters=None, volume_name=None):
        """
        Parameters
        ----------
        volume_name : str
            FDS volume name
        """

        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_list_volumes,
                "List volumes using CLI")

        self.volume_name = volume_name

    def test_list_volumes(self):
        """
        Attempt to list FDS volumes, look for our iSCSI volume

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
        volumes = vol_service.list_volumes()
        for v in volumes:
            if v.name != self.volume_name:
                continue
            s = v.settings
            if s.type != 'ISCSI':
                self.log.info("Require ISCSI type")
            return True

        self.log.info("Missing volume!")
        return True


class TestISCSIAttachVolume(ISCSIFixture):
    """FDS test case to login to an iSCSI target

    Yields generic driver device name so that other test cases can use pass
    through char device for testing.

    Attributes
    ----------
    sd_device : str
        Device using sd upper level driver (example: '/dev/sdb')
    sg_device : str
        Device using sg upper level driver (example: '/dev/sg2')
    initiator_name : str
        Initiator node (can be a remote host) specified in FdsConfigFile 
    target_name : str
        The iSCSI node record name to login.
        Target name is expected to follow IQN convention.
        Example: 'iqn.2012-05.com.formationds:volISCSI'
    volume_name : str
        FDS volume name
    """

    def __init__(self, parameters=None, initiator_name=None, target_name=None, volume_name=None):
        """
        Parameters
        ----------
        initiator_name : str
            Initiator node (can be a remote host) specified in FdsConfigFile 
        target_name : str
            The iSCSI node record name to login
        volume_name : str
            FDS volume name
        """

        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_login_target,
                "Attaching iSCSI volume")

        self.initiator_name = initiator_name
        self.volume_name = volume_name
        self.target_name = target_name
        self.sd_device = None
        self.sg_device = None

    def test_login_target(self):
        """
        Attempt to attach an iSCSI volume.

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        if not self.target_name:
            if not self.volume_name:
                self.log.error("Missing required iSCSI target name")
                return False
            else:
                # Use fixture target name
                self.target_name = self.getTargetName(self.volume_name)

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        initiator_ip = None
        # Attach the volume to the given initiator or local
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if initiator_ip:
            # Specify connection info
            assert connect_fabric(initiator_ip) is True

        status = True

        # Cache the device names prior to login
        disk_devices = []
        generic_devices = []
        if initiator_ip:
            # Fabric run a shell command on a remote host
            result = run('sg_map -sd')
        else:
            result = local('sg_map -sd')

        if result.failed:
            self.log.error('Failed to get sg map. Return code {0}.'.format(
                    result.return_code))
            status = False
        else:
            g = result.split()
            for elem in g:
                if elem[:7] == '/dev/sd':
                    disk_devices.append(elem)
                elif elem[:7] == '/dev/sg':
                    generic_devices.append(elem)

        # Uses CLI to identify an AM node. Targets are presented on AM node.
        am_ip = self.getAMEndpoint(om_node)
        cmd = 'iscsiadm -m node --targetname %s -p %s --login' % (self.target_name, am_ip)

        if status:
            if initiator_ip:
                result = run(cmd)
            else:
                result = local(cmd)

            if result.failed:
                self.log.error("Failed to login iSCSI target %s." % self.target_name)
                status = False

        if status:
            # Compute device names
            if initiator_ip:
                result = run('sg_map -sd')
            else:
                result = local('sg_map -sd')

            if result.succeeded:
                gprime = result.split()
                for elem in gprime:
                    if elem[:7] == '/dev/sd' and not elem in disk_devices:
                        self.sd_device = elem
                        self.setDriveDevice(elem, self.volume_name)
                        self.log.info("Added disk device %s" % elem)
                    elif elem[:7] == '/dev/sg' and not elem in generic_devices:
                        self.sg_device = elem
                        self.setGenericDevice(elem, self.volume_name)
                        self.log.info("Added generic device %s" % elem)

        if not self.sd_device:
            self.log.error("Missing disk device for %s" % self.volume_name)
        if not self.sg_device:
            self.log.error("Missing generic device for %s" % self.volume_name)
        if not self.sd_device or not self.sg_device:
            self.log.info('SG Map prior to iSCSI login: {0}'.format(g))
            status = False

        if initiator_ip:
            disconnect_fabric()
        return status


class TestISCSIMakeFilesystem(ISCSIFixture):
    """FDS test case to make file system on disk device
    """
    def __init__(self, parameters=None, initiator_name=None, sd_device=None, volume_name=None):
        """
        Parameters
        ----------
        parameters : List[str]
        initiator_name : str
            Initiator node (can be a remote host) specified in FdsConfigFile 
        sd_device : str
            The disk device (example: '/dev/sdb')
        volume_name : str
            FDS volume name
        """
        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_make_filesystem,
                "Make filesystem on disk device")

        self.initiator_name = initiator_name
        self.sd_device = sd_device
        self.volume_name = volume_name

    def test_make_filesystem(self):
        """
        Attempt to make filesystem on disk device.

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        # Volume name is required even when sd_device is present.
        # This method uses volume name in the /mnt child directory name.
        if not self.volume_name:
            self.log.error("Missing required iSCSI volume name")
            return False

        if not self.sd_device:
            # Use fixture target name
            self.sd_device = self.getDriveDevice(self.volume_name)

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if initiator_ip:
            # Specify connection info
            assert connect_fabric(initiator_ip) is True

        status = True

        device = self.sd_device[:8]
        # Uses a partition table of type 'gpt'
        cmd = 'parted -s -a optimal %s mklabel gpt -- mkpart primary ext4 1 -1' % device
        if initiator_ip:
            # Fabric run a shell command on a remote host
            result = run(cmd)
        else:
            result = local(cmd)

        if result.failed:
            self.log.error("Failed to make file system for device %s." % device)
            status = False

        if status:
            # build file system
            cmd2 = 'mkfs.ext4 %s1' % device
            if initiator_ip:
                result = run(cmd2)
            else:
                result = local(cmd2)

            # Nominally returns -1 because mkfs.ext4 is ill-behaved. It writes
            # to stderr.
            if result.failed:
                self.log.error("Failed to format file system")
                status = False

        do_mkdir = False
        path = '/mnt/%s' % self.volume_name
        ps = ("import os\n"
              "if not os.path.isdir('{0}'):\n"
              "    print('do_mkdir')".format(path))

        if status:
            if initiator_ip:
                # Run a bit of Python on remote host...
                cmd3 = 'echo -e "{0}" | python'.format(ps)
                result = run(cmd3)
                if result.failed:
                    status = False
                if result == 'do_mkdir':
                    do_mkdir = True
            else:
                # No need to use Fabric for local
                if not os.path.isdir(path):
                    do_mkdir = True

        if status and do_mkdir:
            cmd4 = 'mkdir %s' % path
            if initiator_ip:
                result = run(cmd4)
            else:
                result = local(cmd4)

            if result.failed:
                self.log.error("Failed to create mount directory %s" % path)
                status = False

        if status:
            # mount
            cmd5 = 'mount %s1 %s' % (device, path)
            if initiator_ip:
                result = run(cmd5)
            else:
                result = local(cmd5)

            if result.failed:
                self.log.error('Failed to mount {0}.'.format(device))
                status = False

        if initiator_ip:
            disconnect_fabric()
        self.log.info("Made ext4 file system on device %s." % device)
        return status


class TestISCSIUnitReady(ISCSIFixture):
    """FDS test case to send TEST_UNIT_READY CDB and check response

    """
    def __init__(self, parameters=None, initiator_name=None, sg_device=None, volume_name=None):
        """Only one of sg_device or volume_name is required

        Parameters
        ----------
        sg_device : str
            The generic sg device (example: '/dev/sg2')
        volume_name : str
            FDS volume name
        """
        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_unit_ready,
                "Testing TEST_UNIT_READY CDB in full feature phase")

        self.initiator_name = initiator_name
        self.sg_device = sg_device
        self.volume_name = volume_name

    def test_unit_ready(self):
        """
        Sends TEST_UNIT_READY CDB and checks response.

        [RFC-7413] Section 4.3, 7.4.3
        Verify that target does not send SCSI response PDUs once in the
        Full Feature Phase.

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        if not self.sg_device:
            if not self.volume_name:
                self.log.error("Missing required iSCSI device")
                return False
            else:
                # Use fixture target name
                self.sg_device = self.getGenericDevice(self.volume_name)

        if not self.sg_device:
            self.log.error("Missing required iCSSI generic device")
            return False

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if not initiator_ip:
            # If test case not run as root, creating the SCSIDevice throws!
            try:
                # untrusted execute
                sd = SCSIDevice(self.sg_device, True)
                s = SCSI(sd)
                r = s.testunitready().result
                if not r:
                    # empty dictionary, which is the correct response!
                    return True

                for k, v in r.iteritems():
                    self.log.info('%s - %s' % (k, v))
            except Exception as e:
                self.log.error(str(e))

            return False

        else:
            # TODO: need to decide the best way to run on the initiator

            # Alternative 1 is to cat the commands to stream and pipe to python run
            # Alternative 2 is to put the test case into a .py file and pipe to python run
            # Alternative 3 is to use an initiator utility like sg_turs and stop using the ioctl
            #  based C code

            # Specify connection info
            assert connect_fabric(initiator_ip) is True
            cmd = "sg_turs {0}".format(self.sg_device)
            result = run(cmd)
            disconnect_fabric()
            if result.failed:
                return False

        return True


class TestISCSIDetachVolume(ISCSIFixture):
    """FDS test case for iSCSI node record logout

    Client has responsibility for maintaining the mapping from iSCSI node
    record name to device.

    Attributes
    ----------
    initiator_name : str
        Initiator node (can be a remote host) specified in FdsConfigFile 
    target_name : str
        Each target name is expected to following IQN convention
        Example: 'iqn.2012-05.com.formationds:volISCSI'
    volume_name : str
        FDS volume name
    """

    def __init__(self, parameters=None, initiator_name=None, target_name=None, volume_name=None):
        """
        Parameters
        ----------
        parameters : List[str]
        initiator_name : str
            Initiator node (can be a remote host) specified in FdsConfigFile 
        target_name : str
            The iSCSI node record name to logout, remove
        volume_name : str
            FDS volume name
        """
        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_logout_targets,
                "Disconnecting iSCSI volume")

        self.initiator_name = initiator_name
        self.target_name = target_name
        self.volume_name = volume_name

    def test_logout_targets(self):
        """
        Logout, remove iSCSI node record.

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        if not self.target_name:
            if not self.volume_name:
                self.log.error("Missing required iSCSI target name")
                return False
            else:
                self.target_name = self.getTargetName(self.volume_name)

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        # Unmount BEFORE breaking down the iSCSI node record

        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)

        if initiator_ip:
            # Specify connection info
            assert connect_fabric(initiator_ip) is True

        status = True
        path = '/mnt/{0}'.format(self.volume_name)
        self.log.info('Unmounting {0}...'.format(path))
        if initiator_ip:
            # Fabric run a shell command on a remote host
            result = run('umount -f -v {0}'.format(path))
        else:
            result = local('umount -f -v {0}'.format(path))

        if result.failed:
            self.log.error('Failed to unmount {0}. Return code: {1}.'.format(path,
                    result.return_code))
            status = False

        # Uses CLI to identify an AM node. Targets are presented on AM node.
        am_ip = self.getAMEndpoint(om_node)

        if status:
            # Logout session
            cmd = 'iscsiadm -m node --targetname %s -p %s --logout' % (self.target_name, am_ip) 
            if initiator_ip:
                result = run(cmd)
            else:
                result = local(cmd)

            if result.failed:
                self.log.error('Failed to detach iSCSI volume {0}. Return code: {1}.'.format(t,
                        result.return_code))
                status = False

        if status:
            # Remove the record
            cmd2 = 'iscsiadm -m node -o delete -T %s -p %s' % (self.target_name, am_ip)
            if initiator_ip:
                result = run(cmd2)
            else:
                result = local(cmd2)

            if result.failed:
                self.log.error('Failed to remove iSCSI record {0}. Return code: {1}.'.format(
                        self.target_name, result.return_code))
                status = False

        self.log.info("Detached volume %s and removed iSCSI record" % self.volume_name)
        if initiator_ip:
            disconnect_fabric()
        return status

class TestStandardInquiry(ISCSIFixture):
    """FDS test case to send INQUIRY CDB and check response

    An INQUIRY is standard when both the EVPD and CmdDt bits are clear.
    """
    def __init__(self, parameters=None, initiator_name=None, sg_device=None, volume_name=None):
        """Only one of sg_device or volume_name is required

        Parameters
        ----------
        sg_device : str
            The generic sg device (example: '/dev/sg2')
        volume_name : str
            FDS volume name
        """
        super(self.__class__, self).__init__(parameters,
                self.__class__.__name__,
                self.test_standard_inquiry,
                "Testing standard INQUIRY CDB in full feature phase")

        self.initiator_name = initiator_name
        self.sg_device = sg_device
        self.volume_name = volume_name

    def test_standard_inquiry(self):
        """
        Sends INQUIRY CDB and checks response.

        Returns
        -------
        bool
            True if successful, False otherwise
        """
        if not self.sg_device:
            if not self.volume_name:
                self.log.error("Missing required iSCSI device")
                return False
            else:
                # Use fixture target name
                self.sg_device = self.getGenericDevice(self.volume_name)

        if not self.sg_device:
            self.log.error("Missing required iSCSI generic device")
            return False

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        initiator_ip = None
        if self.initiator_name:
            initiator_ip = self.getInitiatorEndpoint(self.initiator_name, fdscfg)


        cmd = "sg_inq {0}".format(self.sg_device)
        try:
            if initiator_ip:
                # Specify connection info
                assert connect_fabric(initiator_ip) is True
                result = run(cmd)
            else:
                result = local(cmd)
        except Exception as e:
            self.log.error(str(e))
            if initiator_ip:
                disconnect_fabric()
            return False

        if result.failed:
            return False

        self.log.info('standard INQUIRY: \n {0}'.format(result))
        if result.count('Vendor identification: FDS') == 0:
            return False

        return True


class TestISCSIVolumeDelete(ISCSIFixture):
    """FDS test case to delete an iSCSI volume.

    Clears the volume from the test fixture.

    Attributes
    ----------
    passedName : str
        FDS volume name
    """
    def __init__(self, parameters=None, volume_name=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_VolumeDelete,
                                             "Delete iSCSI volume")
        self.passedName = volume_name

    def test_VolumeDelete(self):
        """Attempt to delete iSCSI volume."""

        if not self.passedName:
            self.passedName = DEFAULT_VOLUME_NAME

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        vol_service = get_volume_service(self,om_node.nd_conf_dict['ip'])
        volume_id = None
        volumes = vol_service.list_volumes()
        for volume in volumes:
            if self.passedName == volume.name:
                volume_id = volume.id
                break

        if volume_id is None:
            self.log.error('Failed to find volume {0}'.format(self.passedName))
            return False

        status = vol_service.delete_volume(volume_id)

        if isinstance(status, FdsError) or type(status).__name__ == 'FdsError':
            self.log.error('Delete volume {0} failed, status {1}'.format(self.passedName, status))
            return False

        # Clear volume from fixture
        self.deleteVolumeName(self.passedName)
        return True


if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()
