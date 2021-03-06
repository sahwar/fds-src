package com.formationds.smoketest;

import com.formationds.apis.*;
import com.formationds.commons.Fds;
import com.formationds.hadoop.FdsFileSystem;
import com.formationds.nfs.*;
import com.formationds.protocol.*;
import com.formationds.sc.SvcState;
import com.formationds.sc.api.SvcAsyncAm;
import com.formationds.util.ByteBufferUtility;
import com.formationds.util.ConsumerWithException;
import com.formationds.util.IoFunction;
import com.formationds.xdi.*;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.net.HostAndPort;
import org.dcache.nfs.vfs.DirectoryEntry;
import org.dcache.nfs.vfs.Stat;
import org.joda.time.Duration;
import org.junit.*;

import javax.security.auth.Subject;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.*;
import java.util.concurrent.ExecutionException;
import java.util.stream.IntStream;

import static org.junit.Assert.*;


@Ignore
public class AsyncAmTest extends BaseAmTest {
    private static final boolean USE_SVC_IMPL = false;

    @Test
    public void testChunker() throws Exception {
        AmOps amOps = new AmOps(asyncAm);
        Chunker chunker = new Chunker(amOps);
        int length = (int) Math.round(OBJECT_SIZE * 10.5);
        byte[] bytes = new byte[length];
        new Random().nextBytes(bytes);
        chunker.write(domainName, volumeName, blobName, OBJECT_SIZE, bytes, 0, length, m -> m.put("foo", "bar"));
        byte[] read = new byte[length];
        chunker.read(domainName, volumeName, blobName, Long.MAX_VALUE, OBJECT_SIZE, read, 0, length);
        assertArrayEquals(bytes, read);
    }

    @Test
    public void testDmSupportsSmallObjects() throws Exception {
        AmOps amOps = new AmOps(asyncAm);
        amOps.writeObject(domainName, volumeName, blobName, new ObjectOffset(0), FdsObject.allocate(10, OBJECT_SIZE));
        amOps.commitObject(domainName, volumeName, blobName, new ObjectOffset(0));
        FdsObject fdsObject = amOps.readCompleteObject(domainName, volumeName, blobName, new ObjectOffset(0), OBJECT_SIZE);
        assertEquals(10, fdsObject.limit());
        assertEquals(OBJECT_SIZE, fdsObject.maxObjectSize());
    }

    @Test
    public void testUpdate() throws Exception {
        AmOps amOps = new AmOps(asyncAm);
        DeferredIoOps io = new DeferredIoOps(amOps, maxObjectSize);
        InodeIndex index = new SimpleInodeIndex(io, new MyExportResolver());
        InodeMetadata dir = new InodeMetadata(Stat.Type.DIRECTORY, new Subject(), 0, 3);
        InodeMetadata child = new InodeMetadata(Stat.Type.REGULAR, new Subject(), 0, 4)
                .withLink(dir.getFileId(), "panda");

        index.index(NFS_EXPORT_ID, false, dir);
        index.index(NFS_EXPORT_ID, false, child);
        child = child.withUpdatedAtime();
        index.index(NFS_EXPORT_ID, false, child);
        List<DirectoryEntry> list = index.list(dir, NFS_EXPORT_ID);
        assertEquals(1, list.size());
    }

    @Test
    public void testLookup() throws Exception {
        DeferredIoOps io = new DeferredIoOps(new AmOps(asyncAm), maxObjectSize);
        InodeIndex index = new SimpleInodeIndex(io, new MyExportResolver());
        InodeMetadata fooDir = new InodeMetadata(Stat.Type.DIRECTORY, new Subject(), 0, 2);
        InodeMetadata barDir = new InodeMetadata(Stat.Type.DIRECTORY, new Subject(), 0, 3);

        InodeMetadata child = new InodeMetadata(Stat.Type.REGULAR, new Subject(), 0, 4)
                .withLink(fooDir.getFileId(), "panda")
                .withLink(barDir.getFileId(), "lemur");

        index.index(NFS_EXPORT_ID, false, fooDir);
        index.index(NFS_EXPORT_ID, false, child);
        assertEquals(child, index.lookup(fooDir.asInode(NFS_EXPORT_ID), "panda").get());
        assertEquals(child, index.lookup(barDir.asInode(NFS_EXPORT_ID), "lemur").get());
        assertFalse(index.lookup(fooDir.asInode(NFS_EXPORT_ID), "baboon").isPresent());
        assertFalse(index.lookup(barDir.asInode(NFS_EXPORT_ID), "giraffe").isPresent());
        index.unlink(NFS_EXPORT_ID, 2, "panda");
        assertFalse(index.lookup(fooDir.asInode(NFS_EXPORT_ID), "panda").isPresent());
        assertTrue(index.lookup(barDir.asInode(NFS_EXPORT_ID), "lemur").isPresent());
    }

    @Test
    public void testListDirectory() throws Exception {
        DeferredIoOps io = new DeferredIoOps(new AmOps(asyncAm), maxObjectSize);
        InodeIndex index = new SimpleInodeIndex(io, new MyExportResolver());
        InodeMetadata fooDir = new InodeMetadata(Stat.Type.DIRECTORY, new Subject(), 0, 1);
        InodeMetadata barDir = new InodeMetadata(Stat.Type.DIRECTORY, new Subject(), 0, 2);

        InodeMetadata blue = new InodeMetadata(Stat.Type.REGULAR, new Subject(), 0, 3)
                .withLink(fooDir.getFileId(), "blue")
                .withLink(barDir.getFileId(), "blue");

        InodeMetadata red = new InodeMetadata(Stat.Type.REGULAR, new Subject(), 0, 4)
                .withLink(fooDir.getFileId(), "red");

        index.index(NFS_EXPORT_ID, true, fooDir);
        index.index(NFS_EXPORT_ID, true, barDir);
        index.index(NFS_EXPORT_ID, false, blue);
        index.index(NFS_EXPORT_ID, true, red);
        assertEquals(2, index.list(fooDir, NFS_EXPORT_ID).size());
        assertEquals(1, index.list(barDir, NFS_EXPORT_ID).size());
        assertEquals(0, index.list(blue, NFS_EXPORT_ID).size());
    }

    private class MyExportResolver implements ExportResolver {
        @Override
        public Collection<String> exportNames() {
            return Lists.newArrayList(volumeName);
        }

        @Override
        public int nfsExportId(String volumeName) {
            return NFS_EXPORT_ID;
        }

        @Override
        public String volumeName(int nfsExportId) {
            return volumeName;
        }

        @Override
        public boolean exists(String volumeName) {
            return true;
        }

        @Override
        public int objectSize(String volume) {
            return OBJECT_SIZE;
        }

        @Override
        public long maxVolumeCapacityInBytes(String volume) throws IOException {
            return 0;
        }

        @Override
        public void addVolumeDeleteEventHandler(ConsumerWithException<String> consumer) {

        }
    }

    @Test
    @Ignore
    public void testParallelCreate() throws Exception {
        IntStream.range(0, 10)
                .parallel()
                .map(i -> {
                    try {
                        asyncAm.updateBlobOnce(domainName, volumeName, Integer.toString(i),
                                1, ByteBuffer.allocate(100), 100, new ObjectOffset(1), new HashMap<>()).get();
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                    return 0;
                }).sum();

        IntStream.range(0, 10)
                .parallel()
                .map(i -> {
                    try {
                        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, Integer.toString(i)).get();
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                    return 0;
                }).sum();
    }

    @Test
    public void testOutOfOrderWrites() throws Exception {
        Random random = new Random();
        byte[] halfObject = new byte[OBJECT_SIZE / 2];
        byte[] fullObject = new byte[OBJECT_SIZE];
        random.nextBytes(halfObject);
        random.nextBytes(fullObject);
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, ByteBuffer.wrap(halfObject), OBJECT_SIZE / 2, new ObjectOffset(0), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, ByteBuffer.wrap(halfObject), OBJECT_SIZE / 2, new ObjectOffset(1), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 0, ByteBuffer.wrap(fullObject), OBJECT_SIZE, new ObjectOffset(1), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 0, ByteBuffer.wrap(fullObject), OBJECT_SIZE, new ObjectOffset(0), new HashMap<>()).get();
        for (int i = 0; i < 100; i++) {
            ByteBuffer byteBuffer = asyncAm.getBlob(domainName, volumeName, blobName, OBJECT_SIZE, new ObjectOffset(0)).get();
            assertEquals(OBJECT_SIZE, byteBuffer.remaining());
            byte[] readBuf = new byte[OBJECT_SIZE];
            byteBuffer.get(readBuf);
            assertArrayEquals(fullObject, readBuf);
        }
    }

    @Test
    public void testVolumeMetadata() throws Exception {
        Map<String, String> metadata = asyncAm.getVolumeMetadata(domainName, volumeName).get();
        assertEquals(0, metadata.size());
        metadata.put("hello", "world");
        asyncAm.setVolumeMetadata(domainName, volumeName, metadata).get();
        metadata = asyncAm.getVolumeMetadata(domainName, volumeName).get();
        assertEquals(1, metadata.size());
        assertEquals("world", metadata.get("hello"));
    }

    @Test
    public void testAsyncStreamerWritesOneChunkOnly() throws Exception {
        AsyncStreamer asyncStreamer = new AsyncStreamer(asyncAm, new XdiConfigurationApi(configService));
        HashMap<String, String> map = new HashMap<>();
        map.put("hello", "world");
        OutputStream outputStream = asyncStreamer.openForWriting(domainName, volumeName, blobName, map);
        outputStream.write(new byte[OBJECT_SIZE]);
        outputStream.close();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals(OBJECT_SIZE, (long) blobDescriptor.getByteCount());
        assertEquals("world", blobDescriptor.getMetadata().get("hello"));
    }

    @Test
    public void testAsyncStreamer() throws Exception {
        AsyncStreamer asyncStreamer = new AsyncStreamer(asyncAm, new XdiConfigurationApi(configService));
        OutputStream outputStream = asyncStreamer.openForWriting(domainName, volumeName, blobName, new HashMap<>());
        outputStream.write(new byte[42]);
        outputStream.write(new byte[42]);
        outputStream.close();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals(84, (long) blobDescriptor.getByteCount());
    }

    @Test
    public void testVolumeContents() throws Exception {
        List<BlobDescriptor> contents = asyncAm.volumeContents(domainName,
                volumeName,
                Integer.MAX_VALUE,
                0,
                "",
                PatternSemantics.PCRE,
                "",
                BlobListOrder.UNSPECIFIED,
                false).get().getBlobs();
        assertEquals(0, contents.size());
        Map<String, String> metadata = new HashMap<>();
        metadata.put("hello", "world");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        contents = asyncAm.volumeContents(domainName,
                volumeName,
                Integer.MAX_VALUE,
                0,
                "",
                PatternSemantics.PCRE,
                "",
                BlobListOrder.UNSPECIFIED,
                false).get().getBlobs();
        assertEquals(1, contents.size());
    }

    @Test
    public void testVolumeContentsFilter() throws Exception {
        asyncAm.updateBlobOnce(domainName, volumeName, "/", 1, ByteBuffer.allocate(0), 0, new ObjectOffset(0), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, "/panda", 1, ByteBuffer.allocate(0), 0, new ObjectOffset(0), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, "/panda/foo", 1, ByteBuffer.allocate(0), 0, new ObjectOffset(0), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, "/panda/foo/bar", 1, ByteBuffer.allocate(0), 0, new ObjectOffset(0), new HashMap<>()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, "/panda/foo/bar/hello", 1, ByteBuffer.allocate(0), 0, new ObjectOffset(0), new HashMap<>()).get();
        String filter = "^/[^/]+$";
        List<BlobDescriptor> descriptors = asyncAm.volumeContents(domainName,
                volumeName,
                Integer.MAX_VALUE,
                0,
                filter,
                PatternSemantics.PCRE,
                "",
                BlobListOrder.UNSPECIFIED,
                false).get().getBlobs();
        assertEquals(1, descriptors.size());
    }

    @Test
    @Ignore
    public void testListVolumeContents() throws Exception {
        List<BlobDescriptor> descriptors = asyncAm.volumeContents(domainName,
                "panda",
                Integer.MAX_VALUE,
                0,
                "",
                PatternSemantics.PCRE,
                "",
                BlobListOrder.UNSPECIFIED,
                false).get().getBlobs();
        for (BlobDescriptor descriptor : descriptors) {
            System.out.println(descriptor.getName());
        }
    }

    @Test
    public void testDeleteBlob() throws Exception {
        Map<String, String> metadata = new HashMap<>();
        metadata.put("hello", "world");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals("world", blobDescriptor.getMetadata().get("hello"));
        asyncAm.deleteBlob(domainName, volumeName, blobName).get();
        try {
            asyncAm.statBlob(domainName, volumeName, blobName).get();
            fail("Should have gotten an ExecutionException");
        } catch (ExecutionException e) {
            ApiException apiException = (ApiException) e.getCause();
            assertEquals(ErrorCode.MISSING_RESOURCE, apiException.getErrorCode());
        }
    }

    @Test
    public void testStatVolume() throws Exception {
        VolumeStatus volumeStatus = asyncAm.volumeStatus(domainName, volumeName).get();
        assertEquals(0, volumeStatus.getBlobCount());
    }

    @Test
    public void testUpdateMetadata() throws Exception {
        Map<String, String> metadata = new HashMap<>();
        metadata.put("hello", "world");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals("world", blobDescriptor.getMetadata().get("hello"));

        metadata.put("hello", "panda");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals("panda", blobDescriptor.getMetadata().get("hello"));
        assertEquals(1, blobDescriptor.getMetadataSize());
    }

    @Test
    public void testUpdateMetadataOnMissingBlob() throws Exception {
        Map<String, String> metadata = new HashMap<>();
        metadata.put("hello", "world");
        TxDescriptor tx = asyncAm.startBlobTx(domainName, volumeName, blobName, 0).get();
        asyncAm.updateMetadata(domainName, volumeName, blobName, tx, metadata).get();
        asyncAm.commitBlobTx(domainName, volumeName, blobName, tx).get();
        BlobDescriptor bd = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals("world", bd.getMetadata().get("hello"));
    }

    @Test
    public void testTransactions() throws Exception {
        // Note: explicit casts added to workaround issues with type inference in Eclipse compiler
        asyncAm.startBlobTx(domainName, volumeName, blobName, 1)
                .thenCompose(tx -> asyncAm.updateBlob(domainName, volumeName, blobName, tx, bigObject, OBJECT_SIZE, new ObjectOffset(0), false).thenApply(x -> tx))
                .thenCompose(tx -> asyncAm.updateBlob(domainName, volumeName, blobName, (TxDescriptor) tx, smallObject, smallObjectLength, new ObjectOffset(1), true).thenApply(x -> tx))
                .thenCompose(tx -> asyncAm.commitBlobTx(domainName, volumeName, blobName, (TxDescriptor) tx))
                .thenCompose(x -> asyncAm.statBlob(domainName, volumeName, blobName))
                .thenAccept(bd -> assertEquals(OBJECT_SIZE + smallObjectLength, bd.getByteCount()))
                .get();
    }

    @Test
    public void testTransactionalMetadataUpdate() throws Exception {
        Map<String, String> metadata = new HashMap<>();
        metadata.put("hello", "world");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        TxDescriptor tx = asyncAm.startBlobTx(domainName, volumeName, blobName, 1).get();
        BlobDescriptor bd1 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get();
        assertEquals("world", bd1.getMetadata().get("hello"));

        metadata.put("animal", "panda");
        asyncAm.updateMetadata(domainName, volumeName, blobName, tx, metadata).get();
        asyncAm.commitBlobTx(domainName, volumeName, blobName, tx).get();
        BlobDescriptor bd2 = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals(2, bd2.getMetadataSize());
        assertEquals("panda", bd2.getMetadata().get("animal"));
        assertEquals(smallObjectLength, bd2.getByteCount());
    }

    @Test
    public void testBlobRename() throws Exception {
        String blobName = UUID.randomUUID().toString();
        String blobName2 = UUID.randomUUID().toString();
        Map<String, String> metadata = new HashMap<>();
        metadata.put("clothing", "boot");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, bigObject, OBJECT_SIZE, new ObjectOffset(0), metadata).get();
        BlobDescriptor bd1 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get();
        assertEquals("boot", bd1.getMetadata().get("clothing"));

        // Rename the blob
        asyncAm.renameBlob(domainName, volumeName, blobName, blobName2).get();

        // The old one should be gone
        try {
            BlobDescriptor descriptor = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get();
            fail("Should have gotten an ExecutionException");
        } catch (ExecutionException e) {
            ApiException apiException = (ApiException) e.getCause();
            assertEquals(ErrorCode.MISSING_RESOURCE, apiException.getErrorCode());
        }

        // The new identical to the old
        BlobDescriptor bd2 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName2).get();
        assertEquals(OBJECT_SIZE, bd2.getByteCount());
        assertEquals("boot", bd2.getMetadata().get("clothing"));

        ByteBuffer byteBuffer = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName2, OBJECT_SIZE, new ObjectOffset(0)).get();
        byte[] result = new byte[OBJECT_SIZE];
        byteBuffer.get(result);
        byte[] bigObjectByteArray = new byte[OBJECT_SIZE];
        bigObject.get(bigObjectByteArray);
        assertArrayEquals(result, bigObjectByteArray);
    }

    @Test
    public void testBlobRenameTrucate() throws Exception {
        String blobName = UUID.randomUUID().toString();
        String blobName2 = UUID.randomUUID().toString();

        Map<String, String> metadata = new HashMap<>();
        metadata.put("clothing", "hat");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), metadata).get();
        BlobDescriptor bd1 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get();
        assertEquals("hat", bd1.getMetadata().get("clothing"));

        metadata = new HashMap<>();
        metadata.put("clothing", "boot");
        asyncAm.updateBlobOnce(domainName, volumeName, blobName2, 1, bigObject, OBJECT_SIZE, new ObjectOffset(0), metadata).get();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName2, 1, bigObject, OBJECT_SIZE, new ObjectOffset(1), metadata).get();
        BlobDescriptor bd2 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName2).get();
        assertEquals("boot", bd2.getMetadata().get("clothing"));

        // Rename the blob
        asyncAm.renameBlob(domainName, volumeName, blobName, blobName2).get();

        // The old one should be gone
        try {
            asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get();
            fail("Should have gotten an ExecutionException");
        } catch (ExecutionException e) {
            ApiException apiException = (ApiException) e.getCause();
            assertEquals(ErrorCode.MISSING_RESOURCE, apiException.getErrorCode());
        }

        // The new identical to the old (and truncated)
        BlobDescriptor bd3 = asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName2).get();
        assertEquals(smallObjectLength, bd3.getByteCount());
        assertEquals("hat", bd3.getMetadata().get("clothing"));

        ByteBuffer byteBuffer = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName2, smallObjectLength, new ObjectOffset(0)).get();
        byte[] result = new byte[smallObjectLength];
        byteBuffer.get(result);
        byte[] smallObjectByteArray = new byte[smallObjectLength];
        smallObject.get(smallObjectByteArray);
        assertArrayEquals(result, smallObjectByteArray);
    }

    @Test
    public void testMultipleAsyncUpdates() throws Exception {
        String blobName = UUID.randomUUID().toString();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, bigObject, OBJECT_SIZE, new ObjectOffset(0), Maps.newHashMap()).get();
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(1), Maps.newHashMap()).get();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals(OBJECT_SIZE + smallObjectLength, blobDescriptor.getByteCount());

        byte[] result = new byte[OBJECT_SIZE];
        ByteBuffer byteBuffer = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName, OBJECT_SIZE, new ObjectOffset(0)).get();
        byteBuffer.get(result);
        byte[] bigObjectByteArray = new byte[OBJECT_SIZE];
        bigObject.get(bigObjectByteArray);
        assertArrayEquals(result, bigObjectByteArray);

        byte[] result1 = new byte[smallObjectLength];
        ByteBuffer bb1 = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName, smallObjectLength, new ObjectOffset(1)).get();
        bb1.get(result1);
        byte[] smallObjectByteArray = new byte[smallObjectLength];
        smallObject.get(smallObjectByteArray);
        assertArrayEquals(smallObjectByteArray, result1);

    }

    @Test
    public void testAsyncUpdateOnce() throws Exception {
        asyncAm.updateBlobOnce(domainName, volumeName, blobName, 1, smallObject, smallObjectLength, new ObjectOffset(0), Maps.newHashMap()).get();
        BlobDescriptor blobDescriptor = asyncAm.statBlob(domainName, volumeName, blobName).get();
        assertEquals(smallObjectLength, blobDescriptor.getByteCount());
    }

    @Test //done 6
    public void testAsyncGetNonExistentBlob() throws Exception {
        assertFdsError(ErrorCode.MISSING_RESOURCE,
                () -> asyncAm.statBlob(domainName, volumeName, blobName).get());
    }

    @Test
    public void testStatAndUpdateBlobData() throws ExecutionException, InterruptedException {
        String blobName = "key";
        byte[] buf = new byte[10];
        new Random().nextBytes(buf);

        asyncAm.updateBlobOnce(FdsFileSystem.DOMAIN, volumeName, blobName, 1, ByteBuffer.wrap(buf), buf.length, new ObjectOffset(0), new HashMap<>()).get();
        ByteBuffer bb = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName, Integer.MAX_VALUE, new ObjectOffset(0)).get();
        byte[] result = new byte[buf.length];
        bb.get(result);
        assertArrayEquals(buf, result);
        assertEquals(buf.length, (int) asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get().getByteCount());

        buf = new byte[42];
        new Random().nextBytes(buf);
        TxDescriptor tx = asyncAm.startBlobTx(FdsFileSystem.DOMAIN, volumeName, blobName, 1).get();
        asyncAm.updateBlob(FdsFileSystem.DOMAIN, volumeName, blobName, tx, ByteBuffer.wrap(buf), buf.length, new ObjectOffset(0), false).get();
        asyncAm.commitBlobTx(FdsFileSystem.DOMAIN, volumeName, blobName, tx).get();

        bb = asyncAm.getBlob(FdsFileSystem.DOMAIN, volumeName, blobName, Integer.MAX_VALUE, new ObjectOffset(0)).get();
        result = new byte[buf.length];
        bb.get(result);
        assertArrayEquals(buf, result);
        assertEquals(buf.length, (int) asyncAm.statBlob(FdsFileSystem.DOMAIN, volumeName, blobName).get().getByteCount());
    }

    @Test
    public void testVolumeContentsOnMissingVolume() throws Exception {
        assertFdsError(ErrorCode.MISSING_RESOURCE,
                () -> asyncAm.volumeContents(FdsFileSystem.DOMAIN,
                        "nonExistingVolume",
                        Integer.MAX_VALUE,
                        0,
                        "",
                        PatternSemantics.PCRE,
                        "",
                        BlobListOrder.LEXICOGRAPHIC,
                        false).get());
    }

    private static ConfigurationService.Iface configService;
    private String volumeName;
    private static final int OBJECT_SIZE = 1024 * 1024 * 2;
    public static final int NFS_EXPORT_ID = 42;
    private static IoFunction<String, Integer> maxObjectSize = x -> OBJECT_SIZE;
    private static SvcState svc;
    private Counters counters;
    private static final int MY_AM_RESPONSE_PORT = 9881;
    private static XdiClientFactory xdiCf;
    private static AsyncAm asyncAm;

    private String domainName;
    private String blobName;
    private ByteBuffer bigObject;
    private int smallObjectLength;
    private ByteBuffer smallObject;

    @BeforeClass
    public static void setUpOnce() throws Exception {
        xdiCf = new XdiClientFactory();
        configService = xdiCf.remoteOmService(Fds.getFdsHost(), 9090);

        if (USE_SVC_IMPL) {
            HostAndPort self = HostAndPort.fromParts(Fds.getFdsHost(), 10293);
            HostAndPort om = HostAndPort.fromParts(Fds.getFdsHost(), 7004);
            svc = new SvcState(self, om, 18923L);
            svc.openAndRegister();
            asyncAm = new SvcAsyncAm(svc);
        } else {
            asyncAm = new RealAsyncAm(Fds.getFdsHost(), 8899, MY_AM_RESPONSE_PORT, Duration.standardSeconds(30));
            asyncAm.start();
        }

    }

    @AfterClass
    public static void tearDownOnce() throws Exception {
        if (svc != null) {
            svc.close();
        }
    }

    @Before
    public void setUp() throws Exception {
        counters = new Counters();
        volumeName = UUID.randomUUID().toString();
        domainName = UUID.randomUUID().toString();
        blobName = UUID.randomUUID().toString();
        bigObject = ByteBufferUtility.randomBytes(OBJECT_SIZE);
        smallObjectLength = 42;
        smallObject = ByteBufferUtility.randomBytes(smallObjectLength);

        configService.createVolume(domainName, volumeName, new VolumeSettings(OBJECT_SIZE, VolumeType.OBJECT, 0, 0, MediaPolicy.HDD_ONLY), 0);
    }
}
