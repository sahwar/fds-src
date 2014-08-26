include "../fdsp/snapshot.thrift"
include "../fdsp/fds_stream.thrift"
namespace cpp fds.apis
namespace java com.formationds.apis

enum VolumeType {
     OBJECT = 0,
     BLOCK = 1
}

struct VolumeSettings {
       1: required i32 maxObjectSizeInBytes,
       2: required VolumeType volumeType,
       3: required i64 blockDeviceSizeInBytes
}

struct VolumeDescriptor {
       1: required string name,
       2: required i64 dateCreated,
       3: required VolumeSettings policy,
       4: required i64 tenantId    // Added for multi-tenancy
}

struct VolumeStatus {
       1: required i64 blobCount
       2: required i64 currentUsageInBytes;
}

struct ObjectOffset {
       1: required i64 value
}

enum ErrorCode {
     INTERNAL_SERVER_ERROR = 0,
     MISSING_RESOURCE,
     BAD_REQUEST,
     RESOURCE_ALREADY_EXISTS,
     RESOURCE_NOT_EMPTY,
     SERVICE_NOT_READY
}

exception ApiException {
       1: string message;
       2: ErrorCode errorCode;
}

struct BlobDescriptor {
       1: required string name,
       2: required i64 byteCount,
       4: required map<string, string> metadata
}

struct TxDescriptor {
       1: required i64 txId
}

service AmService {
	void attachVolume(1: string domainName, 2:string volumeName)
             throws (1: ApiException e),

        list<BlobDescriptor> volumeContents(1:string domainName, 2:string volumeName, 3:i32 count, 4:i64 offset)
             throws (1: ApiException e),

        BlobDescriptor statBlob(1: string domainName, 2:string volumeName, 3:string blobName)
             throws (1: ApiException e),

        TxDescriptor startBlobTx(1:string domainName, 2:string volumeName, 3:string blobName, 4:i32 blobMode)
	    throws (1: ApiException e),

        void commitBlobTx(1:string domainName, 2:string volumeName, 3:string blobName, 4:TxDescriptor txDesc)
	    throws (1: ApiException e),

        void abortBlobTx(1:string domainName, 2:string volumeName, 3:string blobName, 4:TxDescriptor txDesc)
	    throws (1: ApiException e),

        binary getBlob(1:string domainName, 2:string volumeName, 3:string blobName, 4:i32 length, 5:ObjectOffset offset)
             throws (1: ApiException e),

        void updateMetadata(1:string domainName, 2:string volumeName, 3:string blobName, 4:TxDescriptor txDesc, 5:map<string, string> metadata)
             throws (1: ApiException e),

        void updateBlob(1:string domainName,
          2:string volumeName, 3:string blobName, 4:TxDescriptor txDesc, 5:binary bytes, 6:i32 length, 7:ObjectOffset objectOffset, 9:bool isLast) throws (1: ApiException e),

        void updateBlobOnce(1:string domainName,
          2:string volumeName, 3:string blobName, 4:i32 blobMode, 5:binary bytes, 6:i32 length, 7:ObjectOffset objectOffset, 8:map<string, string> metadata) throws (1: ApiException e),

          void deleteBlob(1:string domainName, 2:string volumeName, 3:string blobName)
            throws (1: ApiException e)

        VolumeStatus volumeStatus(1:string domainName, 2:string volumeName)
             throws (1: ApiException e),
}

// Added for multi-tenancy
struct User {
    1: i64 id,
    2: string identifier,
    3: string passwordHash,
    4: string secret,
    5: bool isFdsAdmin
}

// Added for multi-tenancy
struct Tenant {
    1: i64 id,
    2: string identifier
}


service ConfigurationService {
        // // Added for multi-tenancy
        i64 createTenant(1:string identifier)
             throws (1: ApiException e),

        list<Tenant> listTenants(1:i32 ignore)
             throws (1: ApiException e),

        i64 createUser(1:string identifier, 2: string passwordHash, 3:string secret, 4: bool isFdsAdmin)
             throws (1: ApiException e),

        void assignUserToTenant(1:i64 userId, 2:i64 tenantId)
             throws (1: ApiException e),

        void revokeUserFromTenant(1:i64 userId, 2:i64 tenantId)
             throws (1: ApiException e),

        list<User> allUsers(1:i64 ignore)
             throws (1: ApiException e),

        list<User> listUsersForTenant(1:i64 tenantId)
             throws (1: ApiException e),

        void updateUser(1: i64 userId, 2:string identifier, 3:string passwordHash, 4:string secret, 5:bool isFdsAdmin)
             throws (1: ApiException e),


        // Added for caching
        i64 configurationVersion(1: i64 ignore)
             throws (1: ApiException e),

        void createVolume(1:string domainName, 2:string volumeName, 3:VolumeSettings volumeSettings, 4: i64 tenantId)
             throws (1: ApiException e),

        void deleteVolume(1:string domainName, 2:string volumeName)
             throws (1: ApiException e),

        VolumeDescriptor statVolume(1:string domainName, 2:string volumeName)
             throws (1: ApiException e),

        list<VolumeDescriptor> listVolumes(1:string domainName)
             throws (1: ApiException e),

        i32 registerStream(
             1:string url,
             2:string http_method,
             3:list<string> volume_names,
             4:i32 sample_freq_seconds,
             5:i32 duration_seconds),

        list<fds_stream.StreamingRegistrationMsg> getStreamRegistrations(1:i32 ignore),
        void deregisterStream(1:i32 registration_id),

    i64 createSnapshotPolicy(1:snapshot.SnapshotPolicy policy)
             throws (1: ApiException e),

    list<snapshot.SnapshotPolicy> listPolicies(1:i64 unused)
             throws (1: ApiException e),

    void deleteSnapshotPolicy(1:i64 id)
             throws (1: ApiException e),

    void attachPolicy(1:i64 volumeId, 2:i64 policyId)
             throws (1: ApiException e),

    list<snapshot.SnapshotPolicy> listSnapshotPoliciesForVolume(1:i64 volumeId)
             throws (1: ApiException e),

    void detachPolicy(1:i64 volumeId, 2:i64 policyId)
             throws (1: ApiException e),

    list<string> listVolumesForPolicy(1:i64 policyId)
             throws (1: ApiException e),

    list<snapshot.Snapshot> listSnapshots(1:i64 volumeId)
             throws (1: ApiException e),

    void restore(1:i64 volumeId, 2:i64 snapshotId)
             throws (1: ApiException e),

    // Returns VolumeID of clone 
    i64 cloneVolume(1:i64 volumeId, 2:i64 fdsp_PolicyInfoId, 3:string clonedVolumeName)

}
