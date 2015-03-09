package com.formationds.util;

import com.formationds.apis.*;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.ResourceState;
import com.formationds.streaming.StreamingRegistrationMsg;
import com.formationds.util.thrift.ConfigurationApi;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.Lists;
import com.google.common.collect.Multimap;
import org.apache.thrift.TException;
import org.joda.time.DateTime;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Collectors;

public class StubConfigurationApi implements ConfigurationApi {
    // Tenants
    // Users (many-to-many relationship User <--> Tenant)
    // Snapshot policies (one-to-many relationship SnapshotPolicy <--> Volume)
    // Volume, VolumeSettings, VolumeDescriptor
    // StreamingRegistrationMsg
    // SnapshotPolicy
    // Clones?
    private List<SnapshotPolicy> snapshotPolicies;
    private AtomicLong snapshotPolicyId;

    private List<Tenant> tenants;
    private AtomicLong tenantId;

    private List<User> users;
    private AtomicLong userId;

    private Multimap<Tenant, User> userTenants;

    private List<VolumeDescriptor> volumes;
    private AtomicLong volumeId;

    private AtomicLong configurationVersion;

    public StubConfigurationApi() {
        configurationVersion = new AtomicLong(0);

        snapshotPolicies = new CopyOnWriteArrayList<>();
        snapshotPolicyId = new AtomicLong(0);
        tenants = new CopyOnWriteArrayList<>();
        tenantId = new AtomicLong(0);
        users = new CopyOnWriteArrayList<>();
        userId = new AtomicLong(0);
        userTenants = HashMultimap.create();
        volumes = new CopyOnWriteArrayList<>();
        volumeId = new AtomicLong();
    }

    @Override
    public long createSnapshotPolicy(String name, String recurrence, long retention, long timelineTime) throws TException {
        configurationVersion.incrementAndGet();
        long id = snapshotPolicyId.incrementAndGet();
        SnapshotPolicy snapshotPolicy = new SnapshotPolicy(id, name, recurrence, retention, ResourceState.Created, timelineTime);
        snapshotPolicies.add(snapshotPolicy);
        return id;
    }

    @Override
    public Optional<Tenant> tenantFor(long userId) {
        return userTenants.entries()
                .stream()
                .filter(entry -> entry.getValue().getId() == userId)
                .map(entry -> entry.getKey())
                .findFirst();
    }

    @Override
    public Long tenantId(long userId) {
        return tenantFor(userId)
                .get()
                .getId();
    }

    @Override
    public User getUser(long userId) {
        return users.stream()
                .filter(user -> user.getId() == userId)
                .findFirst()
                .get();
    }

    @Override
    public User getUser(String login) {
        return users.stream()
                .filter(user -> user.getIdentifier().equals(login))
                .findFirst()
                .get();
    }

    @Override
    public long createTenant(String identifier) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        Tenant tenant = new Tenant(tenantId.incrementAndGet(), identifier);
        tenants.add(tenant);
        return tenant.getId();
    }

    @Override
    public List<Tenant> listTenants(int ignore) throws ApiException, TException {
        return Lists.newArrayList(tenants);
    }

    @Override
    public long createUser(String identifier, String passwordHash, String secret, boolean isFdsAdmin) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        User user = new User(userId.incrementAndGet(), identifier, passwordHash, secret, isFdsAdmin);
        users.add(user);
        return user.getId();
    }

    @Override
    public void assignUserToTenant(long userId, long tenantId) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        User user = getUser(userId);
        Tenant tenant = getTenant(tenantId);
        userTenants.put(tenant, user);
    }

    public Tenant getTenant(long tenantId) {
        return tenants.stream()
                .filter(tenant -> tenant.getId() == tenantId)
                .findFirst()
                .get();
    }

    @Override
    public void revokeUserFromTenant(long userId, long tenantId) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        User user = getUser(userId);
        Tenant tenant = getTenant(tenantId);

        userTenants.get(tenant).remove(user);
    }

    @Override
    public List<User> allUsers(long ignore) throws ApiException, TException {
        return Lists.newArrayList(users);
    }

    @Override
    public List<User> listUsersForTenant(long tenantId) throws ApiException, TException {
        Tenant tenant = getTenant(tenantId);
        return Lists.newArrayList(userTenants.get(tenant));
    }

    @Override
    public void updateUser(long userId, String identifier, String passwordHash, String secret, boolean isFdsAdmin) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        users.stream()
                .filter(user -> user.getId() == userId)
                .forEach(user -> {
                    user.setIdentifier(identifier);
                    user.setPasswordHash(passwordHash);
                    user.setSecret(secret);
                    user.setIsFdsAdmin(isFdsAdmin);
                });
    }

    @Override
    public long configurationVersion(long ignore) throws ApiException, TException {
        return configurationVersion.get();
    }

    @Override
    public void createVolume(String domainName, String volumeName, VolumeSettings volumeSettings, long tenantId) throws ApiException, TException {
        configurationVersion.incrementAndGet();
        long volumeId = this.volumeId.incrementAndGet();
        VolumeDescriptor volumeDescriptor = new VolumeDescriptor(volumeName, DateTime.now().getMillis(), volumeSettings, tenantId, volumeId, ResourceState.Created);
        volumes.add(volumeDescriptor);
    }

    @Override
    public long getVolumeId(String volumeName) throws ApiException, TException {
        return volumes.stream()
                .filter(volume -> volume.getName().equals(volumeName))
                .findFirst()
                .get()
                .getVolId();
    }

    @Override
    public String getVolumeName(long volumeId) throws ApiException, TException {
        return volumes.stream()
                .filter(volume -> volume.getVolId() == volumeId)
                .findFirst()
                .get()
                .getName();
    }

    @Override
    public void deleteVolume(String domainName, String volumeName) throws ApiException, TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public VolumeDescriptor statVolume(String domainName, String volumeName) throws ApiException, TException {
        return volumes.stream()
                .filter(volume -> volume.getName().equals(volumeName))
                .findFirst()
                .get();
    }

    @Override
    public List<VolumeDescriptor> listVolumes(String domainName) throws ApiException, TException {
        return Lists.newArrayList(volumes);
    }

    @Override
    public int registerStream(String url, String http_method, List<String> volume_names, int sample_freq_seconds, int duration_seconds) throws TException {
        configurationVersion.incrementAndGet();

        return 0;
    }

    @Override
    public List<StreamingRegistrationMsg> getStreamRegistrations(int ignore) throws TException {
        return null;
    }

    @Override
    public void deregisterStream(int registration_id) throws TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public long createSnapshotPolicy(SnapshotPolicy policy) throws ApiException, TException {
        configurationVersion.incrementAndGet();

        long id = snapshotPolicyId.incrementAndGet();
        policy.setId(id);
        snapshotPolicies.add(policy);
        return id;
    }

    @Override
    public List<SnapshotPolicy> listSnapshotPolicies(long unused) throws ApiException, TException {
        return Lists.newArrayList(snapshotPolicies);
    }

    @Override
    public void deleteSnapshotPolicy(long id) throws ApiException, TException {
        configurationVersion.incrementAndGet();

        List<SnapshotPolicy> filtered = snapshotPolicies.stream()
                .filter(policy -> policy.getId() != id)
                .collect(Collectors.toList());
        this.snapshotPolicies = new CopyOnWriteArrayList<>(filtered);
    }

    @Override
    public void attachSnapshotPolicy(long volumeId, long policyId) throws ApiException, TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public List<SnapshotPolicy> listSnapshotPoliciesForVolume(long volumeId) throws ApiException, TException {
        return null;
    }

    @Override
    public void detachSnapshotPolicy(long volumeId, long policyId) throws ApiException, TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public List<Long> listVolumesForSnapshotPolicy(long policyId) throws ApiException, TException {
        return null;
    }

    @Override
    public void createSnapshot(long volumeId, String snapshotName, long retentionTime, long timelineTime) throws ApiException, TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public List<Snapshot> listSnapshots(long volumeId) throws ApiException, TException {
        return null;
    }

    @Override
    public void restoreClone(long volumeId, long snapshotId) throws ApiException, TException {
        configurationVersion.incrementAndGet();

    }

    @Override
    public long cloneVolume(long volumeId, long fdsp_PolicyInfoId, String cloneVolumeName, long timelineTime) throws TException {
        configurationVersion.incrementAndGet();

        return 0;
    }
}
