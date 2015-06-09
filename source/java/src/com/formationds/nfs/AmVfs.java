package com.formationds.nfs;

import com.formationds.apis.ObjectOffset;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.BlobDescriptor;
import com.formationds.protocol.BlobListOrder;
import com.formationds.protocol.ErrorCode;
import com.formationds.xdi.AsyncAm;
import com.formationds.xdi.XdiConfigurationApi;
import com.google.common.collect.Sets;
import org.apache.log4j.Logger;
import org.dcache.auth.GidPrincipal;
import org.dcache.auth.UidPrincipal;
import org.dcache.nfs.status.ExistException;
import org.dcache.nfs.status.NoEntException;
import org.dcache.nfs.status.NotDirException;
import org.dcache.nfs.v4.NfsIdMapping;
import org.dcache.nfs.v4.SimpleIdMap;
import org.dcache.nfs.v4.xdr.nfsace4;
import org.dcache.nfs.vfs.*;

import javax.security.auth.Subject;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.stream.Collectors;

import static com.formationds.hadoop.FdsFileSystem.*;

public class AmVfs implements VirtualFileSystem {
    private static final Logger LOG = Logger.getLogger(AmVfs.class);
    private AsyncAm asyncAm;
    private XdiConfigurationApi config;
    private ExportResolver resolver;
    private static final String DOMAIN = "nfs";
    private SimpleIdMap idMap;

    public AmVfs(AsyncAm asyncAm, XdiConfigurationApi config, ExportResolver resolver) {
        this.asyncAm = asyncAm;
        this.config = config;
        this.resolver = resolver;
        idMap = new SimpleIdMap();
    }

    @Override
    public int access(Inode inode, int mode) throws IOException {
        NfsPath path = new NfsPath(inode);
        Optional<NfsAttributes> attributes = load(path);
        if (!attributes.isPresent()) {
            throw new NoEntException();
        }
        return mode;
    }

    @Override
    public Inode create(Inode inode, Stat.Type type, String name, Subject subject, int mode) throws IOException {
        NfsPath parent = new NfsPath(inode);
        if (!load(parent).isPresent()) {
            throw new NoEntException();
        }

        NfsPath childPath = new NfsPath(parent, name);
        if (load(childPath).isPresent()) {
            throw new ExistException();
        }

        incrementGeneration(parent);
        return createInode(type, subject, mode, childPath);
    }

    @Override
    public FsStat getFsStat() throws IOException {
        return new FsStat(1024 * 1024 * 10, 0, 0, 0);
    }

    @Override
    public Inode getRootInode() throws IOException {
        return new Inode(new FileHandle(0, 0, Stat.S_IFDIR, new byte[0]));
    }

    @Override
    public Inode lookup(Inode inode, String name) throws IOException {
        NfsPath parent = new NfsPath(inode);
        NfsPath child = new NfsPath(parent, name);
        Optional<NfsAttributes> attributes = load(child);
        if (!attributes.isPresent()) {
            throw new NoEntException();
        }
        return child.asInode(attributes.get().getType(), resolver);
    }

    @Override
    public Inode link(Inode inode, Inode inode1, String s, Subject subject) throws IOException {
        throw new RuntimeException();
    }

    @Override
    public List<DirectoryEntry> list(Inode inode) throws IOException {
        NfsPath path = new NfsPath(inode);
        Optional<NfsAttributes> attributes = load(path);
        if (!attributes.isPresent()) {
            throw new NoEntException();
        }
        if (!attributes.get().getType().equals(Stat.Type.DIRECTORY)) {
            throw new NotDirException();
        }

        StringBuilder filter = new StringBuilder();
        filter.append(path.blobName());

        if (!path.blobName().endsWith("/")) {
            filter.append("/");
        }
        filter.append("[^/]+$");

        try {
            List<BlobDescriptor> blobDescriptors = unwindExceptions(() ->
                    asyncAm.volumeContents(DOMAIN,
                            path.getVolume(),
                            Integer.MAX_VALUE,
                            0,
                            filter.toString(),
                            BlobListOrder.UNSPECIFIED,
                            false).
                            get());
            List<DirectoryEntry> list = blobDescriptors
                    .stream()
                    .map(bd -> {
                        NfsPath childPath = new NfsPath(path, bd.getName());
                        NfsAttributes childAttributes = new NfsAttributes(bd);
                        return new DirectoryEntry(
                                childPath.fileName(),
                                childPath.asInode(childAttributes.getType(), resolver),
                                childAttributes.asStat());
                    })
                    .collect(Collectors.toList());
            return list;
        } catch (Exception e) {
            throw new IOException(e);
        }
    }

    @Override
    public Inode mkdir(Inode parentInode, String name, Subject subject, int mode) throws IOException {
        NfsPath parent = new NfsPath(parentInode);
        NfsPath path = new NfsPath(parent, name);
        if (load(path).isPresent()) {
            throw new ExistException();
        }

        incrementGeneration(parent);
        return createInode(Stat.Type.DIRECTORY, subject, mode, path);
    }

    @Override
    public boolean move(Inode inode, String s, Inode inode1, String s1) throws IOException {
        return false;
    }

    @Override
    public Inode parentOf(Inode inode) throws IOException {
        return null;
    }

    @Override
    public int read(Inode inode, byte[] bytes, long offset, int len) throws IOException {
        NfsPath path = new NfsPath(inode);
        Optional<NfsAttributes> metadata = load(path);
        if (!metadata.isPresent()) {
            throw new NoEntException();
        }

        if (! metadata.get().getType().equals(Stat.Type.REGULAR)) {
            throw new NoEntException();
        }
        try {
            ByteBuffer byteBuffer = asyncAm.getBlob(DOMAIN, path.getVolume(), path.blobName(), (int) offset + len, new ObjectOffset(0)).get();
            byteBuffer.get(bytes, (int) offset, len);
            return len;
        } catch (Exception e) {
            logError("getBlob()", path, e);
            throw new IOException(e);
        }
    }

    @Override
    public String readlink(Inode inode) throws IOException {
        return null;
    }

    @Override
    public void remove(Inode parent, String name) throws IOException {
        NfsPath parentPath = new NfsPath(parent);
        NfsPath path = new NfsPath(parentPath, name);
        Optional<NfsAttributes> attrs = load(path);
        if (!attrs.isPresent()) {
            throw new NoEntException();
        }


        try {
            incrementGeneration(parentPath);
            unwindExceptions(() -> asyncAm.deleteBlob(DOMAIN, path.getVolume(), path.blobName()).get());
        } catch (Exception e) {
            logError("deleteBlob()", path, e);
        }
    }

    @Override
    public Inode symlink(Inode inode, String s, String s1, Subject subject, int i) throws IOException {
        return null;
    }

    @Override
    public WriteResult write(Inode inode, byte[] data, long offset, int count, StabilityLevel stabilityLevel) throws IOException {
        NfsPath path = new NfsPath(inode);
        Optional<NfsAttributes> attrs = load(path);
        if (!attrs.isPresent()) {
            throw new NoEntException();
        }

        ByteBuffer byteBuffer = ByteBuffer.allocate((int) offset + count);
        byteBuffer.put(data, (int) offset, count);
        try {
            asyncAm.updateBlobOnce(
                    DOMAIN,
                    path.getVolume(),
                    path.blobName(),
                    1,
                    ByteBuffer.wrap(data),
                    (int) offset + count,
                    new ObjectOffset(0),
                    attrs.get().asMetadata()).get();
            return new WriteResult(stabilityLevel, count);
        } catch (Exception e) {
            LOG.error("updateBlobOnce(), e");
            throw new IOException(e);
        }
    }

    @Override
    public void commit(Inode inode, long offset, int count) throws IOException {
        System.out.println("Commit");
    }

    @Override
    public Stat getattr(Inode inode) throws IOException {
        NfsPath path = new NfsPath(inode);
        Optional<NfsAttributes> attributes = load(path);
        if (!attributes.isPresent()) {
            throw new NoEntException();
        }
        return attributes.get().asStat();
    }

    @Override
    public void setattr(Inode inode, Stat stat) throws IOException {
        System.out.println(stat);
    }

    @Override
    public nfsace4[] getAcl(Inode inode) throws IOException {
        return new nfsace4[0];
    }

    @Override
    public void setAcl(Inode inode, nfsace4[] nfsace4s) throws IOException {

    }

    @Override
    public boolean hasIOLayout(Inode inode) throws IOException {
        return false;
    }

    @Override
    public AclCheckable getAclCheckable() {
        return null;
    }

    @Override
    public NfsIdMapping getIdMapper() {
        return idMap;
    }

    public void logError(String operation, NfsPath path, Exception e) {
        LOG.error(operation + " failed on blob [volume:" + path.getVolume() + "]" + path.blobName(), e);
    }

    private Optional<NfsAttributes> load(NfsPath path) throws IOException {
        if (path.isRoot()) {
            return Optional.of(new NfsAttributes(Stat.Type.DIRECTORY, new Subject(), Stat.S_IFDIR | 0755, 0, 0));
        }
        try {
            BlobDescriptor blobDescriptor = unwindExceptions(() -> asyncAm.statBlob(DOMAIN, path.getVolume(), path.blobName()).get());
            return Optional.of(new NfsAttributes(blobDescriptor));
        } catch (ApiException e) {
            if (e.getErrorCode().equals(ErrorCode.MISSING_RESOURCE)) {
                if (path.blobName().equals("/")) {
                    createExportRoot(path.getVolume());
                    return load(path);
                }
                return Optional.empty();
            } else {
                LOG.error("Error accessing [volume:" + path.getVolume() + "]" + path.blobName(), e);
                throw new IOException(e);
            }
        } catch (Exception e) {
            LOG.error("Error accessing [volume:" + path.getVolume() + "]" + path.blobName(), e);
            throw new IOException(e);
        }
    }

    private void incrementGeneration(NfsPath nfsPath) throws IOException {
        BlobDescriptor blobDescriptor = null;
        try {
            blobDescriptor = asyncAm.statBlob(DOMAIN, nfsPath.getVolume(), nfsPath.blobName()).get();
        } catch (Exception e) {
            throw new IOException(e);
        }
        NfsAttributes attributes = new NfsAttributes(blobDescriptor);
        attributes.incrementGeneration();
        updateMetadata(nfsPath, attributes);
    }

    public Inode createInode(Stat.Type type, Subject subject, int mode, NfsPath path) throws IOException {
        Inode childInode = path.asInode(type, resolver);
        NfsAttributes attributes = new NfsAttributes(type, subject, mode, nextFileId(), 0);

        try {
            updateMetadata(path, attributes);
        } catch (Exception e) {
            logError("updateBlobOnce()", path, e);
            throw new IOException(e);
        }
        return childInode;
    }

    public void updateMetadata(NfsPath path, NfsAttributes attributes) throws IOException {
        try {
            unwindExceptions(() -> asyncAm.updateBlobOnce(DOMAIN,
                    path.getVolume(),
                    path.blobName(),
                    1,
                    ByteBuffer.allocate(0),
                    0,
                    new ObjectOffset(0),
                    attributes.asMetadata())
                    .get());
        } catch (Exception e) {
            throw new IOException(e);
        }
    }

    private void createExportRoot(String volume) throws IOException {
        Subject unixRootUser = new Subject(
                true,
                Sets.newHashSet(new UidPrincipal(0), new GidPrincipal(0, true)),
                Sets.newHashSet(),
                Sets.newHashSet());

        createInode(Stat.Type.DIRECTORY, unixRootUser, 0755, new NfsPath(volume, "/"));
    }

    private long nextFileId() {
        return Math.abs(UUID.randomUUID().getLeastSignificantBits());
    }
}
