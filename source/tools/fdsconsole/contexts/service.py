from  svchelper import *
from svc_api.ttypes import *
from platformservice import *
import FdspUtils
import restendpoint
import re
import types
import humanize
import time

class ServiceContext(Context):
    def __init__(self, *args):
        Context.__init__(self, *args)
        self.__restApi = None
        self.config.setServiceApi(self)

    def restApi(self):
        if self.__restApi == None:
            self.__restApi = restendpoint.ServiceEndpoint(self.config.getRestApi())
        return self.__restApi

    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('nodeid', help= "node id",  type=long)
    @arg('svcname', help= "service name. Must be some combination of sm, dm, or am, separated by commas.")
    def addService(self, nodeid, svcname):
        'activate services on a node'

        # Split it up
        svcname = svcname.split(',')
        # Strip spaces, make lowercase
        svcname = map(lambda x: x.strip().lower(), svcname)

        svcs = {}
        if svcname.count('am') > 0:
            svcs['am'] = True
        if svcname.count('sm') > 0:
            svcs['sm'] = True
        if svcname.count('dm') > 0:
            svcs['dm'] = True

        try:
            return self.restApi().startService(nodeid, svcs)
        except Exception, e:
            log.exception(e)
            return 'unable to start service'

    #--------------------------------------------------------------------------------------
    @cliadmincmd
    @arg('nodeid', help= "node id",  type=long)
    @arg('svcname', help= "service name",  choices=['sm','dm','am'])
    def removeService(self, nodeid, svcname):
        'deactivate services on a node'
        try:
            return self.restApi().toggleServices(nodeid, {svcname: False})
        except Exception, e:
            log.exception(e)
            return 'unable to remove node'

    #--------------------------------------------------------------------------------------
    @clicmd
    def list(self):
        'show the list of services in the system'
        try:
            """
            services = ServiceMap.list()
            return tabulate(services, headers=['service id','service', 'incarnation no', 'ip','port', 'status'],
            """
            services = self.restApi().listServices()            
            return tabulate(map(lambda x: [x['uuid'], x['service'], x['ip'], x['port'], x['status']], services),
                            headers=['UUID', 'Service Name', 'IP4 Address', 'TCP Port', 'Service Status'],
                            tablefmt=self.config.getTableFormat())
        except Exception, e:
            log.exception(e)
            return 'unable to get service list'

    #--------------------------------------------------------------------------------------
    # will get service id for sm,am on a single node cluster by name
    # or sm:ip
    def getServiceId(self, pattern, onlyone=True):
        if type(pattern) == types.IntType:
            return pattern
        if pattern.isdigit():
            return int(pattern)

        try:
            name=None
            ip=None
            if ':' in pattern:
                name,ip = pattern.split(':')
            else:
                name=pattern.lower()
                
            if name not in ['om','am','pm','sm','dm']:
                print 'unknown service : {}'.format(name)
                return None
            services = self.restApi().listServices()            
            finallist = [s for s in services if (name == s['service'].lower()) and (ip == None or s['ip'] == ip)]
            if onlyone:
                if len(finallist) == 1:
                    return int(finallist[0]['uuid'])
                else:
                    print 'More than 1 matching services for pattern : {}'.format(pattern)
                    print tabulate(map(lambda x: [x['uuid'], x['service'], x['ip'], x['port'], x['status']], finallist),
                                   headers=['UUID', 'Service Name', 'IP4 Address', 'TCP Port', 'Service Status'],
                                   tablefmt=self.config.getTableFormat())
            else:
                if len(finallist) >= 1:
                    return [int(s['uuid']) for s in finallist]
            print 'No matching services for pattern : {}'.format(pattern)
        except Exception, e:
            log.exception(e)
            return 'unable to get service list'
        return None

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('svcid', help= "service uuid", type=str)
    @arg('match', help= "regex pattern", type=str, default=None, nargs='?')
    def listcounter(self, svcid, match):
        try:
            svcid=self.getServiceId(svcid)
            p=helpers.get_simple_re(match)
            cntrs = ServiceMap.client(svcid).getCounters('*')
            addeditems={}
            for key in cntrs:
                if key.endswith('.timestamp'):
                    addeditems[key + ".human"] = '{} ago'.format(humanize.naturaldelta(time.time()-int(cntrs[key])))
                elif key.endswith('.totaltime'):
                    addeditems[key + ".human"] = '{}'.format(humanize.naturaldelta(cntrs[key]))

            cntrs.update(addeditems)

            data = [(v,k) for k,v in cntrs.iteritems() if not p or p.match(k)]
            data.sort(key=itemgetter(1))
            return tabulate(data,headers=['value', 'counter'], tablefmt=self.config.getTableFormat())
            
        except Exception, e:
            log.exception(e)
            return 'unable to get counter'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('svcid', help= "service Uuid",  type=long)
    @arg('match', help= "regex pattern",  type=str, default=None, nargs='?')
    def listconfig(self, svcid, match):
        try:
            svcid=self.getServiceId(svcid)
            p=helpers.get_simple_re(match)
            cntrs = ServiceMap.client(svcid).getConfig(0)
            data = [(k.lower(),v) for k,v in cntrs.iteritems() if not p or p.match(k)]
            data.sort(key=itemgetter(0))
            return tabulate(data,headers=['config', 'value'], tablefmt=self.config.getTableFormat())
        except Exception, e:
            log.exception(e)
            return 'unable to get config list'


    #--------------------------------------------------------------------------------------
    @clidebugcmd
    @arg('svcid', help= "Service Uuid",  type=str)
    def showsvcmap(self, svcid):
        try:
            svcid=self.getServiceId(svcid)
            svcMap = ServiceMap.client(svcid).getSvcMap(None)
            data = [(e.svc_id.svc_uuid.svc_uuid, e.incarnationNo, e.svc_status, e.ip, e.svc_port) for e in svcMap]
            data.sort(key=itemgetter(0))
            return tabulate(data,headers=['uuid', 'incarnation', 'status', 'ip', 'port'], tablefmt=self.config.getTableFormat())
            
        except Exception, e:
            log.exception(e)
            return 'unable to get svcmap'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('svcid', help= "Service Uuid",  type=str)
    def listflag(self, svcid, name=None):
        try:
            svcid=self.getServiceId(svcid)
            if name is None:
                flags = ServiceMap.client(svcid).getFlags(None)
                data = [(v,k) for k,v in flags.iteritems()]
                data.sort(key=itemgetter(1))
                return tabulate(data, headers=['value', 'flag'], tablefmt=self.config.getTableFormat())
            else:
                return ServiceMap.client(svcid).getFlag(name)
        except Exception, e:
            log.exception(e)
            return 'unable to get volume list'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('svcid', type=str)
    @arg('flag', type=str)
    @arg('value', type=long)
    def setflag(self, svcid, flag, value):
        svcid=self.getServiceId(svcid)
        try:
            ServiceMap.client(svcid).setFlag(flag, value)
            return 'Ok'
        except Exception, e:
            log.exception(e)
            return 'Unable to set flag: {}'.format(flag)
    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('svcid', type=str)
    @arg('cmd', type=str)
    def setfault(self, svcid, cmd):
        svcid=self.getServiceId(svcid)
        try:
            success = ServiceMap.client(svcid).setFault(cmd)
            if success:
                return 'Ok'
            else:
                return "Failed to inject fault.  Check the command"
        except Exception, e:
            log.exception(e)
            return 'Unable to set fault'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('volname', help='-volume name')
    def listblobstat(self, volname):
        try:
            #process.setup_logger()
            #import pdb; pdb.set_trace()
            dmClient = self.config.getPlatform();

            dmUuids = dmClient.svcMap.svcUuids('dm')
            volId = dmClient.svcMap.omConfig().getVolumeId(volname)

            getblobmeta = FdspUtils.newGetVolumeMetaDataMsg(volId);
            cb = WaitedCallback();
            dmClient.sendAsyncSvcReq(dmUuids[0], getblobmeta, cb)

            if not cb.wait():
                print 'async volume meta request failed'

            data = []
            data += [("numblobs",cb.payload.volume_meta_data.blobCount)]
            data += [("size",cb.payload.volume_meta_data.size)]
            data += [("numobjects",cb.payload.volume_meta_data.objectCount)]
            return tabulate(data, tablefmt=self.config.getTableFormat())
        except Exception, e:
            log.exception(e)
            return 'unable to get volume meta list'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('volname', help='-volume name')
    def listdmstats(self, volname):
        try:
            
            #process.setup_logger()
	    # import pdb; pdb.set_trace()
            dmClient = self.config.getPlatform();
            volId = dmClient.svcMap.omConfig().getVolumeId(volname)

            dmUuids = dmClient.svcMap.svcUuids('dm')
            getstatsmsg = FdspUtils.newGetDmStatsMsg(volId);
            statscb = WaitedCallback();
            dmClient.sendAsyncSvcReq(dmUuids[0], getstatsmsg, statscb)

            if not statscb.wait():
		print 'async get dm stats request failed'

    	    data = []
	    data += [("commitlogsize",statscb.payload.commitlog_size)]
	    data += [("extent0size",statscb.payload.extent0_size)]
	    data += [("extentsize",statscb.payload.extent_size)]
	    data += [("metadatasize",statscb.payload.metadata_size)]
	    return  tabulate(data, tablefmt=self.config.getTableFormat())

        except Exception, e:
            print e
            log.exception(e)
            return 'unable to get dm stats '
