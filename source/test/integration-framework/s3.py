import boto
import logging
from filechunkio import FileChunkIO
from boto.s3.key import Key
import boto.s3.connection

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
    def __init__(self, id, token, host, port, secure=True, debug=0):
        self.access_key_id      = id
        self.secret_access_key  = token
        self.host               = host
        self.port               = port
        self.is_secure          = secure
        self.debug              = debug
        self.conn               = None
        self.keys               = []

    def s3_connect(self):
        if self.conn == None:

            if (self.secret_access_key == None or self.secret_access_key == "") :
                #Get the user token
                self.secret_access_key = str(utils.get_user_token(self.access_key_id,
                    config.FDS_AUTH_DEFAULT_ADMIN_PASS,
                    self.host,
                    config.FDS_REST_PORT, 0, 1))

            #Get the connection
            self.conn = boto.connect_s3(aws_access_key_id=self.access_key_id,
                                    aws_secret_access_key=self.secret_access_key,
                                    host=self.host,
                                    #port=8443,
                                    port=self.port,
                                    calling_format = boto.s3.connection.OrdinaryCallingFormat())

        assert(self.conn)
        self.log.info('S3 Connected!')

    def s3_disconnect(self):
        if self.conn:
            self.conn.close()
        self.log.info('S3 Disconnected')

    def get_s3_connection(self):
        if self.conn == None:
            self.s3_connect()
        return self.conn

'''
if __name__ == "__main__":
    s3conn = S3Connection(
                config.FDS_DEFAULT_KEY_ID,
                config.FDS_DEFAULT_SECRET_ACCESS_KEY,
                config.FDS_DEFAULT_HOST,
                config.FDS_AUTH_DEFAULT_PORT,
            )
    print s3conn.conn
    bucket = s3conn.conn.create_bucket('phil-bucket02-test')
'''
