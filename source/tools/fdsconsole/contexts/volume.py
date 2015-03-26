from svchelper import *
from common.ttypes import ApiException
from common.ttypes import ResourceState
from platformservice import *
from restendpoint import *
from pyfdsp.config_types import *

import md5
import os
import FdspUtils 

class VolumeContext(Context):
    def __init__(self, *args):
        Context.__init__(self, *args)

        # Rest endpoint
        rest = RestEndpoint()
        self.volEp = VolumeEndpoint(rest)

    def s3Api(self):
        return self.config.getS3Api()

    #--------------------------------------------------------------------------------------
    @clicmd
    def list(self):
        'show the list of volumes in the system'
        try:
            volumes = ServiceMap.omConfig().listVolumes("")
            volumes.sort(key=lambda vol: ResourceState._VALUES_TO_NAMES[vol.state] + ':' + vol.name)
            return tabulate([(item.volId, item.name, item.tenantId, item.dateCreated, ResourceState._VALUES_TO_NAMES[item.state],
                              'OBJECT' if item.policy.volumeType == 0 else 'BLOCK',
                              item.policy.maxObjectSizeInBytes, item.policy.blockDeviceSizeInBytes) for item in volumes],
                            headers=['Id','Name', 'TenantId', 'Create Date','State','Type', 'Max-Obj-Size', 'Blk-Size'], tablefmt=self.config.getTableFormat())
        except ApiException, e:
            print e
        except Exception, e:
            log.exception(e)
        return 'unable to get volume list'

    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('vol-name', help= "-Volume name  of the clone")
    @arg('clone-name', help= "-name of  the  volume clone")
    @arg('policy-id', help= "-volume policy id" , default=0, type=int, nargs='?')
    @arg('timeline-time', help= "timeline time  of parent volume", nargs='?' , type=long, default=0)
    def clone(self, vol_name, clone_name, policy_id, timeline_time):
        'clone a given volume'
        try:
            volume_id  = ServiceMap.omConfig().getVolumeId(vol_name)
            ServiceMap.omConfig().cloneVolume(volume_id, policy_id, clone_name, timeline_time)
            return
        except ApiException, e:
            print e
        except Exception, e:
            log.exception(e)
        return 'create clone failed: {}'.format(vol_name)
    
    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('vol-name', help= "-volume name  of the clone")
    @arg('clone-name', help= "-name of  the  volume clone for restore")
    def restore(self, vol_name, clone_name):
        try:
            volume_id  = ServiceMap.omConfig().getVolumeId(vol_name)
            ServiceMap.omConfig().restoreClone(volume_id, clone_name)
            return
        except ApiException, e:
            print e
        except Exception, e:
            log.exception(e)
        return 'restore clone failed: {}'.format(vol_name)


    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('vol-name', help='-volume name')
    @arg('--domain', help='-domain to add volume to')
    @arg('--minimum', help='-qos minimum guarantee', type=int)
    @arg('--maximum', help='-qos maximum', type=int)
    @arg('--priority', help='-qos priority', type=int)
    @arg('--max-obj-size', help='-maxiumum size (in bytes) of volume objects', type=int)
    @arg('--vol-type', help='-type of volume to create', choices=['block','object'])
    @arg('--blk-dev-size', help='-maximum size (in bytes) of block device', type=int)
    @arg('--tenant-id', help='-id of tenant to create volume under', type=int)
    @arg('--commit-log-retention', help= " continuous commit log retention time in seconds", type=long)
    @arg('--media-policy', help='-media policy for volume', choices=['ssd', 'hdd', 'hybrid'])
    def create(self, vol_name, domain='abc', priority=10, minimum=0, maximum=0, max_obj_size=0,
               vol_type='object', blk_dev_size=21474836480, tenant_id=1, commit_log_retention=86400, media_policy='HDD_ONLY'):
        
        try:
            res = self.volEp.createVolume(vol_name,
                                            priority,
                                            minimum,
                                            maximum,
                                            vol_type,
                                            blk_dev_size,
                                            'B',
                                            media_policy,
                                            commit_log_retention,
                                            max_obj_size)
            return
        except ApiException, e:
            log.exception(e)
        except Exception, e:
            log.exception(e)

        return 'create volume failed: {}'.format(vol_name)


    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('vol-name', help='-volume name')
    @arg('--minimum', help='-qos minimum guarantee', type=int)
    @arg('--maximum', help='-qos maximum', type=int)
    @arg('--priority', help='-qos priority', type=int)
    def modify(self, vol_name, domain='abc', max_obj_size=2097152, tenant_id=1,
               minimum=0, maximum=0, priority=10):
        try:
            vols = self.volEp.listVolumes()
            vol_id = None
            mediaPolicy = None
            commit_log_retention = None
            for vol in vols:
                if vol['name'] == vol_name:
                    vol_id = vol['id']
                    mediaPolicy = vol['mediaPolicy']
                    commit_log_retention= vol['commit_log_retention']
            assert not vol_id is None
            assert not mediaPolicy is None
            assert not commit_log_retention is None
            res = self.volEp.setVolumeParams(vol_id, minimum, priority, maximum, mediaPolicy, commit_log_retention)
            return
        except ApiException, e:
            log.exception(e)
        except Exception, e:
            log.exception(e)

        return 'modify volume failed: {}'.format(vol_name)


    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('--domain', help='-name of domain that volume resides in')
    @arg('vol-name', help='-volume name')
    def delete(self, vol_name, domain='abc'):
        'delete a volume'
        try:
            ServiceMap.omConfig().deleteVolume(domain, vol_name)
            return
        except ApiException, e:
            print e
        except Exception, e:
            log.exception(e)
        return 'delete volume failed: {}'.format(vol_name)

    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('volname', help='-volume name')
    @arg('pattern', help='-blob name pattern for search', nargs='?', default='')
    @arg('count', help= "-max number for results", nargs='?' , type=long, default=1000)
    @arg('startpos', help= "-starting index of the blob list", nargs='?' , type=long, default=0)
    @arg('orderby', help= "-order by BLOBNAME or BLOBSIZE", nargs='?', default='UNSPECIFIED')
    @arg('descending', help="-display in descending order", nargs='?')
    def listblobs(self, volname, pattern, count, startpos, orderby, descending):
        try:
            dmClient = self.config.getPlatform();

            dmUuids = dmClient.svcMap.svcUuids('dm')
            volId = dmClient.svcMap.omConfig().getVolumeId(volname)

            getblobmsg = FdspUtils.newGetBucketMsg(volId, startpos, count);
            getblobmsg.pattern = pattern;
            if orderby == 'BLOBNAME':
                getblobmsg.orderBy = 1;
            elif orderby == 'BLOBSIZE':
                getblobmsg.orderBy = 2;
            else:
                getblobmsg.orderBy = 0;
            getblobmsg.descending = descending;
            listcb = WaitedCallback();
            dmClient.sendAsyncSvcReq(dmUuids[0], getblobmsg, listcb)

            if not listcb.wait():
                print 'async listblob request failed'

            #import pdb; pdb.set_trace()
            blobs = listcb.payload.blob_info_list;
            # blobs.sort(key=attrgetter('blob_name'))
            return tabulate([(x.blob_name, x.blob_size) for x in blobs],headers=
                 ['blobname', 'blobsize'], tablefmt=self.config.getTableFormat())

        except Exception, e:
            log.exception(e)
            return 'unable to get volume meta list'

    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('value', help='value' , nargs='?')
    def put(self, vol_name, key, value):
        ''' 
        put an object into the volume
        to put a file : start key/value with @
        put <volname> @filename  --> key will be name of the file
        put <volname> <key> @filename
        '''

        try:

            if key.startswith('@'):
                value = open(key[1:],'rb').read()
                key = os.path.basename(key[1:])
            elif value != None and value.startswith('@'):
                value = open(value[1:],'rb').read()
            b = self.s3Api().get_bucket(vol_name)
            k = b.new_key(key)
            num = k.set_contents_from_string(value)
            
            if num >= 0:
                data = []
                data += [('key' , key)]
                data += [('md5sum' , md5.md5(value).hexdigest())]
                data += [('length' , str(len(value)))]
                data += [('begin' , str(value[:30]))]
                data += [('end' , str(value[-30:]))]
                return tabulate(data, tablefmt=self.config.getTableFormat())
            else:
                print "empty data"
        except Exception, e:
            log.exception(e)
            return 'put {} failed on volume: {}'.format(key, vol_name)


    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('cnt', help= "count of objs to get", type=long, default=100)
    def bulkget(self, vol_name, cnt):
        '''
        Does bulk gets.
        TODO: Make it do random gets as well
        '''
        b = self.s3Api().get_bucket(vol_name)
        i = 0
        while i < cnt:
            for key in b.list():
                print "Getting object#: {}".format(i)
                print self.get(vol_name, key.name.encode('ascii','ignore'))
                i = i + 1
                if i >= cnt:
                    break
        return 'Done!'

    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('cnt', help= "count of objs to put", type=long, default=100)
    @arg('seed', help= "count of objs to put", default='seed')
    def bulkput(self, vol_name, cnt, seed):
        '''
        Does bulk put
        TODO: Make it do random put as well
        '''
        for i in xrange(0, cnt):
            k = "key_{}_{}".format(seed, i)
            v = "value_{}_{}".format(seed, i)
            print "Putting object#: {}".format(i)
            print self.put(vol_name, k, v)
        return 'Done!'
    #--------------------------------------------------------------------------------------
    @clidebugcmd
    def get(self, vol_name, key):
        'get an object from the volume'
        try:
            b = self.s3Api().get_bucket(vol_name)
            k = b.new_key(key)
            value = k.get_contents_as_string()

            if value:
                data = []
                data += [('key' , key)]
                data += [('md5sum' , md5.md5(value).hexdigest())]
                data += [('length' , str(len(value)))]
                data += [('begin' , str(value[:30]))]
                data += [('end' , str(value[-30:]))]
                return tabulate(data, tablefmt=self.config.getTableFormat())
            else:
                print "no data"
        except Exception, e:
            log.exception(e)
            return 'get {} failed on volume: {}'.format(key, vol_name)

    #--------------------------------------------------------------------------------------
    @clidebugcmd
    def keys(self, vol_name):
        'get an object from the volume'
        try:
            b = self.s3Api().get_bucket(vol_name)
            data = [[key.name.encode('ascii','ignore')] for key in b.list()]
            data.sort()
            return tabulate(data, tablefmt=self.config.getTableFormat(), headers=['name'])
            
        except Exception, e:
            log.exception(e)
            return 'get objects failed on volume: {}'.format(vol_name)

    #--------------------------------------------------------------------------------------

    @clidebugcmd
    def deleteobject(self, vol_name, key):
        'delete an object from the volume'
        try:
            b = self.s3Api().get_bucket(vol_name)
            k = b.new_key(key)
            k.delete()            
        except Exception, e:
            log.exception(e)
            return 'get {} failed on volume: {}'.format(key, vol_name)

