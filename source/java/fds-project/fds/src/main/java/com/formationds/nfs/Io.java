package com.formationds.nfs;

import com.formationds.apis.ObjectOffset;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Map;

public interface Io {
    public <T> T mapMetadata(String domain, String volumeName, String blobName, MetadataMapper<T> mapper) throws IOException;

    public void mutateMetadata(String domain, String volume, String blobName, MetadataMutator mutator) throws IOException;

    public <T> T mapObject(String domain, String volume, String blobName, int objectSize, ObjectOffset objectOffset, ObjectMapper<T> objectMapper) throws IOException;

    public void mutateObjectAndMetadata(String domain, String volume, String blobName, int objectSize, ObjectOffset objectOffset, ObjectMutator mutator) throws IOException;

    public void mutateObjectAndMetadata(String domain, String volume, String blobName, int objectSize, ObjectOffset objectOffset, ByteBuffer byteBuffer, Map<String, String> metadata) throws IOException;

    public void deleteBlob(String domain, String volume, String blobName) throws IOException;
}
