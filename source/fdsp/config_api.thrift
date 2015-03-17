/*
 * Copyright 2013-2015 Formation Data Systems, Inc.
 */

include "config_types.thrift"

include "common.thrift"

namespace cpp fds.apis
namespace java com.formationds.apis

/* ------------------------------------------------------------
   Operations on the tenancy, users, volumes and snapshots.
   ------------------------------------------------------------*/

/**
 * Configuration Service
 */
service ConfigurationService {
  /**
   * Create a new Local Domain.
   *
   * @param domainName - A string representing the name of the new Local Domain.
   *
   * @return Returns the Local Domain's ID.
   */
  i64 createLocalDomain(1:string domainName)
      throws (1: common.ApiException e);

  /**
   * Create a new tenant.
   *
   * @param identifier a string represnting the tenants identifier or name
   *
   * @return Returns the tenants id
   */
  i64 createTenant(1:string identifier)
      throws (1: common.ApiException e);

  /**
   * Enumerate tenants.
   *
   * @retruns Returns a list of tenants
   */
  list<config_types.Tenant> listTenants(1:i32 ignore)
      throws (1: common.ApiException e);

  /**
   * Create a user.
   *
   * @param identifier a string representing the user identifier, i.e. name
   * @param passwordHash a string representing the users password hash
   * @param secret a string represetning the secret passphrase
   * @param isFdsAdmin a boolean flag indicating if the user has adminstration permissions
   *
   * @return Returns the users id
   */
  i64 createUser(1:string identifier, 2: string passwordHash, 3:string secret, 4: bool isFdsAdmin)
      throws (1: common.ApiException e),

  /**
   * Assign user to a tenant.
   *
   * @param userId the user's uuid
   * @param tenantId the tenant's uuid
   */
  void assignUserToTenant(1:i64 userId, 2:i64 tenantId)
      throws (1: common.ApiException e),

  /**
   * Revoke user from tenant.
   *
   * @param userId the user's uuid
   * @param tenantId the tenant's uuid
   */
  void revokeUserFromTenant(1:i64 userId, 2:i64 tenantId)
      throws (1: common.ApiException e),

  /**
   * Enumerate users.
   *
   * @return Returns a list of users
   */
  list<config_types.User> allUsers(1:i64 ignore)
      throws (1: common.ApiException e),

  /**
   * Enumerate tenant's user list.
   *
   * @param tenantId the tenant's uuid
   *
   * @return Returns
   */
  list<config_types.User> listUsersForTenant(1:i64 tenantId)
      throws (1: common.ApiException e),

  /**
   * Modify a user.
   *
   * @param userId the user's uuid
   * @param identifier a string representing the user identifier, i.e. name
   * @param passwordHash a string representing the users password hash
   * @param secret a string represetning the secret passphrase
   * @param isFdsAdmin a boolean flag indicating if the user has adminstration permissions
   */
  void updateUser(
    1: i64 userId,
    2:string identifier,
    3:string passwordHash,
    4:string secret,
    5:bool isFdsAdmin)
      throws (1: common.ApiException e),

  /**
   * Configuration Version
   *
   * @return Returns the version of the configuration key-value store
   */
  i64 configurationVersion(1: i64 ignore)
      throws (1: common.ApiException e),

  /**
   * Create a volume.
   *
   * @param domainName a string representing the local domain name
   * @param volumeName a string representing the volume name
   * @param volumeSettings a VolumeSettings object
   * @param tenantId the tenant's uuid
   */
  void createVolume(
    1:string domainName,
    2:string volumeName,
    3:config_types.VolumeSettings volumeSettings,
    4: i64 tenantId)
      throws (1: common.ApiException e),

# TODO Returns the volume id

  /**
   * Retrieve FDS volume identifier.
   *
   * @param volumeName a string representing the volume name
   *
   * @return the volume uuid
   */
  i64 getVolumeId(1:string volumeName)
      throws (1: common.ApiException e),

  /**
   * Retrieve FDS volume name.
   *
   * @param volumeId the volume's uuid
   *
   * @return Returns a string representing teh volume name
   */
  string getVolumeName(1:i64 volumeId)
      throws (1: common.ApiException e),

  /**
   * Delete FDS volume.
   *
   * @param domainName a string representing the local domain name
   * @param volumeName a string representing the volume name
   */
  void deleteVolume(1:string domainName, 2:string volumeName)
      throws (1: common.ApiException e),

# TODO Delete volume by id

  /**
   * Request volume status information.
   *
   * @param domainName a string representing the local domain name
   * @param volumeName a string representing the volume name
   */
  config_types.VolumeDescriptor statVolume(
    1:string domainName,
    2:string volumeName)
      throws (1: common.ApiException e),

  # TODO add a statVolume by volume id

  /**
   * Enumerate volumes.
   *
   * @param domainName a string representing the local domain name
   */
  list<config_types.VolumeDescriptor> listVolumes(1:string domainName)
      throws (1: common.ApiException e),

  /**
   * Register a statistic stream.
   *
   * @param url a string represening the URL
   * @param http_method the HTTP method
   * @param volume_names a list of strings representing the associated volume names
   * @param sample_freq_seconds the sample freqency. in seconds
   * @param duration_seconds the duration, in seocnds
   *
   * @return Returns statistics stream uuid
   */
  i32 registerStream(
      1:string url,
      2:string http_method,
      3:list<string> volume_names,
      4:i32 sample_freq_seconds,
      5:i32 duration_seconds),

  /**
   * Enumerate stream registrations.
   *
   * @return Returns a list of StreamRegisterationMsg objects
   */
  list<config_types.StreamingRegistrationMsg> getStreamRegistrations(1:i32 ignore),

  /**
   * Unregister a statistics stream.
   *
   * @param regsiteration_id tghe statictics stream uuid
   */
  void deregisterStream(1:i32 registration_id),

  /**
   * Create a new snapshot policy.
   *
   * @param policy the SnapshotPolicy object
   *
   * @return Returns snapshot policy id
   */
  i64 createSnapshotPolicy(1:config_types.SnapshotPolicy policy)
      throws (1: common.ApiException e),

  /**
   * Enumerate snapshot policies.
   *
   * @return Returns a list of SnapshotPolicy objects
   */
  list<config_types.SnapshotPolicy> listSnapshotPolicies(1:i64 unused)
      throws (1: common.ApiException e),

  /**
   * Delete a snapshot policy.
   *
   * @param id the snapshot id to be deleted
   */
  void deleteSnapshotPolicy(1:i64 id)
      throws (1: common.ApiException e),

  /**
   * Set a snapshot policy on a volume.
   *
   * @param volumeId the volume uuid
   * @param policyId the policy uuid
   */
  void attachSnapshotPolicy(1:i64 volumeId, 2:i64 policyId)
      throws (1: common.ApiException e),

  /**
   * Enumerate a volume's snapshot policies.
   *
   * @param volumeId the volume uuid
   *
   * @return Returns a list of SnapshotPolicy objects
   */
  list<config_types.SnapshotPolicy> listSnapshotPoliciesForVolume(
    1:i64 volumeId)
      throws (1: common.ApiException e),

  /**
   * Unset a snapshot policy from a volume.
   *
   * @param volumeId the volume uuid
   * @param policyId the policy uuid
   */
  void detachSnapshotPolicy(1:i64 volumeId, 2:i64 policyId)
      throws (1: common.ApiException e),

  /**
   * Enumate volumes using snapshot policy.
   *
   * @param policyId the policy uuid
   *
   * @return Returns list of volumeId
   */
  list<i64> listVolumesForSnapshotPolicy(1:i64 policyId)
      throws (1: common.ApiException e),

  /**
   * Create's a snapshot.
   *
   * @param volumeId the volume uuid
   * @param snapshotName a string representing the snapshot name
   * @param retentionTime the retention time, in seconds
   * @param timelineTime the timeline time, in seconds
   */
  void createSnapshot(
    1:i64 volumeId,
    2:string snapshotName,
    3:i64 retentionTime,
    4:i64 timelineTime)
      throws (1: common.ApiException e),

  /**
   * Enumerate snapshots.
   *
   * @param volumeId the volume id
   *
   * @return Returns a list of snapshots
   */
  list<common.Snapshot> listSnapshots(1:i64 volumeId)
      throws (1: common.ApiException e),

  /**
   * Reset a volume to a previous snapshot.
   *
   * @param volumeId the volume uuid
   * @param snapshotId the snapshot uuid
   */
  void restoreClone(1:i64 volumeId, 2:i64 snapshotId)
      throws (1: common.ApiException e),

  /**
   * Retrieve volume clone identifier.
   *
   * @param volumeId the volume uuid
   * @param fdspPolicyInfoId the policy infomration uuid
   * @param cloneVolumeName the clone volume name
   * @param timelimeTimes the timeline time, in seconds
   *
   * @return Returns the clone volume id
   */
  i64 cloneVolume(
    1:i64 volumeId,
    2:i64 fdsp_PolicyInfoId,
    3:string cloneVolumeName,
    4:i64 timelineTime)
}