package com.formationds.nfs;

import com.formationds.index.FdsLuceneDirectory;
import com.formationds.util.FunctionWithExceptions;
import com.formationds.xdi.AsyncAm;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.collect.Sets;
import org.apache.log4j.Logger;
import org.apache.lucene.store.Directory;
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
import java.util.List;
import java.util.Optional;
import java.util.concurrent.TimeUnit;

public class BlockyVfs implements VirtualFileSystem {
    public static final String DOMAIN = "nfs";
    private static final Logger LOG = Logger.getLogger(BlockyVfs.class);
    private InodeMap inodeMap;
    private InodeAllocator allocator;
    private InodeIndex inodeIndex;
    private ExportResolver exportResolver;
    private SimpleIdMap idMap;
    private Chunker chunker;
    private WriteLocks writeLocks;

    public BlockyVfs(AsyncAm asyncAm, ExportResolver resolver) {
        inodeMap = new InodeMap(asyncAm, resolver);
        allocator = new InodeAllocator(asyncAm, resolver);
        this.exportResolver = resolver;
        FunctionWithExceptions<Long, Directory> indexFactory = new FunctionWithExceptions<Long, Directory>() {
            private Cache<Long, Directory> cache = CacheBuilder
                    .newBuilder()
                    .expireAfterAccess(10, TimeUnit.MINUTES)
                    .build();

            @Override
            public Directory apply(Long exportId) throws Exception {
                String volume = exportResolver.volumeName(exportId.intValue());
                int objectSize = exportResolver.objectSize(volume);
                return cache.get(exportId, () -> new FdsLuceneDirectory(asyncAm, volume, objectSize));
            }
        };
        inodeIndex = new InodeIndex(indexFactory, resolver);
        idMap = new SimpleIdMap();
        writeLocks = new WriteLocks();
        chunker = new Chunker(new ChunkyAm(asyncAm));
    }

    public BlockyVfs(InodeMap inodeMap,
                     InodeIndex inodeIndex,
                     InodeAllocator allocator,
                     ExportResolver exportResolver) {
        this.inodeMap = inodeMap;
        this.inodeIndex = inodeIndex;
        this.allocator = allocator;
        this.exportResolver = exportResolver;
        this.idMap = new SimpleIdMap();
    }

    @Override
    public int access(Inode inode, int mode) throws IOException {
        Optional<InodeMetadata> stat = inodeMap.stat(inode);
        if (!stat.isPresent()) {
            throw new NoEntException();
        }
        return mode;
    }

    @Override
    public Inode create(Inode parent, Stat.Type type, String name, Subject subject, int mode) throws IOException {
        Optional<InodeMetadata> parentMetadata = inodeMap.stat(parent);
        if (!parentMetadata.isPresent()) {
            throw new NoEntException();
        }

        Optional<InodeMetadata> childMetadata = inodeIndex.lookup(parent, name);
        if (childMetadata.isPresent()) {
            throw new ExistException();
        }

        if (!parentMetadata.get().isDirectory()) {
            throw new NotDirException();
        }

        String volume = exportResolver.volumeName(parent.exportIndex());
        InodeMetadata metadata = new InodeMetadata(type, subject, mode, allocator.allocate(volume), inodeMap.volumeId(parent))
                .withLink(inodeMap.fileId(parent), name);

        Inode inode = inodeMap.create(metadata);
        InodeMetadata updatedParent = parentMetadata.get()
                .withUpdatedAtime()
                .withUpdatedCtime()
                .withUpdatedMtime()
                .withIncrementedGeneration();
        inodeMap.update(updatedParent);
        inodeIndex.index(metadata.getVolumeId(), metadata, updatedParent);
        return inode;
    }


    @Override
    public FsStat getFsStat() throws IOException {
        return new FsStat(1024l * 1024l * 1024l * 1024l, 0, 0, 0);
    }

    @Override
    public Inode getRootInode() throws IOException {
        return InodeMap.ROOT;
    }

    @Override
    public Inode lookup(Inode parent, String path) throws IOException {
        if (InodeMap.isRoot(parent)) {
            String volumeName = path;
            if (!exportResolver.exists(volumeName)) {
                throw new NoEntException();
            }

            long fileId = exportResolver.exportId(volumeName);
            int exportId = (int) fileId;
            Subject unixRootUser = new Subject(
                    true,
                    Sets.newHashSet(new UidPrincipal(0), new GidPrincipal(0, true)),
                    Sets.newHashSet(),
                    Sets.newHashSet());

            InodeMetadata inodeMetadata = new InodeMetadata(Stat.Type.DIRECTORY, unixRootUser, 0755, fileId, exportId)
                    .withUpdatedAtime()
                    .withUpdatedCtime()
                    .withUpdatedMtime()
                    .withLink(InodeMetadata.fileId(parent), path);

            Optional<InodeMetadata> statResult = inodeMap.stat(inodeMetadata.asInode());
            if (!statResult.isPresent()) {
                LOG.debug("Creating export root for volume " + path);
                Inode inode = inodeMap.create(inodeMetadata);
                inodeIndex.index(exportId, inodeMetadata);
                return inode;
            }
        }

        Optional<InodeMetadata> oim = inodeIndex.lookup(parent, path);
        if (!oim.isPresent()) {
            throw new NoEntException();
        }

        return oim.get().asInode();
    }

    @Override
    public Inode link(Inode parent, Inode link, String path, Subject subject) throws IOException {
        Optional<InodeMetadata> parentMetadata = inodeMap.stat(parent);
        if (!parentMetadata.isPresent()) {
            throw new NoEntException();
        }
        if (!parentMetadata.get().isDirectory()) {
            throw new NotDirException();
        }

        Optional<InodeMetadata> linkMetadata = inodeMap.stat(link);
        if (!linkMetadata.isPresent()) {
            throw new NoEntException();
        }

        InodeMetadata updatedParentMetadata = parentMetadata.get()
                .withUpdatedCtime()
                .withUpdatedMtime()
                .withUpdatedAtime()
                .withIncrementedGeneration();

        InodeMetadata updatedLinkMetadata = linkMetadata.get().withLink(inodeMap.fileId(parent), path);

        inodeMap.update(updatedParentMetadata, updatedLinkMetadata);
        inodeIndex.index(updatedParentMetadata.getVolumeId(), updatedParentMetadata, updatedLinkMetadata);
        return link;
    }

    @Override
    public List<DirectoryEntry> list(Inode inode) throws IOException {
        Optional<InodeMetadata> stat = inodeMap.stat(inode);
        if (!stat.isPresent()) {
            throw new NoEntException();
        }

        return inodeIndex.list(inode);
    }

    @Override
    public Inode mkdir(Inode parent, String path, Subject subject, int mode) throws IOException {
        return create(parent, Stat.Type.DIRECTORY, path, subject, mode);
    }

    @Override
    public boolean move(Inode source, String oldName, Inode destination, String newName) throws IOException {
        Optional<InodeMetadata> sourceMetadata = inodeMap.stat(source);
        if (!sourceMetadata.isPresent()) {
            throw new NoEntException();
        }

        Optional<InodeMetadata> linkMetadata = inodeIndex.lookup(source, oldName);
        if (!linkMetadata.isPresent()) {
            throw new NoEntException();
        }

        Optional<InodeMetadata> destinationMetadata = inodeMap.stat(source);
        if (!destinationMetadata.isPresent()) {
            throw new NoEntException();
        }

        if (!destinationMetadata.get().isDirectory()) {
            throw new NotDirException();
        }

        InodeMetadata updatedSource = sourceMetadata.get()
                .withUpdatedAtime()
                .withUpdatedCtime()
                .withUpdatedMtime()
                .withIncrementedGeneration();

        InodeMetadata updatedLink = linkMetadata.get()
                .withUpdatedAtime()
                .withUpdatedCtime()
                .withUpdatedMtime()
                .withIncrementedGeneration()
                .withoutLink(inodeMap.fileId(source))
                .withLink(inodeMap.fileId(destination), newName);

        InodeMetadata updatedDestination = destinationMetadata.get()
                .withUpdatedAtime()
                .withUpdatedCtime()
                .withUpdatedMtime()
                .withIncrementedGeneration();

        inodeMap.update(updatedSource, updatedLink, updatedDestination);
        inodeIndex.index(updatedSource.getVolumeId(), updatedSource, updatedLink, updatedDestination);

        return true;
    }

    @Override
    public Inode parentOf(Inode inode) throws IOException {
        return null;
    }

    @Override
    public int read(Inode inode, byte[] data, long offset, int count) throws IOException {
        Optional<InodeMetadata> target = inodeMap.stat(inode);
        if (!target.isPresent()) {
            throw new NoEntException();
        }

        String volumeName = exportResolver.volumeName((int) target.get().getVolumeId());
        String blobName = InodeMap.blobName(inode);
        try {
            return chunker.read(DOMAIN, volumeName, blobName, exportResolver.objectSize(volumeName), data, offset, count);
        } catch (Exception e) {
            LOG.error("Error reading blob " + blobName, e);
            throw new IOException(e);
        }
    }

    @Override
    public String readlink(Inode inode) throws IOException {
        Optional<InodeMetadata> stat = inodeMap.stat(inode);
        if (!stat.isPresent()) {
            throw new NoEntException();
        }
        byte[] buf = new byte[(int) stat.get().getSize()];
        read(inode, buf, 0, buf.length);
        return new String(buf, "UTF-8");
    }

    @Override
    public void remove(Inode parentInode, String path) throws IOException {
        Optional<InodeMetadata> parent = inodeMap.stat(parentInode);
        if (!parent.isPresent()) {
            throw new NoEntException();
        }

        Optional<InodeMetadata> link = inodeIndex.lookup(parentInode, path);
        if (!link.isPresent()) {
            throw new NoEntException();
        }

        InodeMetadata updatedParent = parent.get()
                .withUpdatedCtime()
                .withUpdatedAtime()
                .withUpdatedMtime()
                .withIncrementedGeneration();
        InodeMetadata updatedLink = link.get()
                .withoutLink(inodeMap.fileId(parentInode));

        if (updatedLink.refCount() == 0) {
            inodeMap.remove(updatedLink.asInode());
            inodeIndex.remove(updatedLink.asInode());
        } else {
            inodeMap.update(updatedLink);
            inodeIndex.index(updatedLink.getVolumeId(), updatedLink);
        }
        inodeMap.update(updatedParent);
        inodeIndex.index(updatedParent.getVolumeId(), updatedParent);
    }

    @Override
    public Inode symlink(Inode parent, String path, String link, Subject subject, int mode) throws IOException {
        Optional<InodeMetadata> parentMd = inodeMap.stat(parent);
        if (!parentMd.isPresent()) {
            throw new NoEntException();
        }

        Inode inode = create(parent, Stat.Type.SYMLINK, path, subject, mode);
        byte[] linkValue = link.getBytes("UTF-8");
        write(inode, linkValue, 0, linkValue.length, StabilityLevel.FILE_SYNC);
        return inode;
    }

    @Override
    public WriteResult write(Inode inode, byte[] data, long offset, int count, StabilityLevel stabilityLevel) throws IOException {
        return writeLocks.guard(inode, () -> unguardedWrite(inode, data, offset, count, stabilityLevel));
    }

    private WriteResult unguardedWrite(Inode inode, byte[] data, long offset, int count, StabilityLevel stabilityLevel) throws IOException {
        Optional<InodeMetadata> stat = inodeMap.stat(inode);
        if (!stat.isPresent()) {
            throw new NoEntException();
        }

        String volumeName = exportResolver.volumeName(inode.exportIndex());
        String blobName = InodeMap.blobName(inode);

        try {
            long byteCount = stat.get().getSize();
            int length = Math.min(data.length, count);
            byteCount = Math.max(byteCount, offset + length);
            InodeMetadata im = stat.get()
                    .withUpdatedAtime()
                    .withUpdatedMtime()
                    .withUpdatedCtime()
                    .withUpdatedSize(byteCount);
            int objectSize = exportResolver.objectSize(volumeName);
            chunker.write(DOMAIN, volumeName, blobName, objectSize, data, offset, count, byteCount, im.asMap());
            inodeIndex.index(im.getVolumeId(), im);
            return new WriteResult(stabilityLevel, length);
        } catch (Exception e) {
            String message = "Error writing " + volumeName + "." + blobName;
            LOG.error(message, e);
            throw new IOException(message, e);
        }
    }

    @Override
    public void commit(Inode inode, long offset, int count) throws IOException {

    }

    @Override
    public Stat getattr(Inode inode) throws IOException {
        if (inodeMap.isRoot(inode)) {
            return InodeMap.ROOT_METADATA.asStat();
        }

        Optional<InodeMetadata> stat = inodeMap.stat(inode);
        if (!stat.isPresent()) {
            throw new NoEntException();
        }

        return stat.get().asStat();
    }

    @Override
    public void setattr(Inode inode, Stat stat) throws IOException {
        writeLocks.guard(inode, () -> {
            Optional<InodeMetadata> metadata = inodeMap.stat(inode);
            if (!metadata.isPresent()) {
                throw new NoEntException();
            }

            InodeMetadata updated = metadata.get().update(stat)
                    .withUpdatedAtime()
                    .withUpdatedCtime()
                    .withUpdatedMtime()
                    .withIncrementedGeneration();

            inodeMap.update(updated);
            inodeIndex.index(updated.getVolumeId(), updated);
            return 0;
        });
    }

    @Override
    public nfsace4[] getAcl(Inode inode) throws IOException {
        return new nfsace4[0];
    }

    @Override
    public void setAcl(Inode inode, nfsace4[] acl) throws IOException {

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
}
