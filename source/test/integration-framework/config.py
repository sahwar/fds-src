#!/usr/bin/python
# Copyright 2014 by Formation Data Systems, Inc.
# Written by Philippe Ribeiro
# philippe@formationds.com
test_list = "fast_integration_test.json"
log_dir = "test-reports"
test_sets = "testsets"
setup = "setup.cfg"

FDS_ROOT="/root/fds-src/"
FDS_TOOLS="../../../source/tools/"
FDS_CMD="sudo %s/fds %s"
pyUnitConfig = "./../../source/test/testsuites/BuildSmokeTest.ini"

S3_USER = 'fds_testing'
S3_PASSWORD = 'bIEYKCPzDKtQDDBomnAKTKRA'
#FDS_DEFAULT_KEY_ID = 'AKIAJAWXAU57VVTDDWAA'
#FDS_DEFAULT_SECRET_ACCESS_KEY = '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'

FDS_DEFAULT_KEY_ID            = 'AKIAJCNNNWKKBQU667CQ'
FDS_DEFAULT_SECRET_ACCESS_KEY = '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'
FDS_DEFAULT_HOST             = 's3.amazonaws.com'

FDS_DEFAULT_PORT              = 443
FDS_AUTH_DEFAULT_PORT         = 443
FDS_DEFAULT_IS_SECURE         = True

FDS_DEFAULT_BUCKET_NAME       = "demo_volume2"
FDS_DEFAULT_BUCKET_PREFIX     = "demo_volume"
FDS_DEFAULT_KEY_NAME          = "file_1"
FDS_DEFAULT_KEYS_COUNT        = 10
FDS_DEFAULT_FILE_PATH         = "/tmp/foobar"
TEST_DEBUG                    = 1

params = {
    'log_level' : 20,
    'stop_on_fail' : True,
    'run_as_root' : False,
    'iterations' : 1,
    'threads' : 1,
    'root_user' : 'root',
    'sudo_password' : 'passwd',
    'host' : '1.2.3.4',
    'num_nodes' : 10,
    'pyUnit' : True,
    'tar_file' : False,
    'build' : None,
    'dryrun' : False,
    'store_to_database' : False,
    'verbose' : False,
}

NUMBER_USERS = 30
SAMPLE_FILE = "test.txt"
ROOT = "/root/"
ANSIBLE_ROOT = "../../../ansible/"
EC2_TEMPLATE = "./templates/ec2"
BAREMETAL_TEMPLATE = "./templates/integration-framework-cluster"
VAULT_PASS = ".vault_pass.txt"
DESTROY_EC2 = "scripts/teardown_ec2_cluster.sh %s"
START_EC2 = "scripts/deploy_fds_ec2.sh %s %s %s"
START_BAREMETAL = "scripts/deploy_fds.sh %s %s"

LOCAL_CLUSTER_IP_TABLE = ("10.2.10.200", "10.2.10.201", "10.2.10.202", 
                          "10.2.10.203")

CONFIG_DIR = "./config/"
SYSTEM_FRAMEWORK = "testsuites"
SYSTEM_CMD = "../%s/ClusterBootSuite.py -q ../%s/StaticMigration.ini -d dummy --verbose"