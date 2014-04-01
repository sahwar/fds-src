#!/usr/bin/env python

import os
import shutil
import glob
import time

import tempfile
import re
import subprocess
import logging

import argparse
import types
import sys
import traceback
import pwd,grp
import yaml

logging.basicConfig(level=logging.INFO,format="%(levelname)s : %(name)s : %(message)s")
log = logging.getLogger('pkgcreate')
log.setLevel(logging.INFO)

class PkgException(Exception):
    pass

class Helper:
    @staticmethod
    def checkField(fields,supportedFields,message=''):
        if type(fields) != types.ListType:
            fields = [ fields ]
        
        log.debug('fields: %s',fields)
        log.debug('supported: %s',supportedFields)
        for origfield in fields:
            if origfield not in supportedFields:
                field = origfield.lower()
                if field in supportedFields :
                    raise PkgException("unknown field [%s] found. Did you mean [%s] - %s" % (origfield,field,message))

                for item in supportedFields:
                    if item.startswith(field):
                        raise PkgException("unknown field [%s] found. Did you mean [%s] - %s" % (origfield,item,message))

                raise PkgException("unknown field [%s] found... - %s" % (origfield,message))

    @staticmethod
    def expandTicks(value):
        # first expand any shell ``
        st=value.find('`')
        if st >= 0:
            end = value.find('`',st+1)
            if end>st:
                out=Helper.getCmdOutput(value[st+1:end])
                value=value[:st] + ' ' + ' '.join(out) + ' ' + value[end+1:]                
        return value
    
    @staticmethod
    def getCmdOutput(cmd,shell=True):
        log.debug(cmd)
        p=subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=shell)
        return [line.strip('\n') for line in p.stdout]

'''
Maintain info about a file/dir
'''
class FileItem:
    def __init__(self,tokens=None):
        self.perms= None
        self.src  = None
        self.dest = None
        self.dir  = False
        self.owner= 'root:root'
        self.conf = False
        if tokens != None:
            self.load(tokens)

    def getOwnerId(self):
        uid = '0'
        gid = '0'
        if self.owner.find(':') > 0:
            uid,gid = self.owner.split(':')
        elif len(self.owner)>0:
            uid = self.owner
            gid = self.owner

        if uid.isalpha() :
            uid = pwd.getpwnam(uid).pw_uid

        if gid.isalpha():
            gid = grp.getgrnam(gid).gr_gid

        return (int(uid), int(gid))

    def __repr__(self):
        return '[conf=%s;dir=%s;dest=%s;src=%s]' % (self.conf,self.dir,self.dest,self.src)
        
    def load(self,tokens):
        supportedKeys = [ 'perms' , 'conf' , 'owner' ,'dest', 'src', 'dir' ]
        Helper.checkField(tokens.keys(),supportedKeys,'processing file')
        
        self.perms = tokens.get('perms', None)
        self.conf  = tokens.get('conf' , False)
        self.owner = tokens.get('owner', 'root:root')
        self.dest  = tokens.get('dest' , None)
        self.dir   = tokens.get('dir'  , False)

        if self.dest == None:
            raise PkgException('no destination specified for file item')

        self.dest = self.dest.strip()

        if tokens.get('src',None) == None :
            if tokens.get('dir') != None and not self.dir:
                raise PkgException("no src specified , but dir is set to false")
            else:
                # this is a directory 
                self.dir = True
                return

        self.src=[]
        
        if type(tokens['src']) == types.StringType :
            tokens['src'] = [ tokens['src'] ]

        for src in tokens['src']:
            value=src.strip()
            if value.startswith('`'):
                self.src.extend(Helper.getCmdOutput(value[1:-1]))
            else:
                self.src.extend(glob.glob(value))

        if len(self.src)>1 and tokens.get('dir',None) == None :
            self.dir = True

        if not self.dir and len(self.src)>1 :
            raise PkgException("More than 1 file in src but dest not specified as dir");
        
        if not self.dest.startswith('/'):
            log.debug(self)
            raise PkgException("all file destinations should start with / [%s]", self.dest)

    # get the list of files that will be copied
    # for a plain dir, this will be empty
    def getDestFileList(self):
        if self.src == None or len (self.src) == 0:
            return []
        
        if not self.dir:
            return [ self.dest ]

        filelist=[]
        for f in self.src:
            filelist.append(self.dest+'/'+ os.path.basename(f))

        return filelist

'''
Maintain info about a service
autostart : start automatically on boot
installstart : start on install
control : the file which has start/stop/status
'''
class Service:
    def __init__(self,tokens=None):
        self.name = None
        self.autostart = False
        self.installstart = False
        self.control = None

        if tokens != None:
            self.load(tokens)

    def __repr__(self):
        return '[name=%s;@boot=%s;@install=%s;control=%s]' % (self.name,self.autostart,self.installstart,self.control)

    def load(self,tokens) :
        
        supportedKeys = [ 'start-on-boot', 'start-on-install', 'name', 'control' ]
        Helper.checkField(tokens.keys(),supportedKeys,'processing file')

        self.autostart    = tokens.get('start-on-boot'   , False)
        self.installstart = tokens.get('start-on-install', False)

        self.name = tokens.get('name',None)
        self.control = tokens.get('control',None)

        if self.name == None:
            raise PkgException('service name not specified')

        if self.control == None:
            raise PkgException('service control file not specified')

        # validate
        p = re.compile('^[a-z][a-z0-9-.]+$')
        if not p.match(self.name):
            raise PkgException("name [%s] does not conform to standards" % self.name)

        if self.control == None or not os.path.exists(self.control):
            raise PkgException("invalid service control file [%s]" % (self.control))

class CronJob:
    def __init__(self,tokens=None):
        self.interval = ''
        self.user = 'root'
        self.command = ''

        if tokens :
            self.load(tokens)

    def __repr__(self):
        return '[interval=%s;command=%s;user=%s]' % (self.interval,self.command,self.user)

    def load(self,tokens):
        Helper.checkField(tokens.keys(),['interval','user','command'], 'processing cronjob')

        self.interval = tokens.get('interval','').strip()
        self.user     = tokens.get('user','root').strip()
        self.command  = tokens.get('command','').strip()

        # validate
        if self.interval.startswith('@'):
            self.interval = self.interval[1:]

        if self.interval.isalpha():
            Helper.checkField(self.interval,['reboot','yearly','annually','monthly','weekly','daily','hourly'],'cron interval')
            self.interval = '@' + self.interval
        else :
            # this is the standard cron format ..
            if len(self.interval.split(' ')) != 5:
                raise PkgException('please check the cron format [%s] - refer:http://unixhelp.ed.ac.uk/CGI/man-cgi?crontab+5',self.interval)

        if len(self.command) == 0 :
            raise PkgException('please specify a command to be executed');

        
class PkgCreate:
    def __init__(self):
        self.META_PACKAGE     = 'package' 
        self.META_MAINTAINER  = 'maintainer'
        self.META_VERSION     = 'version'
        self.META_DESCRIPTION = 'description'
        self.META_DEPENDS     = 'depends'

        self.META_INSTALL     = 'install-control'

        self.META_POSTINSTALL = 'postinstall'
        self.META_PREINSTALL  = 'preinstall'
        self.META_POSTREMOVE  = 'postremove'
        self.META_PREREMOVE   = 'preremove'
        
        self.installcontrol = dict()
        self.installcontrol[self.META_POSTINSTALL] = 'postinst' 
        self.installcontrol[self.META_PREINSTALL]  = 'preinst' 
        self.installcontrol[self.META_POSTREMOVE]  = 'postrm' 
        self.installcontrol[self.META_PREREMOVE]   = 'prerm' 

        self.verbose = False
        self.keepTemp = False
        self.dryrun = False
        self.releasePackage = False

        self.clear()

    def clear(self) :
        self.metadata = dict()
        self.files         = []
        self.services      = [] 
        self.cronjobs      = []
        self.installscript = ''
        self.metadata[self.META_DEPENDS] = []
        self.pkgdir=None
        
    def process(self,filename):
        log.info("processing : %s", filename)
        try:
            doc = yaml.load(open(filename).read())
            self.processDoc(doc)
        except PkgException as e:
            log.error("exception : %s " ,e.message)
            if self.verbose:
                traceback.print_exc()
            return False
        except Exception as e:
            log.error("exception : %s " ,e.message)
            if self.verbose:
                traceback.print_exc()
            return False
        
        log.debug("metadata = %s", self.metadata)
        log.debug("files = %s", self.files)
        try :
            if self.verify():
                self.makePkgDir()
                if not self.dryrun:
                    self.makePkg()
                else:
                    log.warn("this is a dry run - not making the package")
        finally:
            if self.pkgdir != None and not self.keepTemp:
                shutil.rmtree(self.pkgdir,True)
            elif self.pkgdir != None:
                log.info("the temporary dir is: %s", self.pkgdir)

    def processDoc(self,doc):
        supportedKeys = ['package' ,'maintainer','version','description','depends', 'files','services','cronjobs']
        Helper.checkField(doc.keys(),supportedKeys,'processing doc')

        for key in [ 'package', 'maintainer', 'version', 'description']:
            if key in doc:
                self.metadata[key] = str(doc[key])

        key = 'depends'
        if key in doc:
            if type(doc[key]) == types.StringType:
                doc[key] = [ doc[key] ]
            self.metadata[key].extend(doc[key])

        key = 'files'
        if key in doc:
            if type(doc[key]) != types.ListType:
                doc[key] = [ doc[key] ]

            for item in doc[key]:
                log.debug('processing a file item')
                fileItem =FileItem(item)
                self.files.append(fileItem)

        key = self.META_INSTALL
        if key in doc:
            self.installscript = doc[key]

        key = 'services'
        if key in doc:
            if type(doc[key]) != types.ListType:
                doc[key] = [ doc[key] ]

            for item in doc[key]:
                log.debug('processing a service item')
                service = Service(item)
                self.services.append(service)

        key = 'cronjobs'
        if key in doc:
            if type(doc[key]) != types.ListType:
                    doc[key] = [ doc[key] ]

            for item in doc[key]:
                log.debug('processing a cronjob item')
                cronjob = CronJob(item)
                self.cronjobs.append(cronjob)


    def verify(self):
        # check for meta data
        log.debug("verifying package information")
        #https://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Source
        if self.META_PACKAGE in self.metadata:
            log.debug('verifying package name : %s', self.metadata[self.META_PACKAGE])
            p = re.compile('^[a-z0-9][a-z0-9-\.]+$')
            if None == p.match(self.metadata[self.META_PACKAGE]) or len (self.metadata[self.META_PACKAGE]) < 5:
                log.error("invalid package name [%s] : lowercase, (a-z,0-9,+-.) and len >= 5 " % (self.metadata[self.META_PACKAGE]))
                return False
        else:
            log.error("meta : %s : missing"  % (self.META_PACKAGE))
            return False

        if self.META_VERSION in self.metadata:
            p = re.compile('^[0-9][a-z0-9-\.]+$')
            log.debug('verifying version : %s' , self.metadata[self.META_VERSION])
            if None == p.match(self.metadata[self.META_VERSION]):
                log.error( "invalid version [%s] : [^[0-9][a-z0-9-\.]+$] " % (self.metadata[self.META_VERSION]))
                return False
            
            # check if this is a release/test package
            if not self.releasePackage:
                self.metadata[self.META_VERSION] = '%s.T%d' % (self.metadata[self.META_VERSION], time.time()) 
            log.debug('version set to [%s]' % ( self.metadata[self.META_VERSION]) )
        else:
            log.error("meta : %s : missing"  % (self.META_VERSION))
            return False

        if self.META_DESCRIPTION in self.metadata:
            log.debug('verifying description')
            desc=self.metadata[self.META_DESCRIPTION]
            desc=desc.strip()
            if len (desc) < 10:
                log.error("please add more info in description")
                return False
            self.metadata[self.META_DESCRIPTION] = re.sub(r'\n +','\n ',desc)
        else:
            log.error("meta : %s : missing"  % (self.META_DESCRIPTION))
            return False

        if self.META_MAINTAINER in self.metadata:
            log.debug('verifying maintainer')
            p = re.compile('^[a-z0-9-\._ ]+ <[a-z0-9-\._]+@[a-z0-9-\._]+>$')
            if None == p.match(self.metadata[self.META_MAINTAINER]) :
                log.error("invalid maintainer [%s] :- name <email>" % (self.metadata[self.META_MAINTAINER]) )
                return False
        else:
            log.warn("meta : %s : missing .. so adding default"  % (self.META_MAINTAINER))
            self.metadata[self.META_MAINTAINER]= "fds <engineering@fds.com>"

        if self.META_DEPENDS in self.metadata and len(self.metadata[self.META_DEPENDS])>0:
            log.debug('verifying dependencies')
            p = re.compile('^[a-z0-9][a-z0-9-\.]+ *(\([<>=]* *[a-z0-9-\.]+\))?$')
            for depends in self.metadata[self.META_DEPENDS]:
                if None == p.match(depends):
                    log.error("invalid depends [%s] : ^[a-z0-9][a-z0-9-\.]+ ([<>=]*)$ " % (depends))
                    return False

        # check all the files
        log.debug('checking all the files')
        for item in self.files:
            if not item.dir:
                for f in item.src:
                    log.debug('checking file : %s' , f)
                    if not os.path.exists(f):
                        log.error('unable to locate file [%s]',f)
                        return False

        # check for automatic dependencies...
        if self.needsInstallScripts():
            fExists = False
            for depends in self.metadata[self.META_DEPENDS]:
                if depends.startswith('fds-pkghelper'):
                    fExists = True
                    break

            if not fExists:
                log.info("adding automatic dependency - fds-pkghelper")
                self.metadata[self.META_DEPENDS].append('fds-pkghelper')

        return True

    def writeControlFiles(self,debianDir):
        log.debug("writing control file :[%s]" , debianDir + '/control')
        with open(debianDir + '/control','w') as f: 
            f.write('Package: %s\n' % (self.metadata[self.META_PACKAGE]))
            f.write('Version: %s\n' % (self.metadata[self.META_VERSION]))
            f.write('Maintainer: %s\n' % (self.metadata[self.META_MAINTAINER]))
            f.write('Description: %s\n' %(self.metadata[self.META_DESCRIPTION]))
            output = Helper.getCmdOutput('dpkg --print-architecture')
            f.write('Architecture: %s\n' % (output[0]))
            
            if len(self.metadata[self.META_DEPENDS]) > 0 :
                f.write('Depends: ')
                maxlen=len(self.metadata[self.META_DEPENDS])
                for n in range(0,maxlen):
                    depends = self.metadata[self.META_DEPENDS][n]
                    f.write(depends)
                    if n < (maxlen-1):
                        f.write(', ')
                f.write('\n')

        fConfNeeded = False
        for item in self.files:
            if item.conf:
                fConfNeeded = True 
                break
        
        # https://www.debian.org/doc/manuals/maint-guide/dother.en.html#conffiles
        if not fConfNeeded:
            log.debug("no conf files specified")
        else:
            log.debug("writing conf files [%s]",debianDir + '/conffiles')
            with open(debianDir + '/conffiles','w') as f:
                for fileitem in self.files:
                    if fileitem.conf:
                        for destfile in fileitem.getDestFileList():
                            f.write('%s\n' % (destfile))

        self.writeInstallScripts(debianDir)
    
    # check to see if this pkg needs install scripts
    def needsInstallScripts(self):
        log.debug('install script : [%s]',self.installscript)
        log.debug('num services : [%d]' , len(self.services))
        if self.installscript == '' and len(self.services) == 0:
            return False
        return True

    # write pre/post install/remove files
    # https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html
    def writeInstallScripts(self,debianDir):
        # check if we need to write any install script
        if not self.needsInstallScripts():
            log.warn("no install scripts will be written")
            return False

        allservices=[]
        autoservices=[]
        startoninstall=[]
        
        for service in self.services:
            allservices.append(service.name)
            if service.autostart:
                autoservices.append(service.name)
            if service.installstart:
                startoninstall.append(service.name)
        
        templatescript='''
            #!/bin/bash
            ################################################
            # Auto generated by fds-pkg-create
            ################################################
            INSTALLSCRIPT=%s
            SERVICES="%s"
            AUTO_SERVICES="%s"
            START_ON_INSTALL="%s"

            source /usr/include/pkghelper/install-base.sh

            processInstallScript $(basename $0) $@
         ''' % (self.installscript, ' '.join(allservices), ' '.join(autoservices), ' '.join(startoninstall))
        
        templatescript = re.sub(r'\n *','\n',templatescript.strip())
        
        # Now write these files...
        for installtype,installname in self.installcontrol.items():
            installfile= '%s/%s' %(debianDir,installname)
            with open(installfile,'w') as f:
                f.write(templatescript)
            os.chmod(installfile, 0555)

        return True

    def writeCronJobs(self):
        if len(self.cronjobs) == 0:
            log.debug('no cronjobs sepcified')
            return
        cronfile = '/etc/cron.d/%s' % (self.metadata[self.META_PACKAGE])
        log.info('writing cron file : %s', cronfile)
        os.makedirs(self.pkgdir + "/etc/cron.d")
        with open(self.pkgdir + "/" + cronfile,'w') as f:
            for cron in self.cronjobs:
                f.write('%s %s %s\n' % (cron.interval, cron.user, cron.command))

    def writeServices(self):
        if len(self.services) == 0:
            log.debug('no services to be installed')
            return
        os.makedirs(self.pkgdir + "/etc/init.d")
        for service in self.services:
            servicefile = '%s/etc/init.d/%s' % (self.pkgdir,service.name)
            log.debug('setting up service [%s] @ [%s]' % (service.name, servicefile))
            shutil.copy2(service.control, servicefile)

    def makePkg(self):
        debfile="%s_%s.deb" % (self.metadata[self.META_PACKAGE],self.metadata[self.META_VERSION])
        log.info('creating %s' % (debfile))
        if 0 != subprocess.call(['dpkg-deb', '--build', self.pkgdir ,debfile]):
            log.warn("some issue with pkg creation")
            return False
        log.info("pkg created successfully : [%s]" , debfile)
        return True

    def makePkgDir(self):
        self.pkgdir=tempfile.mkdtemp(dir="./",prefix='tmppkg-')
        log.debug("pkgdir = %s" %( self.pkgdir))
        
        for item in self.files:
            uid,gid = item.getOwnerId()
            log.debug("will set perms to [%d:%d]",uid,gid)
            dest = self.pkgdir + item.dest
            if item.dir:
                log.debug('making dir : %s', dest)
                if not os.path.exist(dest):
                    os.makedirs(dest)
                os.chown(dest,uid,gid)
                for f in item.src:
                    log.debug("copy: src: %s, dest: %s " %(f,dest))
                    shutil.copy2(f,dest)
                    os.chown(dest+'/'+ os.path.basename(f) ,uid,gid)
            else :
                destdir = os.path.dirname(dest)
                log.debug ('destfile: %s , destdir: %s' % ( dest , destdir))
                if not os.path.exists(destdir):
                    os.makedirs(destdir)
                os.chown(destdir,uid,gid)
                log.debug("copy: src: %s, dest: %s " %(item.src[0],dest))
                shutil.copy2(item.src[0],dest)
                os.chown(destdir,uid,gid)

        debianDir = self.pkgdir + "/DEBIAN"
        os.mkdir(debianDir)
        self.writeControlFiles(debianDir)
        self.writeCronJobs()
        self.writeServices()

    def cleanDir(self):
        log.info('cleaning temp directories')
        for tempdir in glob.glob('tmppkg-*'):
            if os.path.isdir(tempdir):
                log.debug('cleaning temp dir: %s' , tempdir)
                shutil.rmtree(tempdir)

        log.info('cleaning temp packages')
        for testpkg in glob.glob('*.T[0-9]*.deb'):
            log.debug('cleaning test pkg : %s' , testpkg)
            os.remove(testpkg)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Debian Pkg Maker',formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-v','--verbose', action="store_true" , default=False, help = "be more verbose" )
    parser.add_argument('-k','--keeptemp', action="store_true", default=False, help = "keep temporary files")
    parser.add_argument('-n','--dryrun', action="store_true", default=False, help = "do not create the pkg")
    parser.add_argument('-r','--release', action="store_true", default=False, help = "create a release pkg")
    parser.add_argument('-c','--clean', action="store_true", default=False, help = "clean the current dir of temp files and test pkgs")

    parser.add_argument('pkgfiles', nargs=argparse.REMAINDER , help = "list of pkgdef files")

    args = parser.parse_args()

    pkg=PkgCreate()
    pkg.verbose  = args.verbose
    if pkg.verbose:
        log.setLevel(logging.DEBUG)

    pkg.keepTemp = args.keeptemp
    pkg.releasePackage = args.release

    # check for root priveleges
    if 0 != os.geteuid():
        log.error("need to be root or use sudo to create packages")
        sys.exit(1)
    
    if args.clean:
        pkg.cleanDir()
        sys.exit(0)

    if len(args.pkgfiles) == 0:
        args.pkgfiles = glob.glob('*.pkgdef')

    if len(args.pkgfiles) == 0:
        log.error("no .pkgdef files found..")
        sys.exit(0)

    pkg.dryrun = args.dryrun

    for pkgfile in args.pkgfiles:
        pkg.clear()
        pkg.process(pkgfile)
