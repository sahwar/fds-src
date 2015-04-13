#!/usr/bin/python
#
# Copyright 2014 by Formation Data Systems, Inc.
#

# FDS test-case pattern requirements.
import unittest
import traceback
import TestCase

# Module-specific requirements
import sys
import os
import subprocess
import time

nbd_device = "/dev/nbd15"
pwd = ""

# This class contains the attributes and methods to test
# the FDS interface to create a block volume.
#
class TestBlockCrtVolume(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockCrtVolume,
                                             "Creating an block volume")

    def test_BlockCrtVolume(self):
        """
        Test Case:
        Attempt to create a block volume.
        """

        # TODO(Andrew): We should call OM's REST endpoint with an auth
        # token.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node
        global pwd

        # Block volume create command
        # TODO(Andrew): Don't hard code volume name
        cmd = "volume create  volume1 --vol-type block --blk-dev-size 104857600"
        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py {} > ./fdsconsole.out 2>&1) \"'.format(cmd),
                                            fds_tools=True)
        if status != 0:
            self.log.error("Failed to create block volume")
            return False
        time.sleep(5)

        cmd = "volume modify volume1 --minimum 0 --maximum 10000 --priority 1"
        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py {} > ./fdsconsole.out 2>&1) \"'.format(cmd),
                                            fds_tools=True)
        if status != 0:
            self.log.error("Failed to modify block volume")
            return False

        time.sleep(5)

        return True

# This class contains the attributes and methods to test
# the FDS interface to attach a volume.
#
class TestBlockAttachVolume(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockAttVolume,
                                             "Attaching a block volume")

        self.passedVolume = volume

    def test_BlockAttVolume(self):
        """
        Test Case:
        Attempt to attach a block volume client.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        volume = 'volume1'

	#Per FS-1368, make use of --lockfile /tmp/nbdadm/nbdadm.lock
	nbdadm_tmp = '/tmp/nbdadm'
	nbdadm_lockfile = 'nbdadm.lock'

	os.makedirs('%s' % nbdadm_tmp)

        # Check if a volume was passed to us.
        if self.passedVolume is not None:
            volume = self.passedVolume

        cinder_dir = os.path.join(fdscfg.rt_env.get_fds_source(), 'cinder')
        #blkAttCmd = '%s/nbdadm.py attach %s %s' % (cinder_dir, om_node.nd_conf_dict['ip'], volume)
        blkAttCmd = '%s/nbdadm.py --lockfile %s/%s attach %s %s' % (cinder_dir, nbdadm_tmp, nbdadm_lockfile, om_node.nd_conf_dict['ip'], volume)

        # Parameter return_stdin is set to return stdout. ... Don't ask me!
        status, stdout = om_node.nd_agent.exec_wait(blkAttCmd, return_stdin=True)

        if status != 0:
            self.log.error("Failed to attach block volume %s: %s." % (volume, stdout))
            return False
        else:
            global nbd_device
            nbd_device = stdout.rstrip()

        time.sleep(5)
        self.log.info("Attached block device %s" % (nbd_device))

        return True

# This class contains the attributes and methods to test
# the FDS interface to disconnect a volume.
#
class TestBlockDetachVolume(TestCase.FDSTestCase):
    def __init__(self, parameters=None, volume=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockDetachVolume,
                                             "Disconnecting a block volume")

        self.passedVolume = volume

    def test_BlockDetachVolume(self):
        """
        Test Case:
        Attempt to detach a block volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        volume = 'blockVolume'

        # Check if a volume was passed to us.
        if self.passedVolume is not None:
            volume = self.passedVolume

        cinder_dir = os.path.join(fdscfg.rt_env.get_fds_source(), 'cinder')

        # Command to detach a block volume
        blkDetachCmd = '%s/nbdadm.py detach %s' % (cinder_dir, volume)

        status = om_node.nd_agent.exec_wait(blkDetachCmd)

        if status != 0:
            self.log.error("Failed to detach block volume with status %s." % status)
            return False

        time.sleep(5)

        return True

# This class contains the attributes and methods to test
# writing block data.
#
class TestBlockFioSeqW(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockFioWrite,
                                             "Writing a block volume")


    def test_BlockFioWrite(self):
        """
        Test Case:
        Attempt to write to a block volume.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        if self.childPID is None:
            # Not running in a forked process.
            # Stop on failures.
            verify_fatal = 1
        else:
            # Running in a forked process. Don't
            # stop on failures.
            verify_fatal = 0

        # TODO(Andrew): Don't hard code all of this stuff...
        fioCmd = "sudo fio --name=seq-writers --readwrite=write --ioengine=libaio --direct=1 --bsrange=512-128k " \
                 "--iodepth=128 --numjobs=1 --fill_device=1 --filename=%s --verify=md5 --verify_fatal=%d" %\
                 (nbd_device, verify_fatal)

        status = om_node.nd_agent.exec_wait(fioCmd)

        if status != 0:
            self.log.error("Failed to run write workload with status %s." % status)
            return False

        return True

# This class contains the attributes and methods to test
# writing block data randomly
#
class TestBlockFioRandW(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockFioRandW,
                                             "Randomly writing a block volume")

    def test_BlockFioRandW(self):
        """
        Test Case:
        Attempt to write to a block volume.
        """

        # TODO(Andrew): Don't hard code all of this stuff...
        fioCmd = "sudo fio --name=rand-writers --readwrite=randwrite --ioengine=libaio --direct=1 --bsrange=512-128k --iodepth=128 --numjobs=1 --fill_device=1 --filename=%s --verify=md5 --verify_fatal=1" % (nbd_device)
        result = subprocess.call(fioCmd, shell=True)
        if result != 0:
            self.log.error("Failed to run write workload")
            return False
        time.sleep(5)

        return True

# This class contains the attributes and methods to test
# reading/writing block data
#
class TestBlockFioRW(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockFioReadWrite,
                                             "Reading/writing a block volume")

    def test_BlockFioReadWrite(self):
        """
        Test Case:
        Attempt to read/write to a block volume.
        """

        # TODO(Andrew): Don't hard code all of this stuff...
        fioCmd = "sudo fio --name=rw --readwrite=readwrite --ioengine=libaio --direct=1 --bsrange=512-128k --iodepth=128 --numjobs=4 --size=50M --filename=%s" % (nbd_device)
        result = subprocess.call(fioCmd, shell=True)
        if result != 0:
            self.log.error("Failed to run read/write workload")
            return False
        time.sleep(5)

        return True

# This class contains the attributes and methods to test
# reading/writing random block data
#
class TestBlockFioRandRW(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_BlockFioReadWrite,
                                             "Reading/writing a block volume")

    def test_BlockFioReadWrite(self):
        """
        Test Case:
        Attempt to random read/write to a block volume.
        """

        # TODO(Andrew): Don't hard code all of this stuff...
        fioCmd = "sudo fio --name=rand-rw --readwrite=randrw --ioengine=libaio --direct=1 --bs=512-128k --iodepth=128 --numjobs=1 --size=50M --filename=%s" % (nbd_device)
        result = subprocess.call(fioCmd, shell=True)
        if result != 0:
            self.log.error("Failed to run random read/write workload")
            return False
        time.sleep(5)

        return True

if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()
