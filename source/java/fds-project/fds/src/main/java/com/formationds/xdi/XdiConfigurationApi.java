/**
 * Copyright (c) 2015 Formation Data Systems. All rights reserved.
 */

package com.formationds.xdi;

import com.formationds.apis.*;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.NotMasterDomain;
import com.formationds.protocol.svc.types.FDSP_Node_Info_Type;
import com.formationds.protocol.svc.types.FDSP_PolicyInfoType;
import com.formationds.protocol.svc.types.FDSP_VolumeDescType;
import com.formationds.protocol.svc.types.SvcInfo;
import com.formationds.protocol.svc.types.SvcUuid;
import com.formationds.util.thrift.CachedConfiguration;
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

    public XdiConfigurationApi( ConfigurationService.Iface config ) throws Exception {
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
     * Get API version used by service handler.
     */
    @Override
    public Version getVersion() throws ApiException, TException {

        return config.getVersion();
    }

    /**
     * Get the API version table for the service.
     */
    @Override
    public List<ServiceAPIVersion> getVersionTable() throws ApiException, TException {

        return config.getVersionTable();
    }

    /**
     * Handshake with the server to negotiate API version.
     * @param stubVersion - Version: The API version suggested by the server.client stub
     * @return Version: The API version acceptable to the server
     */
    @Override
    public Version suggestVersion(Version stubVersion) throws ApiException, TException {

       return config.suggestVersion(stubVersion);
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
    public FDSP_VolumeDescType GetVolInfo(FDSP_GetVolInfoReqType vol_info_req)
            throws ApiException, org.apache.thrift.TException {
        return config.GetVolInfo(vol_info_req);
    }

    @Override
    public int ModifyVol(FDSP_ModifyVolType mod_vol_req)
            throws ApiException, TException {
        return config.ModifyVol(mod_vol_req);
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
    public List<FDSP_VolumeDescType> ListVolumes(int ignore)
            throws ApiException, TException {
        return config.ListVolumes(ignore);
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
    public FDSP_PolicyInfoType createQoSPolicy(String policyName, long minIops, long maxIops, int relPrio)
            throws ApiException, TException {
        FDSP_PolicyInfoType qosPolicy = config.createQoSPolicy(policyName, minIops, maxIops, relPrio);
        return qosPolicy;
    }

    @Override
    public List<FDSP_PolicyInfoType> listQoSPolicies(long ignore)
            throws ApiException, TException {
        return config.listQoSPolicies(0);
    }

    @Override
    public FDSP_PolicyInfoType modifyQoSPolicy(String currentPolicyName, String newPolicyName, long minIops, long maxIops, int relPrio)
            throws ApiException, TException {
        FDSP_PolicyInfoType qosPolicy = config.modifyQoSPolicy(currentPolicyName, newPolicyName, minIops, maxIops, relPrio);
        return qosPolicy;
    }

    @Override
    public void deleteQoSPolicy(String policyName)
        throws ApiException, TException {
        config.deleteQoSPolicy(policyName);
    }

    @Override
    public void createSnapshot(long volumeId, String snapshotName, long retentionTime, long timelineTime)
            throws ApiException, TException {
        config.createSnapshot(volumeId, snapshotName, retentionTime, timelineTime);
        // TODO: is there a generated snapshot id?
    }

    @Override
    public void deleteSnapshot(long volumeId, long snapshotId)
            throws ApiException, TException {
        config.deleteSnapshot(volumeId, snapshotId);
    }


    @Override
    public List<com.formationds.protocol.svc.types.Snapshot> listSnapshots(long volumeId)
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

    @Override
    public long createLocalDomain(String domainName, String domainSite)
            throws ApiException, TException {
        long id = config.createLocalDomain(domainName, domainSite);
        return id;
    }

    @Override
    public boolean isLocalDomainUp() throws TException {
        return config.isLocalDomainUp();
    }

    @Override
    public List<LocalDomainDescriptor> listLocalDomains(int ignore)
            throws ApiException, TException {
        return config.listLocalDomains(0);
    }

    @Override
    public List<LocalDomainDescriptorV07> listLocalDomainsV07(int ignore)
            throws ApiException, TException {
        return config.listLocalDomainsV07(0);
    }

    @Override
    public void updateLocalDomainName(String oldDomainName, String newDomainName)
        throws ApiException, TException {
        config.updateLocalDomainName(oldDomainName, newDomainName);
        return;
    }

    @Override
    public void updateLocalDomainSite(String domainName, String newSiteName)
        throws ApiException, TException {
        config.updateLocalDomainSite(domainName, newSiteName);
        return;
    }

    @Override
    public void setThrottle(String domainName, double throttleLevel)
        throws ApiException, TException {
        config.setThrottle(domainName, throttleLevel);
        return;
    }

    @Override
    public void setScavenger(String domainName, String scavengerAction)
        throws ApiException, TException {
        config.setScavenger(domainName, scavengerAction);
        return;
    }

    @Override
    public void startupLocalDomain(String domainName)
        throws TException {
        config.startupLocalDomain(domainName);
        return;
    }
    @Override
    public int shutdownLocalDomain(String domainName)
            throws TException {
        return config.shutdownLocalDomain(domainName);
    }

    @Override
    public void deleteLocalDomain(String domainName)
        throws ApiException, TException {
        config.deleteLocalDomain(domainName);
        return;
    }

    @Override
    public void activateLocalDomainServices(String domainName, boolean sm, boolean dm, boolean am)
            throws TException {
        config.activateLocalDomainServices(domainName, sm, dm, am);
        return;
    }

    @Override
    public int ActivateNode(com.formationds.apis.FDSP_ActivateOneNodeType act_serv_req)
            throws TException {
        return config.ActivateNode(act_serv_req);
    }

    @Override
    public int AddService(com.formationds.protocol.pm.NotifyAddServiceMsg add_svc_req)
            throws TException {
        return config.AddService(add_svc_req);
    }

    @Override
    public int StartService(com.formationds.protocol.pm.NotifyStartServiceMsg start_svc_req)
            throws TException {
        return config.StartService(start_svc_req);
    }

    @Override
    public int StopService(com.formationds.protocol.pm.NotifyStopServiceMsg stop_svc_req)
            throws TException {
        return config.StopService(stop_svc_req);
    }

    @Override
    public int RemoveService(com.formationds.protocol.pm.NotifyRemoveServiceMsg rm_svc_req)
            throws TException {
        return config.RemoveService(rm_svc_req);
    }

    @Override
    public List<FDSP_Node_Info_Type> listLocalDomainServices(String domainName)
            throws TException {
        return config.listLocalDomainServices(domainName);
    }

    @Override
    public List<FDSP_Node_Info_Type> ListServices(int ignore)
            throws TException {
        return config.ListServices(ignore);
    }

    @Override
    public void removeLocalDomainServices(String domainName, boolean sm, boolean dm, boolean am)
            throws TException {
        config.removeLocalDomainServices(domainName, sm, dm, am);
        return;
    }

    @Override
    public int RemoveServices(FDSP_RemoveServicesType rm_node_req)
            throws TException {
        return config.RemoveServices(rm_node_req);
    }

    /* Subscription Management */
    /**
     * Create a subscription.
     *
     * @param subName: Subscription name. Unique within the global domain for the given tenant.
     * @param tenantID: ID of the tenant owning the subscription.
     * @param primaryDomainID: ID of the local domain in which the primary copy of the replicated volume resides.
     * @param primaryVolumeID: ID of the volume which is the source of replication.
     * @param replicaDomainID: ID of the local domain in which the replica copy of the replicated volume resides.
     * @param subType: Indicates whether the replication mechanism for this subscription is content-based or
     *                 transaction-based.
     * @param schedType: For content-based replication, specifies what kind of scheduling mechanism is to be used.
     * @param intervalSize: For content-based replication, specifies the quantity upon which the scheduling mechanism
     *                      is based. 5 might be every 5 minutes or every 5 MB of change depending upon schedType,
     *                      for example.
     * @return ID of the created subscription. 0 if creation failed.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public long createSubscription(String subName,
                                   long tenantID,
                                   int primaryDomainID,
                                   long primaryVolumeID,
                                   int replicaDomainID,
                                   com.formationds.apis.SubscriptionType subType,
                                   com.formationds.apis.SubscriptionScheduleType schedType,
                                   long intervalSize)
            throws ApiException, NotMasterDomain, TException {
        return config.createSubscription(subName,
                                            tenantID,
                                            primaryDomainID,
                                            primaryVolumeID,
                                            replicaDomainID,
                                            subType,
                                            schedType,
                                            intervalSize);
    }

    /**
     * List all subscriptions defined in the global domain.
     *
     * @param ignore: Crutch for Thrift when no parameters required.
     * @return List of subscriptions and their detail. May be empty if no subscriptions defined in the global domain.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listSubscriptionsAll(int ignore)
            throws ApiException, NotMasterDomain, TException {
        return config.listSubscriptionsAll(ignore);
    }

    /**
     * List all subscriptions defined in the global domain for the identified tenant.
     *
     * @param tenantID: ID of the tenant owning the subscriptions to be listed.
     * @return List of subscriptions owned by the tenant and their detail. May be empty if no subscriptions defined
     *         in the global domain for the identified tenant.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listTenantSubscriptionsAll(long tenantID)
            throws ApiException, NotMasterDomain, TException {
        return config.listTenantSubscriptionsAll(tenantID);
    }

    /**
     * List all subscriptions defined for the identified primary domain.
     *
     * @param primaryDomainID: ID of the local domain that is to be identified as "primary" for the subscriptions listed.
     * @return List of subscriptions with the identified primary domain and their detail. May be empty if no
     *         subscriptions are defined for the identified primary domain.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listSubscriptionsPrimaryDomain(int primaryDomainID)
            throws ApiException, NotMasterDomain, TException {
        return config.listSubscriptionsPrimaryDomain(primaryDomainID);
    }

    /**
     * List all subscriptions defined for the identified primary domain and tenant.
     *
     * @param primaryDomainID: ID of the local domain that is to be identified as "primary" for the subscriptions listed.
     * @param tenantID: ID of the tenant owning the subscriptions to be listed.
     * @return List of subscriptions with the identified primary domain and tenant and their detail. May be empty if no
     *         subscriptions defined for the identified primary domain and tenant.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listTenantSubscriptionsPrimaryDomain(int primaryDomainID, long tenantID)
            throws ApiException, NotMasterDomain, TException {
        return config.listTenantSubscriptionsPrimaryDomain(primaryDomainID, tenantID);
    }

    /**
     * List all subscriptions defined for the identified replica domain.
     *
     * @param replicaDomainID: ID of the local domain that is to be identified as "replica" for the subscriptions listed.
     * @return List of subscriptions with the identified replica domain and their detail. May be empty if no
     *         subscriptions are defined for the identified replica domain.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listSubscriptionsReplicaDomain(int replicaDomainID)
            throws ApiException, NotMasterDomain, TException {
        return config.listSubscriptionsReplicaDomain(replicaDomainID);
    }

    /**
     * List all subscriptions defined for the identified replica domain and tenant.
     *
     * @param replicaDomainID: ID of the local domain that is to be identified as "replica" for the subscriptions listed.
     * @param tenantID: ID of the tenant owning the subscriptions to be listed.
     * @return List of subscriptions with the identified replica domain and tenant and their detail. May be empty if no
     *         subscriptions defined for the identified replica domain and tenant.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listTenantSubscriptionsReplicaDomain(int replicaDomainID, long tenantID)
            throws ApiException, NotMasterDomain, TException {
        return config.listTenantSubscriptionsReplicaDomain(replicaDomainID, tenantID);
    }

    /**
     * List all subscriptions defined with the identified volume as primary. Note that tenant is implied since
     * a volume is owned by exactly one tenant.
     *
     * @param primaryVolumeID: ID of the volume which is named as "primary" for the subscription.
     * @return List of subscriptions with the identified primary volume and their detail. May be empty if no
     *         subscriptions defined for the identified primary volume.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listSubscriptionsPrimaryVolume(long primaryVolumeID)
            throws ApiException, NotMasterDomain, TException {
        return config.listSubscriptionsPrimaryVolume(primaryVolumeID);
    }

    /**
     * List all subscriptions defined with the identified volume as replica. Note that tenant is implied since
     * a volume is owned by exactly one tenant.
     *
     * @param replicaVolumeID: ID of the volume which is named as "replica" for the subscription.
     * @return List of subscriptions with the identified replica volume and their detail. May be empty if no
     *         subscriptions defined for the identified replica volume.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws TException
     */
    @Override
    public List<SubscriptionDescriptor> listSubscriptionsReplicaVolume(long replicaVolumeID)
            throws ApiException, NotMasterDomain, TException {
        return config.listSubscriptionsReplicaVolume(replicaVolumeID);
    }

    /**
     * Retrieve the subscription details for the subscription identified by name and tenant (which combined is a unique
     * identifier for a subscription).
     *
     * @param subName: Subscription name. Unique within the global domain for the given tenant.
     * @param tenantID: ID of the tenant owning the subscription named.
     * @return Details of subscription for the identified tenant and with the given name.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws SubscriptionNotFound: The subscription identified to be deleted can not be found.
     * @throws TException
     */
    @Override
    public SubscriptionDescriptor getSubscriptionInfoName(String subName, long tenantID)
            throws ApiException, NotMasterDomain, SubscriptionNotFound, TException {
        return config.getSubscriptionInfoName(subName, tenantID);
    }

    /**
     * Retrieve the subscription details for the subscription identified by the globally unique ID.
     *
     * @param subID: ID of the subscription whose details are to be retrieved.
     * @return Details of subscription identified.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws SubscriptionNotFound: The subscription identified to be deleted can not be found.
     * @throws TException
     */
    @Override
    public SubscriptionDescriptor getSubscriptionInfoID(long subID)
            throws ApiException, NotMasterDomain, SubscriptionNotFound, TException {
        return config.getSubscriptionInfoID(subID);
    }

    /**
     * Update the indicated subscription.
     *
     * @param subMods: Using the ID included in the subMods object, replace the current subscription contents with
     *                 the contents provided here. subMods content that cannot be modified includes tenantID, primaryDomainID,
     *                 primaryVolumeID, and replicaDomainID.
     * @return Nothing.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws SubscriptionNotFound: The subscription identified to be deleted can not be found.
     * @throws SubscriptionNotModified: The changed attribute cannot be modified.
     * @throws TException
     */
    @Override
    public void updateSubscription(com.formationds.apis.SubscriptionDescriptor subMods)
            throws ApiException, NotMasterDomain, SubscriptionNotFound, SubscriptionNotModified, TException {
        config.updateSubscription(subMods);
    }

    /**
     * Delete the subscription identified by name and tenant (which combined is a unique
     * identifier for a subscription).
     *
     * @param subName: Subscription name. Unique within the global domain for the given tenant.
     * @param tenantID: ID of the tenant owning the subscription named.
     * @param dematerialize: "true" if the replica data associated with this subscription is to be deleted as well.
     * @return Nothing.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws SubscriptionNotFound: The subscription identified to be deleted can not be found.
     * @throws SubscriptionNotModified: The subscription could not be deleted.
     * @throws TException
     */
    @Override
    public void deleteSubscriptionName(String subName, long tenantID, boolean dematerialize)
            throws ApiException, NotMasterDomain, SubscriptionNotFound, SubscriptionNotModified, TException {
        config.deleteSubscriptionName(subName, tenantID, dematerialize);
    }

    /**
     * Delete the subscription identified by its globally unique ID.
     *
     * @param subID: ID of the subscription to be deleted.
     * @param dematerialize: "true" if the replica data associated with this subscription is to be deleted as well.
     * @return Nothing.
     * @throws ApiException
     * @throws NotMasterDomain: This API may only be executed in the Master Domain.
     * @throws SubscriptionNotFound: The subscription identified to be deleted can not be found.
     * @throws SubscriptionNotModified: The subscription could not be deleted.
     * @throws TException
     */
    @Override
    public void deleteSubscriptionID(long subID, boolean dematerialize)
            throws ApiException, NotMasterDomain, SubscriptionNotFound, SubscriptionNotModified, TException {
        config.deleteSubscriptionID(subID, dematerialize);
    }

    public List<SvcInfo> getAllNodeInfo() throws TException {
        return config.getAllNodeInfo();
    }

    public SvcInfo getNodeInfo(SvcUuid nodeUuid) throws ApiException,
            TException {
        return config.getNodeInfo(nodeUuid);
    }

	@Override
	public long getDiskCapacityNode(SvcUuid nodeUuid) throws ApiException, TException {
		return config.getDiskCapacityNode(nodeUuid);
	}

	@Override
	public long getDiskCapacityTotal() throws ApiException, TException {
		return config.getDiskCapacityTotal();
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
