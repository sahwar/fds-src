from  svchelper import *
from svc_api.ttypes import *
from svc_types.ttypes import FDSPMsgTypeId
import platformservice
from platformservice import *
import FdspUtils
import humanize

class ScavengerContext(Context):
    def __init__(self, *args):
        Context.__init__(self, *args)

    def smClient(self):
        return self.config.getPlatform()

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='sm', nargs='?')
    def enable(self, sm):
        'enable garbage collection'
        try:
            for uuid in self.config.getServiceId(sm, False):
                print 'enabling scavenger on {}'.format(self.config.getServiceName(uuid))
                getScavMsg = FdspUtils.newEnableScavengerMsg()
                scavCB = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, getScavMsg, scavCB)

        except Exception, e:
            log.exception(e)
            return 'Enable failed'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='sm', nargs='?')
    def disable(self, sm):
        'disable garbage collection'
        try:
            for uuid in self.config.getServiceId(sm, False):
                print 'disabling scavenger on {}'.format(self.config.getServiceName(uuid))
                getScavMsg = FdspUtils.newDisableScavengerMsg()
                scavCB = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, getScavMsg, scavCB)
        except Exception, e:
            log.exception(e)
            return 'disable failed'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='sm', nargs='?')
    def start(self, sm):
        'start garbage collection on sm'
        try:
            for uuid in self.config.getServiceId(sm, False):
                print 'starting scavenger on {}'.format(self.config.getServiceName(uuid))
                getScavMsg = FdspUtils.newStartScavengerMsg()
                scavCB = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, getScavMsg, scavCB)
        except Exception, e:
            log.exception(e)
            return 'start failed'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('dm', help= "-Uuid of the DM to send the command to", type=str, default='dm', nargs='?')
    def refscan(self, dm):
        'start object refscanner on dm'
        try:
            for uuid in self.config.getServiceId(dm, False):
                print 'starting refscan on {}'.format(self.config.getServiceName(uuid))
                msg = FdspUtils.newSvcMsgByTypeId(FDSPMsgTypeId.StartRefScanMsgTypeId)
                cb = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, msg, cb)
        except Exception, e:
            log.exception(e)
            return 'start refscan failed'

    #--------------------------------------------------------------------------------------
    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='sm', nargs='?')
    def stop(self, sm):
        'stop garbage collection'
        try:
            for uuid in self.config.getServiceId(sm, False):
                print 'stopping scavenger on {}'.format(self.config.getServiceName(uuid))
                getScavMsg = FdspUtils.newStopScavengerMsg()
                scavCB = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, getScavMsg, scavCB)
        except Exception, e:
            log.exception(e)
            return 'stop failed'


    #--------------------------------------------------------------------------------------

    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='*', nargs='?')
    def info(self, sm):
        'show information about garbage collection'
        try:
            gcdata =[]
            cluster_totalobjects = 0
            cluster_deletedobjects = 0
            cluster_num_gc_running = 0
            cluster_num_compactor_running = 0
            cluster_num_refscan_running = 0
            numsvcs=0
            dm=False
            if sm == '*' :
                sm='sm'
                dm=True

            for uuid in self.config.getServiceId(sm, False):
                numsvcs += 1
                cntrs = ServiceMap.client(uuid).getCounters('*')
                keys=cntrs.keys()
                totalobjects =0
                deletedobjects=0
                totaltokens=0
                for key in keys:
                    if key.find('scavenger.token') >= 0:
                        if key.endswith('.total'):
                            totalobjects += cntrs[key]
                            totaltokens += 1
                        elif key.endswith('.deleted'):
                            deletedobjects += cntrs[key]
                data = []
                gcstart='not yet'
                key = 'sm.scavenger.start.timestamp'
                if key in cntrs and cntrs[key] > 0:
                    gcstart='{} ago'.format(humanize.naturaldelta(time.time()-int(cntrs[key])))

                cluster_totalobjects += totalobjects
                cluster_deletedobjects += deletedobjects
                data.append(('gc.start',gcstart))

                if cntrs.get('sm.scavenger.running',0) > 0: cluster_num_gc_running +=1
                if cntrs.get('sm.scavenger.compactor.running',0) : cluster_num_compactor_running +=1

                data.append(('num.gc.running', cntrs.get('sm.scavenger.running',0) ))
                data.append(('num.compactors', cntrs.get('sm.scavenger.compactor.running',0)))
                data.append(('objects.total',totalobjects))
                data.append(('objects.deleted',deletedobjects))
                data.append(('tokens.total',totaltokens))
                print ('{}\ngc info for {}\n{}'.format('-'*40, self.config.getServiceName(uuid), '-'*40))
                print tabulate(data,headers=['key', 'value'], tablefmt=self.config.getTableFormat())

            gcdata.append(('sm.objects.total',cluster_totalobjects))
            gcdata.append(('sm.objects.deleted',cluster_deletedobjects))
            gcdata.append(('sm.doing.gc',cluster_num_gc_running))
            gcdata.append(('sm.doing.compaction',cluster_num_compactor_running))

            if dm:
                totalobjects =0
                totalvolumes=0
                for uuid in self.config.getServiceId('dm', False):
                    cntrs = ServiceMap.client(uuid).getCounters('*')
                    keys=cntrs.keys()
                    data = []
                    key='dm.refscan.lastrun.timestamp'
                    if key in cntrs and cntrs[key] > 0:
                        data.append(('dm.refscan.lastrun', '{} ago'.format(humanize.naturaldelta(time.time()-int(cntrs[key])))))
                    else:
                        data.append(('dm.refscan.lastrun', 'not yet'))
                    data.append(('dm.refscan.num_objects', cntrs.get('dm.refscan.num_objects',0)))
                    data.append(('dm.refscan.num_volumes', cntrs.get('dm.refscan.num_volumes',0)))
                    data.append(('dm.refscan.running', cntrs.get('dm.refscan.running',0)))
                    if cntrs.get('dm.refscan.running',0) > 0 :  cluster_num_refscan_running += 1
                    totalobjects += cntrs.get('dm.refscan.num_objects',0)
                    totalvolumes += cntrs.get('dm.refscan.num_volumes',0)
                    print ('{}\ngc info for {}\n{}'.format('-'*40, self.config.getServiceName(uuid), '-'*40))
                    print tabulate(data,headers=['key', 'value'], tablefmt=self.config.getTableFormat())

                gcdata.append(('dm.objects.total',totalobjects))
                gcdata.append(('dm.volumes.total',totalvolumes))
                gcdata.append(('dm.doing.refscan',cluster_num_refscan_running))


            print ('\n{}\ncombined gc info\n{}'.format('='*40,'='*40))
            print tabulate(gcdata,headers=['key', 'value'], tablefmt=self.config.getTableFormat())
            print '=' * 40

        except Exception, e:
            log.exception(e)
            return 'get counters failed'


    #--------------------------------------------------------------------------------------
    @arg('sm', help= "-Uuid of the SM to send the command to", type=str, default='sm', nargs='?')
    def progress(self, sm):
        try:
            for uuid in self.config.getServiceId(sm, False):
                print 'progress of scavenger on {}'.format(self.config.getServiceName(uuid))
                getStatusMsg = FdspUtils.newScavengerProgressMsg()
                scavCB = WaitedCallback()
                self.smClient().sendAsyncSvcReq(uuid, getStatusMsg, scavCB)
                scavCB.wait()
                resp = scavCB.payload.progress_pct
                print "Scavenger progress: {}%".format(resp)
        except Exception, e:
            log.exception(e)
            return 'get progress failed'


class ScrubberContext(Context):
    def __init__(self, *args):
        Context.__init__(self, *args)

    def smClient(self):
        return self.config.getPlatform()

    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=long)
    def enable(self, sm):
        'enable scrubber'
        try:
            scrubEnableMsg = FdspUtils.newEnableScrubberMsg()
            scrubCB = WaitedCallback()
            self.smClient().sendAsyncSvcReq(sm, scrubEnableMsg, None)
        except Exception, e:
            log.exception(e)
            return 'enable scrubber failed'

    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=long)
    def disable(self, sm):
        'disable scrubber'
        try:
            scrubDisableMsg = FdspUtils.newDisableScrubberMsg()
            scrubCB = WaitedCallback()
            self.smClient().sendAsyncSvcReq(sm, scrubDisableMsg, scrubCB)
        except Exception, e:
            log.exception(e)
            return 'disable scrubber failed'

    @clicmd
    @arg('sm', help= "-Uuid of the SM to send the command to", type=long)
    def info(self, sm):
        'show info about scrubber status'
        try:
            scrubStatusMsg = FdspUtils.newQueryScrubberStatusMsg()
            scrubCB = WaitedCallback()
            self.smClient().sendAsyncSvcReq(sm, scrubStatusMsg, scrubCB)
            scrubCB.wait()
            resp = scrubCB.payload.scrubber_status
            print "Scrubber status: ",
            if resp == 1:
                print "ACTIVE"
            elif resp == 2:
                print "INACTIVE"
            elif resp == 3:
                print "DISABLED"
            elif resp == 4:
                print "FINISHING"
        except Exception, e:
            log.exception(e)
            return 'scrubber status failed'
