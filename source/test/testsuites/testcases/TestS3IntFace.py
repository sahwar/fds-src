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
import uuid
import os
import math
import hashlib
import httplib
from filechunkio import FileChunkIO
import boto
from boto.s3 import connection
from boto.s3.key import Key
import filecmp
import random
import types
import string
import re

class Helper:
    @staticmethod
    def tobytes(num):
        if type(num) == types.IntType:
            return num

        if type(num) == types.StringType:
            if num.isdigit(): return int(num)
            m = re.search(r'[A-Za-z]', num)
            if m.start() <=0:
                return num

            n = int(num[0:m.start()])
            c = num[m.start()].upper()
            if c == 'K': return n*1024;
            if c == 'M': return n*1024*1024;
            if c == 'G': return n*1024*1024*1024;

    @staticmethod
    def boolean(value):
        if type(value) == types.StringType:
            value = value.lower()
        return value in ['true', '1', 'yes', 1, 'ok', 'set', True]

    @staticmethod
    def genData(length=10, seed=None):
        r = random.Random(seed)
        return ''.join([r.choice(string.ascii_lowercase) for i in range(0, Helper.tobytes(length))])

    @staticmethod
    def bucketName(seed=None):
        return 'volume-{}'.format( Helper.genData(10, seed))

    @staticmethod
    def keyName(seed=None):
        return 'key-{}'.format( Helper.genData(12, seed))


# Class to contain S3 objects used by these test cases.
class S3(object):
    def __init__(self, conn):
        self.conn = conn
        self.bucket1 = None
        self.keys = []  # As we add keys to the bucket, add them here as well.
        self.verifiers = {} # As we track objects to verify, add object name
                            # and hash here


# This class contains the attributes and methods to test
# the FDS S3 interface to create a connection.
#
# You must have successfully retrieved an authorization token from
# OM and stored it in fdscfg.rt_om_node.auth_token. See TestOMIntFace.TestGetAuthToken.
class TestS3GetConn(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3GetConn,
                                             "Getting an S3 connection")

    def test_S3GetConn(self):
        """
        Test Case:
        Attempt to get an S3 connection.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        if not hasattr(om_node, "auth_token"):
            self.log.error("No authorization token from OM. This is needed for an S3 connection.")
            return False
        else:
            self.log.info("Get an S3 connection on %s." %
                               om_node.nd_conf_dict['node-name'])

            s3conn = boto.connect_s3(aws_access_key_id='admin',
                                            aws_secret_access_key=om_node.auth_token,
                                            host=om_node.nd_conf_dict['ip'],
                                            port=8443,
                                            calling_format=boto.s3.connection.OrdinaryCallingFormat())

            if not s3conn:
                self.log.error("boto.connect_s3() on %s did not return an S3 connection." %
                               om_node.nd_conf_dict['node-name'])
                return False

            # This method may be called multiple times in the even the connection
            # is closed and needs to be re-opened. But we only want to instantiate the
            # S3 object once.
            if not "s3" in self.parameters:
                self.parameters["s3"] = S3(s3conn)
            else:
                self.parameters["s3"].conn = s3conn

        return True


# This class contains the attributes and methods to test
# the FDS S3 interface to close a connection.
#
# You must have successfully created a connection.
class TestS3CloseConn(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3CloseConn,
                                             "Closing an S3 connection")

    def test_S3CloseConn(self):
        """
        Test Case:
        Attempt to close an S3 connection.
        """

        if not "s3" in self.parameters:
            self.log.error("No S3 interface object so no connection to close.")
            return False
        elif self.parameters["s3"].conn is None:
            self.log.error("No S3 connection to close.")
            return False
        else:
            self.log.info("Close an S3 connection.")
            s3 = self.parameters["s3"]
            try:
                s3.conn.close()
            except Exception as e:
                self.log.error("Could not close S3 connection.")
                self.log.error(e.message)
                return False

        return True


# This class contains the attributes and methods to test
# the FDS S3 interface to create a bucket.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn. See TestS3IntFace.TestS3GetConn.
class TestS3CrtBucket(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3CrtBucket,
                                             "Creating an S3 bucket")

    def test_S3CrtBucket(self):
        """
        Test Case:
        Attempt to create an S3 Bucket.
        """

        if not "s3" in self.parameters:
            self.log.error("No S3 interface object with which to create a bucket.")
            return False
        elif self.parameters["s3"].conn is None:
            self.log.error("No S3 connection with which to create a bucket.")
            return False
        else:
            self.log.info("Create an S3 bucket.")
            s3 = self.parameters["s3"]

            s3.bucket1 = s3.conn.create_bucket('bucket1')

            if not s3.bucket1:
                self.log.error("s3.conn.create_bucket() failed to create bucket bucket1.")
                return False

        return True


class TestS3Acls(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3Acls,
                                             "Testing ACLs with Boto")

    def test_S3Acls(self):
        """
        Test Case:
        Attempt to set ACL parameters with Boto.
        """

        if not "s3" in self.parameters:
            self.log.error("No S3 interface object with which to create a bucket.")
            return False
        elif self.parameters["s3"].conn is None:
            self.log.error("No S3 connection with which to create a bucket.")
            return False
        else:
            s3 = self.parameters["s3"]
            bucket_name = str(uuid.uuid4())
            random_bucket = s3.conn.create_bucket(bucket_name)
            key_name = str(uuid.uuid4())
            conn = httplib.HTTPConnection("localhost:8000")

            # Try a PUT without setting the ACL first, should be rejected
            conn.request("PUT", "/" + bucket_name + "/" + key_name, "file contents")
            result = conn.getresponse()
            self.assertEqual(403, result.status)

            # Anonymous PUT should be accepted after ACL has been set
            random_bucket.set_acl('public-read-write')
            conn = httplib.HTTPConnection("localhost:8000")
            conn.request("PUT", "/" + bucket_name + "/" + key_name, "file contents")
            result = conn.getresponse()
            self.assertEqual(200, result.status)


        return True


# This class contains the attributes and methods to test
# the FDS S3 interface to upload a zero-length BLOB.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadZBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadZBLOB,
                                             "Upload a zero-length BLOB into an S3 bucket")

    def test_S3LoadZBLOB(self):
        """
        Test Case:
        Attempt to load a zero-length BLOB into an S3 Bucket.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Load a zero-length BLOB into an S3 bucket.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = "/dev/null"
            source_size = os.stat(source_path).st_size

            # Get a Key/Value object for th bucket.
            k = Key(s3.bucket1)

            # Set the key.
            s3.keys.append("zero")
            k.key = "zero"

            self.log.info("Loading %s of size %d using Boto's Key.set_contents_from_string() interface." %
                          (source_path, source_size))

            # Set the value, write it to the bucket, and, while the file containing the value is still
            # open, read it back to verify.
            with open(source_path, 'r')  as f:
                f.seek(0)
                k.set_contents_from_string(f.read())

                f.seek(0)
                if k.get_contents_as_string() == (f.read()):
                    return True
                else:
                    self.log.error("File mis-match.")
                    return False


# This class contains the attributes and methods to test
# the FDS S3 interface to upload a small BLOB.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadSBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadSBLOB,
                                             "Upload a 'small' (<= 2MiB) BLOB into an S3 bucket")

    def test_S3LoadSBLOB(self):
        """
        Test Case:
        Attempt to load a 'small' BLOB (<= 2MiB) into an S3 Bucket.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Load a 'small' BLOB (<= 2Mib) into an S3 bucket.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = bin_dir + "/orchMgr"
            source_size = os.stat(source_path).st_size

            # Get a Key/Value object for th bucket.
            k = Key(s3.bucket1)

            # Set the key.
            s3.keys.append("small")
            k.key = "small"

            self.log.info("Loading %s of size %d using Boto's Key.set_contents_from_string() interface." %
                          (source_path, source_size))

            # Set the value, write it to the bucket, and, while the file containing the value is still
            # open, read it back to verify.
            with open(source_path, 'r')  as f:
                self.log.debug("Read from file %s:" % source_path)

                f.seek(0)
                self.log.debug(f.read())

                f.seek(0)
                k.set_contents_from_string(f.read())

                self.log.debug("Read from FDS %s:" % source_path)
                self.log.debug(k.get_contents_as_string())

                f.seek(0)
                if k.get_contents_as_string() == (f.read()):
                    return True
                else:
                    self.log.error("File mis-match.")
                    return False


# This class contains the attributes and methods to test
# the FDS S3 interface to upload a "largish" BLOB in one piece.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadFBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadFBLOB,
                                             "Upload a 'largish' (<= 2MiB) BLOB into an S3 bucket in one piece")

    def test_S3LoadFBLOB(self):
        """
        Test Case:
        Attempt to load a 'largish' BLOB (<= 2MiB) into an S3 Bucket in one piece.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Load a 'largish' BLOB (<= 2Mib) into an S3 bucket in one piece.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = bin_dir + "/disk_type.py"
            source_size = os.stat(source_path).st_size

            # Get a Key/Value object for the bucket.
            k = Key(s3.bucket1)

            # Set the key.
            s3.keys.append("largish")
            k.key = "largish"

            self.log.info("Loading %s of size %d using Boto's Key.set_contents_from_filename() interface." %
                          (source_path, source_size))

            # Set the value and write it to the bucket.
            k.set_contents_from_filename(source_path)

            # Read it back to a file and then compare.
            dest_path = bin_dir + "/disk_type.py.boto"
            k.get_contents_to_filename(dest_path)

            test_passed = filecmp.cmp(source_path, dest_path, shallow=False)
            if not test_passed:
                self.log.error("File mis-match")

            os.remove(dest_path)

            return test_passed


# This class contains the attributes and methods to test
# the FDS S3 interface to upload a "largish" BLOB in one piece with meta-data.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadMBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadMBLOB,
                                             "Upload a 'largish' (<= 2MiB) BLOB into an S3 bucket in one piece with meta-data")

        self.passedBucket = bucket

    def test_S3LoadMBLOB(self):
        """
        Test Case:
        Attempt to load a 'largish' BLOB (<= 2MiB) into an S3 Bucket in one piece with meta-data.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False

        # Check if a bucket was passed to us.
        if self.passedBucket is not None:
            self.parameters["s3"].bucket1 = self.parameters["s3"].conn.lookup(self.passedBucket)
            if self.parameters["s3"].bucket1 is None:
                self.log.error("Cannot find passed bucket named %s." % self.passedBucket)
                return False

        if not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Load a 'largish' BLOB (<= 2Mib) into an S3 bucket in one piece with meta-data.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = bin_dir + "/disk_type.py"
            source_size = os.stat(source_path).st_size

            # Get a Key/Value object for the bucket.
            k = Key(s3.bucket1)

            # Set the key.
            s3.keys.append("largishWITHmetadata")
            k.key = "largishWITHmetadata"

            self.log.info("Loading %s of size %d using Boto's Key.set_contents_from_filename() interface "
                          "and setting meta-data." %
                          (source_path, source_size))

            # Set the value and write it to the bucket.
            k.set_metadata('meta1', 'This is the first metadata value')
            k.set_metadata('meta2', 'This is the second metadata value')
            k.set_contents_from_filename(source_path)

            # Read it back to a file and then compare.
            dest_path = bin_dir + "/disk_type.py.boto"
            k.get_contents_to_filename(dest_path)

            # Check the file.
            test_passed = filecmp.cmp(source_path, dest_path, shallow=False)

            # If the file looked OK, check the meta-data.
            if test_passed:
                meta = k.get_metadata('meta1')
                if meta != 'This is the first metadata value':
                    self.log.error("Meta-data 1 is incorrect: %s" % meta)
                    test_passed = False

                meta = k.get_metadata('meta2')
                if meta != 'This is the second metadata value':
                    self.log.error("Meta-data 2 is incorrect: %s" % meta)
                    test_passed = False
            else:
                self.log.error("File mis-match")

            os.remove(dest_path)

            return test_passed


# This class contains the attributes and methods to test
# that the largish BLOB with meta-data is in tact.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
#
# You must also have sucessfully executed test case TestS3LoadMBLOB,
class TestS3VerifyMBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3VerifyMBLOB,
                                             "Verify the 'largish' (<= 2MiB) BLOB with meta-data")

    def test_S3VerifyMBLOB(self):
        """
        Test Case:
        Attempt to verify the 'largish' BLOB (<= 2MiB) with meta-data.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Verify the 'largish' BLOB (<= 2Mib) with meta-data.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = bin_dir + "/disk_type.py"
            source_size = os.stat(source_path).st_size

            # Get a Key/Value object for the bucket.
            k = Key(s3.bucket1)

            # Set the key.
            s3.keys.append("largishWITHmetadata")
            k.key = "largishWITHmetadata"

            # Read the BLOB to a file and then compare.
            dest_path = bin_dir + "/disk_type.py.boto"
            k.get_contents_to_filename(dest_path)

            # Check the file.
            test_passed = filecmp.cmp(source_path, dest_path, shallow=False)

            # If the file looked OK, check the meta-data.
            if test_passed:
                meta = k.get_metadata('meta1')
                if meta != 'This is the first metadata value':
                    self.log.error("Meta-data 1 is incorrect: %s" % meta)
                    test_passed = False

                meta = k.get_metadata('meta2')
                if meta != 'This is the second metadata value':
                    self.log.error("Meta-data 2 is incorrect: %s" % meta)
                    test_passed = False
            else:
                self.log.error("File mis-match")

            os.remove(dest_path)

            return test_passed


# This class contains the attributes and methods to test
# the FDS S3 interface to upload a large BLOB.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadLBLOB(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadLBLOB,
                                             "Upload a 'large' (> 2MiB) BLOB into an S3 bucket")

        self.passedBucket=bucket

    def test_S3LoadLBLOBCb(self, so_far, total):
        self.log.info(str(so_far) + "B Downloaded");

    def test_S3LoadLBLOB(self):
        """
        Test Case:
        Attempt to load a 'large BLOB (> 2MiB) into an S3 Bucket.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False

        # Check if a bucket was passed to us.
        if self.passedBucket is not None:
            self.parameters["s3"].bucket1 = self.parameters["s3"].conn.lookup(self.passedBucket)
            if self.parameters["s3"].bucket1 is None:
                self.log.error("Cannot find passed bucket named %s." % self.passedBucket)
                return False

        if not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Load a 'large' BLOB (> 2MiB) into an S3 bucket.")
            s3 = self.parameters["s3"]

            # Get file info
            source_path = bin_dir + "/StorMgr"
            source_size = os.stat(source_path).st_size

            # Create a multipart upload request
            s3.keys.append("large")
            mp = s3.bucket1.initiate_multipart_upload("large")

            # Use a chunk size of 50 MiB (feel free to change this)
            chunk_size = 52428800
            chunk_count = int(math.ceil(float(source_size) / chunk_size))
            md5sum = str(hashlib.md5(open(source_path, 'rb').read()).hexdigest())

            self.log.info("Loading %s of size %d [md5: %s] using %d chunks of max size %d. using "
                          "Boto's 'multi-part' upload interface" %
                          (source_path, source_size, md5sum, chunk_count, chunk_size))

            # Send the file parts, using FileChunkIO to create a file-like object
            # that points to a certain byte range within the original file. We
            # set bytes to never exceed the original file size.
            for i in range(chunk_count):
                self.log.info("Sending chunk %d." % (i + 1))
                offset = chunk_size * i
                bytes = min(chunk_size, source_size - offset)
                with FileChunkIO(source_path, 'r', offset=offset,
                         bytes=bytes) as fp:
                    mp.upload_part_from_file(fp, part_num=i + 1)

            # Finish the upload
            completed_upload = mp.complete_upload()
            if not md5sum != completed_upload.etag:
                self.log.info("Etag does not match md5 sum of file: [ " + md5sum + " ] != [ " + completed_upload.etag + "]")
                return False
            else:
                self.log.info("Etag matched: [" + completed_upload.etag + "]")

            # Read it back to a file and then compare.
            dest_path = bin_dir + "/StorMgr.boto"

            k = Key(s3.bucket1)
            k.key = 'large'

            k.get_contents_to_filename(filename=dest_path, cb=self.test_S3LoadLBLOBCb)

            test_passed = filecmp.cmp(source_path, dest_path, shallow=False)
            if not test_passed:
                md5sum = str(hashlib.md5(open(source_path, 'rb').read()).hexdigest())
                self.log.error("File mis-match. [" + md5sum + "]")

            os.remove(dest_path)

            return test_passed


# This class contains the attributes and methods to test
# the FDS S3 interface to put an objected with verifiable content
# in place for later retrieval and validation
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3LoadVerifiableObject(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3LoadVerifiableObject,
                                             "Load a verifiable object into S3 bucket")

        self.passedBucket=bucket
        self.verifiableObjectSha=None

    def test_S3LoadVerifiableObject(self):
        """
        Test Case:
        Attempt to load an object with verifiable contents into a bucket
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load an object.")
            return False

        # Check if a bucket was passed to us.
        if self.passedBucket is not None:
            self.parameters["s3"].bucket1 = self.parameters["s3"].conn.lookup(self.passedBucket)
            if self.parameters["s3"].bucket1 is None:
                self.log.error("Cannot find passed bucket named %s." % self.passedBucket)
                return False

        if not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load an object.")
            return False
        else:
            self.log.info("Load an object with verifiable contents into an S3 bucket.")
            bucket = self.parameters["s3"].bucket1

            verifiable_file_contents = "a" * 1024
            verifiable_object = bucket.new_key('s3VerifiableObject')
            verifiable_object.set_contents_from_string(verifiable_file_contents)

            # Grab the object and capture the hash for verification
            test_passed = False
            try:
                obj = bucket.get_key('s3VerifiableObject')
            except Exception as e:
                self.log.error("Could not get object just put.")
                self.log.error(e.message)
            else:
                if obj:
                    stored_hash = hashlib.sha1(obj.get_contents_as_string()).hexdigest()
                    self.log.info("Hash of object stored in FDS: %s" % stored_hash)
                    self.parameters["s3"].verifiers['s3VerifiableObject'] = stored_hash
                    test_passed = True

            return test_passed

# This class contains the attributes and methods to test
# the FDS S3 interface to verify an object with verifiable content
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3CheckVerifiableObject(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3CheckVerifiableObject,
                                             "Check verifiable object in S3 bucket")

        self.passedBucket=bucket

    def test_S3CheckVerifiableObject(self):
        """
        Test Case:
        Attempt to get an object with verifiable contents and validate the
        checksum matches what we have stored
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to get an object.")
            return False

        # Check if a bucket was passed to us.
        if self.passedBucket is not None:
            self.parameters["s3"].bucket1 = self.parameters["s3"].conn.lookup(self.passedBucket)
            if self.parameters["s3"].bucket1 is None:
                self.log.error("Cannot find passed bucket named %s." % self.passedBucket)
                return False

        if not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to get an object.")
            return False
        else:
            self.log.info("Verify contents of verifiable object in an S3 bucket.")
            bucket = self.parameters["s3"].bucket1

            test_passed = False
            try:
                verifiable_object = bucket.get_key('s3VerifiableObject')
            except Exception as e:
                self.log.error("Could not get object to be verified.")
                self.log.error(e.message)
            else:
                verify_hash = hashlib.sha1(verifiable_object.get_contents_as_string()).hexdigest()
                self.log.info("Hash of object read from FDS: %s" % verify_hash)
                stored_verify_hash = self.parameters['s3'].verifiers['s3VerifiableObject']
                self.log.info("Hash of object stored from LoadVerifiableObject: %s" % stored_verify_hash)
                if stored_verify_hash == verify_hash:
                    test_passed = True
                else:
                    self.log.error("S3 Verifiable Object hash did not match")

            return test_passed

# This class contains the attributes and methods to test
# the FDS S3 interface to list the keys of a bucket.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3ListBucketKeys(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3ListBucketKeys,
                                             "Listing the keys of an S3 bucket")

    def test_S3ListBucketKeys(self):
        """
        Test Case:
        Attempt to list the keys of an S3 Bucket.
        """

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("List the keys of an S3 bucket.")
            test_passed = True

            s3 = self.parameters["s3"]

            cnt = 0
            for key in s3.bucket1.list():
                cnt += 1
                self.log.info(key.name)
                if s3.keys.count(key.name) != 1:
                    self.log.error("Unexpected key %s." % key.name)
                    test_passed = False

            if cnt != len(s3.keys):
                self.log.error("Missing key(s).")
                test_passed = False

            return test_passed


# This class contains the attributes and methods to test
# the FDS S3 interface to delete all the keys of a bucket.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3DelBucketKeys(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3DelBucketKeys,
                                             "Deleting all the keys of an S3 bucket")

    def test_S3DelBucketKeys(self):
        """
        Test Case:
        Attempt to delete all the keys of an S3 Bucket.
        """

        if (not "s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Delete all the keys of an S3 bucket.")
            s3 = self.parameters["s3"]

            for key in s3.bucket1.list():
                key.delete()

            for key in s3.bucket1.list():
                self.log.error("Unexpected keys remaining in bucket: %s" % key.name)
                return False

            return True


# This class contains the attributes and methods to test
# the FDS S3 interface to delete a bucket.
#
# You must have successfully created an S3 connection
# and stored it in self.parameters["s3"].conn (see TestS3IntFace.TestS3GetConn)
# and created a bucket and stored it in self.parameters["s3"].bucket1.
class TestS3DelBucket(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3DelBucket,
                                             "Deleting an S3 bucket")

    def test_S3DelBucket(self):
        """
        Test Case:
        Attempt to delete an S3 Bucket.
        """

        if not ("s3" in self.parameters) or (self.parameters["s3"].conn) is None:
            self.log.error("No S3 connection with which to load a BLOB.")
            return False
        elif not self.parameters["s3"].bucket1:
            self.log.error("No S3 bucket with which to load a BLOB.")
            return False
        else:
            self.log.info("Delete an S3 bucket.")
            s3 = self.parameters["s3"]

            s3.conn.delete_bucket('bucket1')

            if s3.conn.lookup('bucket1') != None:
                self.log.error("Unexpected bucket: bucket1")
                return False

            s3.bucket1 = None
            return True


class TestPuts(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None, dataset='key', count=10, size='1K'):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_Puts,
                                             "upload N no.of objects with specified size")
        self.dataset = dataset
        self.count   = int(count)
        self.size    = size
        self.passedBucket=bucket

    def test_Puts(self):
        if not self.checkS3Info(self.passedBucket):
            return False

        s3 = self.parameters["s3"]
        if self.dataset not in s3.verifiers:
            s3.verifiers[self.dataset] = {}
            s3.verifiers[self.dataset]['count'] = self.count
            s3.verifiers[self.dataset]['size'] = self.size
        else:
            self.count = s3.verifiers[self.dataset]['count']
            self.size = s3.verifiers[self.dataset]['size']

        self.log.info("uploading {} keys of size: {}".format(self.count, self.size))
        for n in range(0, self.count):
            key = Helper.keyName(self.dataset + str(n))
            value = Helper.genData(self.size,n)
            self.parameters["s3"].verifiers[self.dataset][key] = hash(value)
            k = Key(s3.bucket1, key)
            self.log.info('uploading key {} : {}'.format(n, key))
            k.set_contents_from_string(value)

        return True

class TestGets(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None, dataset='key', count=10, size='1K'):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_Gets,
                                             "fetch N no.of objects with specified size")
        self.dataset = dataset
        self.count   = int(count)
        self.size    = size
        self.passedBucket=bucket

    def test_Gets(self):
        if not self.checkS3Info(self.passedBucket):
            return False

        s3 = self.parameters["s3"]
        if self.dataset not in s3.verifiers:
            s3.verifiers[self.dataset] = {}
            s3.verifiers[self.dataset]['count'] = self.count
            s3.verifiers[self.dataset]['size'] = self.size
        else:
            self.count = s3.verifiers[self.dataset]['count']
            self.size = s3.verifiers[self.dataset]['size']

        self.log.info("fetching {} keys of size: {}".format(self.count, self.size))

        for n in range(0, self.count):
            key = Helper.keyName(self.dataset + str(n))
            value = s3.bucket1.get_key(key).get_contents_as_string()
            valuehash = hash(value)
            if self.parameters["s3"].verifiers[self.dataset][key] != valuehash:
                self.log.error('hash mismatch for key {} : {} '.format(n, key))
                return False
            k = Key(s3.bucket1, key)
            self.log.info('fetching key {} : {}'.format(n, key))
            k.set_contents_from_string(value)

        return True

class TestDeletes(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None, dataset='key', count=10):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_Deletes,
                                             "check N no.of keys")
        self.dataset = dataset
        self.count   = int(count)
        self.passedBucket=bucket

    def test_Deletes(self):
        if not self.checkS3Info(self.passedBucket):
            return False

        s3 = self.parameters["s3"]
        if self.dataset not in s3.verifiers:
            s3.verifiers[self.dataset] = {}
            s3.verifiers[self.dataset]['count'] = self.count
        else:
            self.count = s3.verifiers[self.dataset]['count']

        self.log.info("deleting {} keys".format(self.count))

        for n in range(0, self.count):
            key = Helper.keyName(self.dataset + str(n))
            k = Key(s3.bucket1, key)
            k.delete()

        return True

class TestKeys(TestCase.FDSTestCase):
    def __init__(self, parameters=None, bucket=None, dataset='key', count=10, exist=True):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_Keys,
                                             "delete N no.of objects")
        self.dataset = dataset
        self.count   = int(count)
        self.exist   = Helper.boolean(exist)
        self.passedBucket=bucket

    def test_Keys(self):
        if not self.checkS3Info(self.passedBucket):
            return False

        s3 = self.parameters["s3"]
        if self.dataset not in s3.verifiers:
            s3.verifiers[self.dataset] = {}
            s3.verifiers[self.dataset]['count'] = self.count
        else:
            self.count = s3.verifiers[self.dataset]['count']

        self.log.info("checking {} keys".format(self.count))
        bucket_keys = [key.name for key in s3.bucket1.list()]
        for n in range(0, self.count):
            key = Helper.keyName(self.dataset + str(n))
            key_exists = key in bucket_keys
            if self.exist != key_exists:
                self.log.error("key {} exist check failed. Should exist: {}, Does exist: {}".format(key, self.exist, key_exists))
                return False

        return True



# This class contains the attributes and methods to reset
# the S3 object so that we can reuse these test cases.
#
# You must have successfully created a connection.
class TestS3ObjReset(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_S3ObjReset,
                                             "Resetting S3 object")

    def test_S3ObjReset(self):
        """
        Test Case:
        Reset teh S3 object.
        """

        if not "s3" in self.parameters:
            self.log.error("No S3 interface object to reset.")
        else:
            self.log.info("Reset S3 object.")
            del self.parameters["s3"]

        return True

if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()
