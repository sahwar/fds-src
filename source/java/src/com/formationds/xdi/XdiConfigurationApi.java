/**
 * Copyright (c) 2015 Formation Data Systems. All rights reserved.
 */

package com.formationds.xdi;

import com.formationds.apis.*;
import com.formationds.streaming.StreamingRegistrationMsg;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.xdi.s3.S3Endpoint;
import com.google.common.collect.Lists;
import org.apache.log4j.Logger;
import org.apache.thrift.TException;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

// TODO: authorize here
public class XdiConfigurationApi implements ConfigurationApi {
    public static final String SYSTEM_VOLUME_PREFIX = "SYSTEM_VOLUME_";

    private static final Logger LOG = Logger.getLogger(XdiConfigurationApi.class);
    private static final long   KEY = 0;
    private final ConfigurationService.Iface                   config;
    private final ConcurrentHashMap<Long, CachedConfiguration> map;
    private Updater updater = null;

    public XdiConfigurationApi(ConfigurationService.Iface config) throws Exception {
        this.config = config;
        map = new ConcurrentHashMap<>();
        map.compute( KEY, ( k, v ) -> {
            try {
                return new CachedConfiguration( config );
            } catch ( Exception e ) {
                LOG.error( "Unable to load configuration", e );
                throw new RuntimeException( e );
            }
        } );
    }

    /**
     * Start the cache updater thread
     */
    public void startCacheUpdaterThread(long intervalMS) {
        updater = new Updater( intervalMS );
        new Thread(updater).start();
    }

    /**
     * Stop the cache updater thread
     */
    public void stopCacheUpdaterThread() {
        updater.shutdown();
    }

    /**
     *
     * @param tenantId
     *
     * @return the system volume name for the specified tenant.
     */
    public static String systemFolderName(long tenantId) {
        return SYSTEM_VOLUME_PREFIX + tenantId;
    }

    public long createSnapshotPolicy(final String name, final String recurrence, final long retention, final long timelineTime)
            throws TException {
        final SnapshotPolicy apisPolicy = new SnapshotPolicy();

        apisPolicy.setPolicyName(name);
        apisPolicy.setRecurrenceRule(recurrence);
        apisPolicy.setRetentionTimeSeconds(retention);
        apisPolicy.setTimelineTime(timelineTime);

        return createSnapshotPolicy(apisPolicy);
    }

    @Override
    public Optional<Tenant> tenantFor(long userId) {
        return fillCacheMaybe().tenantFor(userId);
    }

    @Override
    public Long tenantId(long userId) throws SecurityException {
        Long tid = fillCacheMaybe().tenantId( userId );
        if (tid == null) {
            getCache().loadTenants();
            tid = getCache().tenantId( userId );
        }
        return tid;
    }

    @Override
    public User getUser(long userId) {
        User user = fillCacheMaybe().getUser( userId );
        if (user == null) {
            getCache().loadUsers();
            user = getCache().getUser( userId );
        }
        return user;
    }

    @Override
    public User getUser(String login) {
        User user = fillCacheMaybe().getUser( login );
        if (user == null) {
            getCache().loadUsers();
            user = getCache().getUser( login );
        }
        return user;
    }

    @Override
    public long createTenant(String identifier)
            throws ApiException, TException {
        long tenantId = config.createTenant(identifier);

        VolumeSettings volumeSettings = new VolumeSettings(1024 * 1024 * 2, VolumeType.OBJECT, 0, 0, MediaPolicy.HDD_ONLY);
        String sysvolName = systemFolderName( tenantId );
        config.createVolume( S3Endpoint.FDS_S3, sysvolName, volumeSettings, tenantId );

        // TODO: need better API for accessing a specific tenant
        Optional<Tenant> found = config.listTenants( 0 )
                                       .stream()
                                       .filter( (t) -> {return t.getId() == tenantId;} )
                                       .findFirst();
        if ( found.isPresent() ) {
            getCache().addTenant( found.get() );
        } // TODO: else something is very wrong...
        return tenantId;
    }

    @Override
    public List<Tenant> listTenants(int ignore)
            throws ApiException, TException {
        return fillCacheMaybe().tenants();
    }

    @Override
    public long createUser(String identifier, String passwordHash, String secret, boolean isFdsAdmin)
            throws ApiException, TException {
        long userId = config.createUser(identifier, passwordHash, secret, isFdsAdmin);
        // TODO: need api to retrieve a specific user by id or login
        config.allUsers( 0 ).stream().forEach( getCache()::addUser );
        return userId;
    }

    @Override
    public void assignUserToTenant(long userId, long tenantId)
            throws ApiException, TException {
        config.assignUserToTenant( userId, tenantId );

        getCache().addTenantUser( tenantId, userId );
    }

    @Override
    public void revokeUserFromTenant(long userId, long tenantId)
            throws ApiException, TException {
        config.revokeUserFromTenant( userId, tenantId );
        getCache().removeTenantUser( tenantId, userId );
    }

    @Override
    public List<User> allUsers(long ignore)
            throws ApiException, TException {
        CachedConfiguration cached = fillCacheMaybe();
        return Lists.newArrayList(cached.users());
    }

    @Override
    public List<User> listUsersForTenant(long tenantId)
            throws ApiException, TException {
        return fillCacheMaybe().listUsersForTenant(tenantId);
    }

    @Override
    public void updateUser(long userId, String identifier, String passwordHash, String secret, boolean isFdsAdmin)
            throws ApiException, TException {
        config.updateUser( userId, identifier, passwordHash, secret, isFdsAdmin );

        // TODO: need a config api to get a specific user (and have updateUser return the updated user)
        config.allUsers( 0 ).stream().forEach( getCache()::addUser );
    }

    @Override
    public long configurationVersion(long ignore)
            throws ApiException, TException {
        return fillCacheMaybe().getVersion();
    }

    @Override
    public void createVolume(String domainName, String volumeName, VolumeSettings volumeSettings, long tenantId)
            throws ApiException, TException {
        config.createVolume( domainName, volumeName, volumeSettings, tenantId );

        VolumeType vt = volumeSettings.getVolumeType();
        long maxSize = (VolumeType.BLOCK.equals(vt) ?
                                volumeSettings.getBlockDeviceSizeInBytes() :
                                volumeSettings.getMaxObjectSizeInBytes());

        VolumeDescriptor newVol = config.statVolume( domainName, volumeName );

        getCache().addVolume( newVol );
    }

    @Override
    public long getVolumeId(String volumeName)
            throws ApiException, org.apache.thrift.TException {
        VolumeDescriptor vol = fillCacheMaybe().getVolume( "", volumeName );
        if (vol != null) {
            return vol.getVolId();
        }
        return config.getVolumeId( volumeName );
    }

    @Override
    public String getVolumeName(long volumeId)
            throws ApiException, org.apache.thrift.TException {
        VolumeDescriptor vol = fillCacheMaybe().getVolume( volumeId );
        if (vol != null) {
            return vol.getName();
        }
        return config.getVolumeName(volumeId);
    }

    @Override
    public void deleteVolume(String domainName, String volumeName)
            throws ApiException, TException {
        config.deleteVolume(domainName, volumeName);
        getCache().removeVolume( domainName, volumeName );
    }

    @Override
    public VolumeDescriptor statVolume(String domainName, String volumeName)
            throws ApiException, TException {
        VolumeDescriptor volumeDescriptor = fillCacheMaybe().getVolume( domainName, volumeName );
        if (volumeDescriptor == null) {
            volumeDescriptor = config.statVolume( domainName, volumeName );
            if (volumeDescriptor != null) {
                getCache().addVolume( volumeDescriptor );
            }
        }
        return volumeDescriptor;
    }

    @Override
    public List<VolumeDescriptor> listVolumes(String domainName)
            throws ApiException, TException {
        return fillCacheMaybe().getVolumes();
    }

    @Override
    public int registerStream(String url, String http_method, List<String> volume_names, int sample_freq_seconds, int duration_seconds)
            throws TException {
        return config.registerStream( url, http_method, volume_names, sample_freq_seconds, duration_seconds );
    }

    @Override
    public List<StreamingRegistrationMsg> getStreamRegistrations(int ignore)
            throws TException {
        return config.getStreamRegistrations( ignore );
    }

    @Override
    public void deregisterStream(int registration_id)
            throws TException {
        config.deregisterStream(registration_id);
    }

    @Override
    public long createSnapshotPolicy(com.formationds.apis.SnapshotPolicy policy)
            throws ApiException, org.apache.thrift.TException {
        long l = config.createSnapshotPolicy( policy );
        // TODO: is the value returned the new policy id?
        return l;
    }

    @Override
    public List<com.formationds.apis.SnapshotPolicy> listSnapshotPolicies(long unused)
            throws ApiException, org.apache.thrift.TException {
        return config.listSnapshotPolicies(unused);
    }

    // TODO need deleteSnapshotForVolume Iface call.

    @Override
    public void deleteSnapshotPolicy(long id)
            throws ApiException, org.apache.thrift.TException {
        config.deleteSnapshotPolicy(id);
    }

    // TODO need deleteSnapshotForVolume Iface call.

    @Override
    public void attachSnapshotPolicy(long volumeId, long policyId)
            throws ApiException, org.apache.thrift.TException {
        config.attachSnapshotPolicy(volumeId, policyId);
    }

    @Override
    public List<com.formationds.apis.SnapshotPolicy> listSnapshotPoliciesForVolume(long volumeId)
            throws ApiException, org.apache.thrift.TException {
        return config.listSnapshotPoliciesForVolume(volumeId);
    }

    @Override
    public void detachSnapshotPolicy(long volumeId, long policyId)
            throws ApiException, org.apache.thrift.TException {
        config.detachSnapshotPolicy(volumeId, policyId);
    }

    @Override
    public List<Long> listVolumesForSnapshotPolicy(long policyId)
            throws ApiException, org.apache.thrift.TException {
        return config.listVolumesForSnapshotPolicy( policyId );
    }

    @Override
    public void createSnapshot(long volumeId, String snapshotName, long retentionTime, long timelineTime)
            throws ApiException, TException {
        config.createSnapshot(volumeId, snapshotName, retentionTime, timelineTime);
        // TODO: is there a generated snapshot id?
    }

    @Override
    public List<com.formationds.apis.Snapshot> listSnapshots(long volumeId)
            throws ApiException, org.apache.thrift.TException {
        return config.listSnapshots(volumeId);
    }

    @Override
    public void restoreClone(long volumeId, long snapshotId)
            throws ApiException, org.apache.thrift.TException {
        config.restoreClone(volumeId, snapshotId);
    }

    @Override
    public long cloneVolume(long volumeId, long fdsp_PolicyInfoId, String clonedVolumeName, long timelineTime)
            throws org.apache.thrift.TException {
        long clonedVolumeId = config.cloneVolume(volumeId, fdsp_PolicyInfoId, clonedVolumeName, timelineTime);

        VolumeDescriptor clonedVol = config.statVolume( "", clonedVolumeName );
        if (clonedVol.getVolId() != clonedVolumeId) {
            // houston, we have a problem.
            throw new IllegalStateException( "Cloned volume id returned from clone " +
                                             "command does not match cloned volume descriptor." );
        }

        // update the cache with the new volume
        getCache().addVolume( clonedVol );

        return clonedVol.getVolId();
    }

    public CachedConfiguration getCache() {
        return fillCacheMaybe();
    }


    private void dropCache() {
        map.clear();
    }

    private CachedConfiguration fillCacheMaybe() {
        return map.computeIfAbsent( KEY, k -> {
            try {
                return new CachedConfiguration( config );
            } catch ( Exception e ) {
                LOG.error( "Error refreshing configuration", e );
                throw new RuntimeException( e );
            }
        } );
    }

    private class Updater
            implements Runnable {

        private final long intervalMS;
        private transient boolean stopRequested = false;

        Updater(long intervalMS) {
            this.intervalMS = intervalMS;
        }

        synchronized void shutdown() {
            stopRequested = true;
            notifyAll();
        }

        @Override
        public void run() {
            // do an initial wait
            try { Thread.sleep( 10000 ); }
            catch (InterruptedException ie) { Thread.currentThread().interrupt(); }

            while (!stopRequested) {
                try {
                    synchronized ( this ) {
                        wait( intervalMS );
                        if (stopRequested)
                            break;
                    }
                    map.compute(KEY, (k, v) -> {
                        try {
                            return obtainConfig(v);
                        } catch (Exception e) {
                            LOG.error("Error refreshing cache", e);
                            return v;
                        }
                    });
                } catch (InterruptedException e) {
                    // ignore and recheck loop
                }
            }
        }

        private CachedConfiguration obtainConfig(CachedConfiguration v)
                throws Exception {
            if (v == null) {
                return new CachedConfiguration(config);
            } else {
                long currentVersion = config.configurationVersion(0);
                if (currentVersion != v.getVersion()) {
                    LOG.debug("Cache updated, refreshing");
                    return new CachedConfiguration(config);
                } else {
                    return v;
                }
            }
        }
    }
}
