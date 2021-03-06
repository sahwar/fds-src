#!/usr/bin/python
#
# Copyright 2015 by Formation Data Systems, Inc.
# author: Philippe Ribeiro
# email: philippe@formationds.com
# file: test_100gb_volume.py

import lib
import config
import unittest
import requests
import json
import sys
import time
import subprocess
import shutil

import ssh
import os
import lib

nbd_path = os.path.abspath(os.path.join('..', ''))
sys.path.append(nbd_path)

from boto.s3.key import Key

import testsets.testcase as testcase
# import the testlib lib
import testlib.lib.nbd as nbd
import lib
import file_generator
import block_volumes
import fio_template

class TestBareAMParted(testcase.FDSTestCase):
    '''
    Create one block volume with 100GB capacity, and populate first with 100GB;
    After that, add more 2GB of data, to ensure correctness.
    '''
    def __init__(self, parameters=None, config_file=None, om_ip_address=None):
        super(TestBareAMParted, self).__init__(parameters=parameters,
                                               config_file=config_file,
                                               om_ip_address=om_ip_address)

        # create 1 file of 1GB for the tests.
        self.fgen = file_generator.FileGenerator(1, 1, 'G')
        self.ndblib = nbd.NBD(self.om_ip_address)
        self.block_volume = block_volumes.BlockVolumes(om_ip_address)
        self.volumes = []
        self.devices = {}

    def runTest(self):
        self.volumes = self.block_volume.create_volumes(2, "test_bare_am", 10,
                                                        "GB")
        test_passed = False
        table = {}
        assert len(self.volumes) == 2
        volume_a, volume_b = self.volumes[0], self.volumes[1]
        # first volume will be tested for IOs
        test_passed = self.test_io_cmd(volume_a)
        self.reportTestCaseResult(test_passed)
    
    def test_parted_cmd(self, volume, device):

        path = config.FDS_MOUNT % volume
        template = fio_template.template % path
        current_fio_file = "fio_%s" % volume
        with open(current_fio_file, 'w') as f:
            f.write(template)
            f.close()

        if not os.path.exists(path):
            # clean up the existing mount point
            os.makedirs(path)
        # mount the volume
        device = self.ndblib.attach(volume)
        self.ndblib.mount_device(device, path)
        self.ndblib.format_dev(device, None)
        # generate the file to be copied to
        self.fgen.generate_files()
        # copy the file to the mount point
        files = self.fgen.get_files()
        for f in files:
            current = os.path.join(config.RANDOM_DATA, f)
            shutil.copy(current, path)
        
        cmd = "parted -s -a optimal %s mklabel gpt - mkpart primary ext4 1 -1" % device
        # perform FIO operations
        subprocess.call([cmd], shell=True)
        # remove the file
        if os.path.exists(current_fio_file):
            os.remove(current_fio_file)
            
        # remove the files which were created
        self.fgen.clean_up()
        
        # unmount the device
        self.ndblib.unmount(path)
        # detach the ndb device
        if self.ndblib.detach(volume):
            test_passed = True
        else:
            test_passed = False
            self.reportTestCaseResult(test_passed)
        shutil.rmtree(path)
        return test_passed
        
    def test_io_cmd(self, volume):
        path = config.FDS_MOUNT % volume
        template = fio_template.template % path
        current_fio_file = "fio_%s" % volume
        with open(current_fio_file, 'w') as f:
            f.write(template)
            f.close()

        if not os.path.exists(path):
            # clean up the existing mount point
            os.makedirs(path)
        self.log.info(volume)
        # mount the volume
        self.ndblib.mount_volume(volume, path, True)
        # generate the file to be copied to
        self.fgen.generate_files()
        # copy the file to the mount point
        files = self.fgen.get_files()
        for f in files:
            current = os.path.join(config.RANDOM_DATA, f)
            shutil.copy(current, path)
        
        cmd = "fio %s" % current_fio_file
        # perform FIO operations
        subprocess.call([cmd], shell=True)
        # remove the file
        if os.path.exists(current_fio_file):
            os.remove(current_fio_file)
        if os.path.exists(path):
            shutil.rmtree(path)
            
        # remove the files which were created
        self.fgen.clean_up()
        
        # unmount the device
        self.ndblib.unmount(path)
        # detach the ndb device
        if self.ndblib.detach(volume):
            test_passed = True
        else:
            test_passed = False
            self.reportTestCaseResult(test_passed)
        return test_passed