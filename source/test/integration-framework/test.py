#!/usr/bin/python
# coding: utf8
from boto.s3.connection import S3Connection
import utils
'''
:q
:wq
import boto
from boto.s3.key import Key
import boto.s3.connection

FDS_DEFAULT_KEY_ID            = 'AKIAJCNNNWKKBQU667CQ'
FDS_DEFAULT_SECRET_ACCESS_KEY = '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'

conn = boto.s3.connect_to_region('us-east-1',
       aws_access_key_id=FDS_DEFAULT_KEY_ID,
       aws_secret_access_key=FDS_DEFAULT_SECRET_ACCESS_KEY,
       is_secure=True,               # uncommmnt if you are not using ssl
       calling_format = boto.s3.connection.OrdinaryCallingFormat(),
)

for i in xrange(1, 31):
    name = "hello_" + str(i)
    conn.delete_bucket(name)
'''
def main():
    hash1 = utils.hash_file_content("./downloads/sample_file_1")
    hash2 = utils.hash_file_content("./samples/sample_file_1")

    print hash1, hash2

if __name__ == "__main__":
   main()
