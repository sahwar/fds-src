from svchelper import *
from svc_types.ttypes import *
from common.ttypes import *
from platformservice import *
import FdspUtils
import json


class VolumeGroupContext(Context):
    def __init__(self, *args):
        Context.__init__(self, *args)

    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('volid', help= "volume id", type=str)
    @arg('svcid', help= "service id or name", type=str)
    def state(self, volid, svcid):
        'Gets the volume group related state from service.  At the moment am or dm are valid'
        volid = self.config.getVolumeApi().getVolumeId(volid)
        for uuid in self.config.getServiceApi().getServiceIds(svcid):
            self.printServiceHeader(uuid)
            try:
                svcType = self.config.getServiceApi().getServiceType(uuid)
                state=dict()
                if svcType == 'am':
                    stateStr = ServiceMap.client(uuid).getStateInfo('volumegrouphandle.{}'.format(volid))
                    # humanize returned json
                    temp = json.loads(stateStr)
                    for key in temp.keys():
                        if key.isdigit():
                            dmSvcName = self.config.getServiceApi().getServiceName(long(key))
                            state[dmSvcName] = temp[key] 
                        else:
                            state[key] = temp[key]
                elif svcType == 'dm':
                    stateStr = ServiceMap.client(uuid).getStateInfo('volume.{}'.format(volid))
                    # humanize returned json
                    temp = json.loads(stateStr)
                    for key in temp.keys():
                        if key == "coordinator" and temp[key] != 0:
                            state[key] = self.config.getServiceApi().getServiceName(long(temp[key]))
                        else:
                            state[key] = temp[key]
                else:
                    print "Unknow service of type: " + svc_type
                    return
                print (json.dumps(state, indent=2, sort_keys=True))
            except Exception, e:
                print "Failed to fetch state.  Either service is down or argument is incorrect"
        return

    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('volid', help= "volume id", type=str)
    @arg('dmuuid', help= "dm uuid", type=str)
    def forcesync(self, volid, dmuuid):
        'Forces sync on a given volume.  Volume must be offline for this to work'
        volid = self.config.getVolumeApi().getVolumeId(volid)
        svc = self.config.getPlatform();
        msg = FdspUtils.newSvcMsgByTypeId('DbgForceVolumeSyncMsg');
        msg.volId = volid
        for uuid in self.config.getServiceApi().getServiceIds(dmuuid):
            cb = WaitedCallback();
            svc.sendAsyncSvcReq(uuid, msg, cb)

            print('-->From service {}: '.format(uuid))
            if not cb.wait(30):
                print 'Failed to force sync: {}'.format(self.config.getServiceApi().getServiceName(uuid))
            elif cb.header.msg_code != 0:
                print 'Failed to force sync error: {}'.format(cb.header.msg_code)
            else:
                print "Initiated force sync"

