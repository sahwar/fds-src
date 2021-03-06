package com.formationds.nfs;

import com.formationds.apis.ObjectOffset;
import com.formationds.apis.TxDescriptor;
import com.formationds.commons.util.SupplierWithExceptions;
import com.formationds.protocol.*;
import com.formationds.util.IoConsumer;
import com.formationds.util.IoFunction;
import com.formationds.xdi.AsyncAm;
import com.formationds.xdi.RecoverableException;
import com.google.common.collect.Sets;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.*;
import java.util.stream.Collectors;

import static com.formationds.hadoop.FdsFileSystem.unwindExceptions;

public class AmOps implements IoOps {
    private static final Logger LOG = LogManager.getLogger(AmOps.class);
    private AsyncAm asyncAm;

    public static final ErrorCode[] RECOVERABLE_ERRORS = new ErrorCode[]{
            ErrorCode.INTERNAL_SERVER_ERROR,
            ErrorCode.BAD_REQUEST,
            ErrorCode.SERVICE_NOT_READY,
            ErrorCode.SERVICE_SHUTTING_DOWN,
            ErrorCode.TIMEOUT};

    public AmOps(AsyncAm asyncAm) {
        this.asyncAm = asyncAm;
    }

    @Override
    public Optional<Map<String, String>> readMetadata(String domain, String volumeName, String blobName) throws IOException {
        String operationName = "AM.readMetadata";
        String description = "volume=" + volumeName + ", blobName=" + blobName;

        WorkUnit<Optional<Map<String, String>>> unit = new WorkUnit<Optional<Map<String, String>>>(operationName, description) {
            @Override
            public Optional<Map<String, String>> supply() throws Exception {
                BlobDescriptor blobDescriptor = asyncAm.statBlob(domain, volumeName, blobName).get();
                return Optional.of(new HashMap<String, String>(blobDescriptor.getMetadata()));
            }
        };

        return handleExceptions(new ErrorCode[]{ErrorCode.MISSING_RESOURCE}, ec -> {
            LOG.debug("Blob not found, volume=" + volumeName + ", blobName=" + blobName);
            return Optional.empty();
        }, unit);
    }

    @Override
    public void writeMetadata(String domain, String volume, String blobName, Map<String, String> metadata) throws IOException {
        String operationName = "AM.writeMetadata";
        String description = "volume=" + volume + ", blobName=" + blobName + ", fieldCount=" + metadata.size();
        WorkUnit<Void> workUnit = new WorkUnit<Void>(operationName, description) {
            @Override
            public Void supply() throws Exception {
                TxDescriptor tx = asyncAm.startBlobTx(domain, volume, blobName, 0).get();
                asyncAm.updateMetadata(domain, volume, blobName, tx, metadata).get();
                asyncAm.commitBlobTx(domain, volume, blobName, tx).get();
                return null;
            }
        };

        handleExceptions(new ErrorCode[0], ec -> null, workUnit);
    }

    @Override
    public void commitMetadata(String domain, String volumeName, String blobName) throws IOException {

    }

    @Override
    public FdsObject readCompleteObject(String domain, String volumeName, String blobName, ObjectOffset objectOffset, int maxObjectSize) throws IOException {
        String operation = "AM.readObject";
        String description = "volume=" + volumeName + ", blobName=" + blobName + ", objectOffset=" + objectOffset.getValue();

        WorkUnit<FdsObject> unit = new WorkUnit<FdsObject>(operation, description) {
            @Override
            public FdsObject supply() throws Exception {
                ByteBuffer byteBuffer = asyncAm.getBlob(domain, volumeName, blobName, maxObjectSize, objectOffset).get();
                return new FdsObject(byteBuffer, maxObjectSize);
            }
        };

        return handleExceptions(
                new ErrorCode[]{ErrorCode.MISSING_RESOURCE},
                ec -> new FdsObject(ByteBuffer.wrap(new byte[0]), maxObjectSize),
                unit);
    }

    @Override
    public void writeObject(String domain, String volume, String blobName, ObjectOffset objectOffset, FdsObject fdsObject) throws IOException {
        int length = fdsObject.limit();
        String description = "AM.writeObject";
        String argumentsSummary = "volume=" + volume + ", blobName=" + blobName + ", objectOffset=" + objectOffset.getValue() + ", buf=" + length;

        WorkUnit<Void> unit = new WorkUnit<Void>(description, argumentsSummary) {
            @Override
            public Void supply() throws Exception {
                TxDescriptor tx = asyncAm.startBlobTx(domain, volume, blobName, 0).get();
                asyncAm.updateBlob(domain, volume, blobName, tx, fdsObject.asByteBuffer(), fdsObject.limit(), objectOffset, false).get();
                asyncAm.commitBlobTx(domain, volume, blobName, tx).get();
                return null;
            }
        };

        handleExceptions(new ErrorCode[0], c -> null, unit);
    }

    @Override
    public void commitObject(String domain, String volumeName, String blobName, ObjectOffset objectOffset) throws IOException {

    }

    @Override
    public Collection<BlobMetadata> scan(String domain, String volume, String blobNamePrefix) throws IOException {
        String operationName = "AM.volumeContents";
        String description = "volume=" + volume + ", prefix=" + blobNamePrefix;

        WorkUnit<List<BlobMetadata>> unit = new WorkUnit<List<BlobMetadata>>(operationName, description) {
            @Override
            public List<BlobMetadata> supply() throws Exception {
                List<BlobMetadata> result = asyncAm.volumeContents(domain, volume, Integer.MAX_VALUE, 0, blobNamePrefix, PatternSemantics.PREFIX, null, BlobListOrder.UNSPECIFIED, false).get()
                        .getBlobs()
                        .stream()
                        .map(bd -> new BlobMetadata(bd.getName(), bd.getMetadata()))
                        .collect(Collectors.toList());
                return result;
            }
        };

        return handleExceptions(new ErrorCode[0], c -> null, unit);
    }

    @Override
    public void commitAll() throws IOException {

    }

    @Override
    public void commitAll(String domain, String volumeName) throws IOException {

    }

    @Override
    public void onVolumeDeletion(String domain, String volumeName) throws IOException {

    }

    @Override
    public void addCommitListener(IoConsumer<MetaKey> listener) {

    }

    @Override
    public void deleteBlob(String domain, String volume, String blobName) throws IOException {
        String operationName = "AM.deleteBlob";
        String description = "volume=" + volume + ", blobName=" + blobName;

        WorkUnit<Void> unit = new WorkUnit<Void>(operationName, description) {
            @Override
            public Void supply() throws Exception {
                asyncAm.deleteBlob(domain, volume, blobName).get();
                return null;
            }
        };

        handleExceptions(new ErrorCode[]{ErrorCode.MISSING_RESOURCE}, c -> null, unit);
    }

    @Override
    public void renameBlob(String domain, String volumeName, String oldName, String newName) throws IOException {
        String operationName = "AM.renameBlob";
        String description = "volume=" + volumeName + ", oldName=" + oldName + ", newName=" + newName;

        WorkUnit<Void> unit = new WorkUnit<Void>(operationName, description) {
            @Override
            public Void supply() throws Exception {
                asyncAm.renameBlob(domain, volumeName, oldName, newName);
                return null;
            }
        };

        handleExceptions(new ErrorCode[0], c -> null, unit);
    }


    private abstract class WorkUnit<T> implements SupplierWithExceptions<T> {
        String operationName;
        String description;

        public WorkUnit(String operationName, String description) {
            this.operationName = operationName;
            this.description = description;
        }
    }

    private <T> T handleExceptions(
            ErrorCode[] explicitelyHandledErrors,
            IoFunction<ErrorCode, T> errorHandler,
            WorkUnit<T> workUnit) throws IOException {

        try {
            T result = unwindExceptions(workUnit);
            LOG.debug(workUnit.operationName + ", " + workUnit.description);
            return result;
        } catch (ApiException e) {
            if (Sets.newHashSet(RECOVERABLE_ERRORS).contains(e.getErrorCode())) {
                LOG.warn(workUnit.operationName + " failed with error code " + e.getErrorCode() + " (recoverable), " + workUnit.description, e);
                throw new RecoverableException();
            } else if (Sets.newHashSet(explicitelyHandledErrors).contains(e.getErrorCode())) {
                return errorHandler.apply(e.getErrorCode());
            } else {
                LOG.error(workUnit.operationName + " failed  with error code " + e.getErrorCode() + ", " + workUnit.description, e);
                throw new IOException(e);
            }
        } catch (Exception e) {
            LOG.error(workUnit.operationName + " failed, " + workUnit.description, e);
            throw new IOException(e);
        }
    }
}
