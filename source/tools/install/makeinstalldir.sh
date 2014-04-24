#!/bin/bash

source ./loghelper.sh

INSTALLDIR=/tmp/fdsinstall
SOURCEDIR=$(pwd)
PKGDIR=$(cd pkg; pwd)
PKGSERVER=http://coke.formationds.com:8000/external/
THISFILE=$(basename $0)

externalpkgs=(
    libpcre3_8.31-2ubuntu2_amd64.deb

    xfsprogs_3.1.9ubuntu2_amd64.deb
    java-common_0.51_all.deb
    oracle-java8-jdk_8u5_amd64.deb

    libjemalloc1_3.6.0.deb
    redis-server_2.8.8.deb
    redis-tools_2.8.8.deb

    paramiko-1.10.1.tar.gz
    scp-0.7.1.tar.gz
)

fdscorepkgs=(
    fds-accessmgr
    fds-cli
    fds-coredump
    fds-datamgr
    fds-orchmgr
    fds-platformmgr
    fds-pythonlibs
    fds-stormgr
    fds-systemconf
    fds-systemdir
    fds-tools
)

fdsrepopkgs=(
    fds-pkghelper
    fds-boost
    fds-leveldb
    fds-jdk-default
    fds-python-scp
    fds-python-paramiko
)

function getExternalPkgs() {
    loginfo "fetching external pkgs"
    (
        cd ${INSTALLDIR}
        for pkg in ${externalpkgs[@]} ; do
            loginfo "fetching pkg [$pkg]"
            if ! (wget -N ${PKGSERVER}/$pkg) ; then
                logerror "unable to fetch [$pkg] from ${PKGSERVER}"
            fi
        done
    )
}

function getFdsCorePkgs() {
    loginfo "copying core fds pkgs"
    (
        cd ${PKGDIR}
        for pkg in ${fdscorepkgs[@]} ; do
            debfile=$(ls -1t ${pkg}*.deb | head -n1)
            if [[ -n $debfile ]]; then
                loginfo "copying ${debfile}"
                cp ${debfile} ${INSTALLDIR}
            else
                logerror "unable to locate .deb file for [$pkg] in ${PKGDIR}"
            fi
        done
    )
}

function getFdsRepoPkgs() {
    loginfo "fetching fds pkgs from repo"
    sudo fdspkg update
    (
        cd ${INSTALLDIR}
        for pkg in ${fdsrepopkgs[@]} ; do
            loginfo "fetching $pkg from fds pkg repo"
            if ! (apt-get download $pkg) ; then
                logerror "unable to fetch [$pkg] from fds pkg repo"
            fi
        done
    )
}


function getInstallSources() {
    loginfo "copying install sources"
    ( 
        cd ${SOURCEDIR}
        for file in *.py *.sh ../../platform/python/disk_type.py; do
            if [[ ${file} != ${THISFILE} ]]; then
                loginfo "copying [$file] to ${INSTALLDIR}"
                cp $file ${INSTALLDIR}
            fi
        done
    )
}

function setupInstallDir() {
    loginfo "creating install dir @ ${INSTALLDIR}"
    if [[ ! -e ${INSTALLDIR} ]]; then
        mkdir ${INSTALLDIR}
    fi

    getExternalPkgs
    getFdsRepoPkgs
    getFdsCorePkgs
    getInstallSources
}

#############################################################################
# Main program
#############################################################################

setupInstallDir
#getFdsCorePkgs
