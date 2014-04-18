namespace cpp fds.xdi
namespace java com.formationds.xdi.shim

struct Uuid {
       1: required i64 low,
       2: required i64 high
}

struct VolumePolicy {
       1: required i32 objectSizeInBytes
}

struct VolumeDescriptor {
       1: required string name,
       2: required Uuid uuid,
       3: required i64 dateCreated,
       4: required VolumePolicy policy
}

struct VolumeStatus {
       1: required i64 blobCount
}

exception FdsException {
       1: string message;
}

struct BlobDescriptor {
       1: required string name,
       2: required i64 byteCount,
       3: required map<string, string> metadata
}

service AmShim {
        void createVolume(1:string domainName, 2:string volumeName, 3:VolumePolicy volumePolicy)
             throws (1: FdsException e),

        void deleteVolume(1:string domainName, 2:string volumeName)
             throws (1: FdsException e),

        VolumeDescriptor statVolume(1:string domainName, 2:string volumeName)
             throws (1: FdsException e),

        list<VolumeDescriptor> listVolumes(1:string domainName)
             throws (1: FdsException e),

        VolumeStatus volumeStatus(1:string domainName, 2:string volumeName)
             throws (1: FdsException e),

        list<BlobDescriptor> volumeContents(1:string domainName, 2:string volumeName, 3:i32 count, 4:i64 offset)
             throws (1: FdsException e),

        BlobDescriptor statBlob(1: string domainName, 2:string volumeName, 3:string blobName)
             throws (1: FdsException e),

        binary getBlob(1:string domainName, 2:string volumeName, 3:string blobName, 4:i32 length, 5:i64 offset)
             throws (1: FdsException e),

        Uuid startBlobTx(1:string domainName, 2:string volumeName, 3:string blobName)
             throws (1: FdsException e),

        void updateMetadata(1:string domainName, 2:string volumeName, 3:string blobName, 5:Uuid txUuid, 6:map<string, string> metadata)
             throws (1: FdsException e),

        void updateBlob(1:string domainName, 2:string volumeName, 3:string blobName, 4:Uuid txUuid, 5:binary bytes, 6:i32 length, 7:i64 offset)
             throws (1: FdsException e),

        void commit(1:Uuid txId)
             throws (1: FdsException e),

        void deleteBlob(1:string domainName, 2:string volumeName, 3:string blobName)
             throws (1: FdsException e)

}
