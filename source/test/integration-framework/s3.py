import boto
import logging
from filechunkio import FileChunkIO
from boto.s3.key import Key
import boto.s3.connection
import os
import time

import config
import utils

class TestParameters():
    def __init__(self, bucket_name, bucket_prefix, key_name, keys_count, file_path, verbose):
        self.bucket_name = bucket_name
        self.bucket_prefix = bucket_prefix
        self.key_name    = key_name
        self.file_path   = file_path
        self.verbose     = verbose
        self.keys_count  = keys_count

# Class to contain S3 objects used by these test cases.
class S3Connection():

    logging.basicConfig(level=logging.INFO)
    log = logging.getLogger(__name__)

    bucket1 = None
    def __init__(self, id, token, host, port, om_ip_address, secure=True, debug=0):
        self.access_key_id      = id
        self.secret_access_key  = token
        self.host               = host
        self.port               = port
        self.is_secure          = secure
        self.debug              = debug
        self.conn               = None
        self.keys               = []
        self.is_authenticated   = False
        self.om_ip_address      = om_ip_address

    def s3_connect(self):
        if not self.is_authenticated:
            self.authenticate()

        if self.conn == None:
            #Get the connection
            self.conn = boto.connect_s3(aws_access_key_id=self.access_key_id,
                                    aws_secret_access_key=self.secret_access_key,
                                    host=self.host,
                                    port=self.port,
                                    calling_format = boto.s3.connection.OrdinaryCallingFormat())
        assert(self.conn)
        self.log.info('S3 Connected!')
    
    def authenticate(self):
        if self.is_authenticated:
            self.log.info("S3 Connection already authenticated")
            return
        if (self.secret_access_key == None or self.secret_access_key == ""):
            #Get the user token
            self.secret_access_key = str(utils.get_user_token(self.access_key_id,
                config.FDS_DEFAULT_ADMIN_PASS,
                self.om_ip_address,
                config.FDS_REST_PORT, 0, 1))
            self.is_authenticated = True
            
    def s3_disconnect(self):
        if self.conn:
            self.conn.close()
        self.is_authenticated = False
        self.log.info('S3 Disconnected')

    def get_s3_connection(self):
        if self.conn == None:
            self.s3_connect()
        return self.conn
    

import sys

def percent_cb(complete, total):
    sys.stdout.write('=')
    sys.stdout.flush()

if __name__ == "__main__":
    s3conn = S3Connection(
                config.FDS_DEFAULT_ADMIN_USER,
                None,
                '10.2.10.200',
                config.FDS_S3_PORT,
                '10.2.10.200'
            )
    s3conn.s3_connect()
    bucket = s3conn.conn.create_bucket('phil-bucket05-test')
    print bucket
    print "Sleeping 30 sec"
    time.sleep(30)
    testfile = "sample_file"
    print 'Uploading %s to Amazon S3 bucket %s' % \
       (testfile, bucket.name)
    k = Key(bucket)
    k.key = 'testfile'
    k.set_contents_from_filename(testfile,
            cb=percent_cb, num_cb=10)
    
    #print "Downloading the file..."
    #time.sleep(30)
    #key = bucket.get_key('testfile')
    #fp = open('testfile', 'w')
    #key.get_file(fp)
