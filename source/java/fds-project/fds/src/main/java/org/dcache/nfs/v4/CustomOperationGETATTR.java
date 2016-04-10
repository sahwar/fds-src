package org.dcache.nfs.v4;

import com.formationds.nfs.XdiVfs;
import org.apache.commons.lang3.reflect.FieldUtils;
import org.dcache.nfs.nfsstat;
import org.dcache.nfs.status.InvalException;
import org.dcache.nfs.v4.xdr.*;
import org.dcache.nfs.vfs.FsStat;
import org.dcache.nfs.vfs.Inode;
import org.dcache.nfs.vfs.Stat;
import org.dcache.xdr.OncRpcException;
import org.dcache.xdr.XdrAble;
import org.dcache.xdr.XdrBuffer;
import org.glassfish.grizzly.Buffer;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.IOException;
import java.util.Optional;

public class CustomOperationGETATTR extends AbstractNFSv4Operation {

    private static final Logger _log = LogManager.getLogger(CustomOperationGETATTR.class);

    public CustomOperationGETATTR(nfs_argop4 args) {
        super(args, nfs_opnum4.OP_GETATTR);
    }

    @Override
    public void process(CompoundContext context, nfs_resop4 result) throws IOException, OncRpcException {

        final GETATTR4res res = result.opgetattr;

        res.resok4 = new GETATTR4resok();

        XdiVfs vfs = null;
        try {
            vfs = (XdiVfs) FieldUtils.readField(context.getFs(), "_inner", true);
        } catch (IllegalAccessException e) {
            throw new IOException(e);
        }
        res.resok4.obj_attributes = getAttributes(_args.opgetattr.attr_request,
                vfs,
                context.currentInode(), context);

        res.status = nfsstat.NFS_OK;

    }

    static fattr4 getAttributes(bitmap4 bitmap, XdiVfs fs, Inode inode, Stat stat, CompoundContext context)
            throws IOException, OncRpcException {

        /*
         * bitmap we send back. can't be uninitialized.
         */
        bitmap4 processedAttributes = new bitmap4(new int[0]);

        XdrBuffer xdr = new XdrBuffer(1024);
        xdr.beginEncoding();

        for (int i : bitmap) {
            Optional<XdrAble> optionalAttr = (Optional<XdrAble>) fattr2xdr(i, fs, inode, stat, context);
            if (optionalAttr.isPresent()) {
                XdrAble attr = optionalAttr.get();
                _log.debug("   getAttributes : {} ({}) OK.", i, attrMask2String(i));
                attr.xdrEncode(xdr);
                processedAttributes.set(i);
            } else {
                _log.debug("   getAttributes : {} ({}) NOT SUPPORTED.", i, attrMask2String(i));
            }
        }

        xdr.endEncoding();
        Buffer body = xdr.asBuffer();
        byte[] retBytes = new byte[body.remaining()];
        body.get(retBytes);

        fattr4 attributes = new fattr4();
        attributes.attrmask = processedAttributes;
        attributes.attr_vals = new attrlist4(retBytes);

        return attributes;
    }

    static fattr4 getAttributes(bitmap4 bitmap, XdiVfs fs, Inode inode, CompoundContext context)
            throws IOException, OncRpcException {
        return getAttributes(bitmap, fs, inode, context.getFs().getattr(inode), context);
    }

    private static FsStat getFsStat(XdiVfs fs, Inode inode) throws IOException {
        return fs.getFsStat(inode.exportIndex());
    }

    /**
     * get inodes requested attribute and converted into RPC xdr format
     * operates with READ and R/W attributes
     *
     * @param fattr
     * @param inode
     * @return XdrAble of object attribute,
     * Corresponding to fattr
     * @throws Exception
     */

    // read/read-write
    private static Optional<? extends XdrAble> fattr2xdr(int fattr, XdiVfs fs, Inode inode, Stat stat, CompoundContext context) throws IOException {

        FsStat fsStat = null;

        switch (fattr) {

            case nfs4_prot.FATTR4_SUPPORTED_ATTRS:
                bitmap4 bitmap = new bitmap4();
                bitmap.value = new int[]{
                        NFSv4FileAttributes.NFS4_SUPPORTED_ATTRS_MASK0,
                        NFSv4FileAttributes.NFS4_SUPPORTED_ATTRS_MASK1
                };
                return Optional.of(new fattr4_supported_attrs(bitmap));
            case nfs4_prot.FATTR4_TYPE:
                fattr4_type type = new fattr4_type(unixType2NFS(stat.getMode()));
                return Optional.of(type);
            case nfs4_prot.FATTR4_FH_EXPIRE_TYPE:
                fattr4_fh_expire_type fh_expire_type = new fattr4_fh_expire_type(nfs4_prot.FH4_PERSISTENT);
                return Optional.of(fh_expire_type);
            case nfs4_prot.FATTR4_CHANGE:
                fattr4_change change = new fattr4_change(stat.getGeneration());
                return Optional.of(change);
            case nfs4_prot.FATTR4_SIZE:
                fattr4_size size = new fattr4_size(stat.getSize());
                return Optional.of(size);
            case nfs4_prot.FATTR4_LINK_SUPPORT:
                fattr4_link_support link_support = new fattr4_link_support(true);
                return Optional.of(link_support);
            case nfs4_prot.FATTR4_SYMLINK_SUPPORT:
                fattr4_symlink_support symlink_support = new fattr4_symlink_support(true);
                return Optional.of(symlink_support);
            case nfs4_prot.FATTR4_NAMED_ATTR:
                fattr4_named_attr named_attr = new fattr4_named_attr(false);
                return Optional.of(named_attr);
            case nfs4_prot.FATTR4_FSID:
                fsid4 fsid = new fsid4();
                fsid.major = new uint64_t(17);
                fsid.minor = new uint64_t(17);
                return Optional.of(new fattr4_fsid(fsid));
            case nfs4_prot.FATTR4_UNIQUE_HANDLES:
                return Optional.of(new fattr4_unique_handles(true));
            case nfs4_prot.FATTR4_LEASE_TIME:
                return Optional.of(new fattr4_lease_time(NFSv4Defaults.NFS4_LEASE_TIME));
            case nfs4_prot.FATTR4_RDATTR_ERROR:
                //enum is an integer
                return Optional.of(new fattr4_rdattr_error(0));
            case nfs4_prot.FATTR4_FILEHANDLE:
                nfs_fh4 fh = new nfs_fh4();
                fh.value = inode.toNfsHandle();
                return Optional.of(new fattr4_filehandle(fh));
            case nfs4_prot.FATTR4_ACL:
                nfsace4[] aces = context.getFs().getAcl(inode);
                return Optional.of(new fattr4_acl(aces));
            case nfs4_prot.FATTR4_ACLSUPPORT:
                fattr4_aclsupport aclSupport = new fattr4_aclsupport(
                        nfs4_prot.ACL4_SUPPORT_ALLOW_ACL | nfs4_prot.ACL4_SUPPORT_DENY_ACL);
                return Optional.of(aclSupport);
            case nfs4_prot.FATTR4_ARCHIVE:
                return Optional.empty();
            case nfs4_prot.FATTR4_CANSETTIME:
                return Optional.of(new fattr4_cansettime(true));
            case nfs4_prot.FATTR4_CASE_INSENSITIVE:
                return Optional.of(new fattr4_case_insensitive(true));
            case nfs4_prot.FATTR4_CASE_PRESERVING:
                return Optional.of(new fattr4_case_preserving(true));
            case nfs4_prot.FATTR4_CHOWN_RESTRICTED:
                return Optional.empty();
            case nfs4_prot.FATTR4_FILEID:
                return Optional.of(new fattr4_fileid(stat.getFileId()));
            case nfs4_prot.FATTR4_FILES_AVAIL:
                fsStat = getFsStat(fs, inode);
                fattr4_files_avail files_avail = new fattr4_files_avail(fsStat.getTotalFiles() - fsStat.getUsedFiles());
                return Optional.of(files_avail);
            case nfs4_prot.FATTR4_FILES_FREE:
                fsStat = getFsStat(fs, inode);
                fattr4_files_free files_free = new fattr4_files_free(fsStat.getTotalFiles() - fsStat.getUsedFiles());
                return Optional.of(files_free);
            case nfs4_prot.FATTR4_FILES_TOTAL:
                fsStat = getFsStat(fs, inode);
                return Optional.of(new fattr4_files_total(fsStat.getTotalFiles()));
            case nfs4_prot.FATTR4_FS_LOCATIONS:
                return Optional.empty();
            case nfs4_prot.FATTR4_HIDDEN:
                return Optional.empty();
            case nfs4_prot.FATTR4_HOMOGENEOUS:
                return Optional.of(new fattr4_homogeneous(true));
            case nfs4_prot.FATTR4_MAXFILESIZE:
                return Optional.of(new fattr4_maxfilesize(NFSv4Defaults.NFS4_MAXFILESIZE));
            case nfs4_prot.FATTR4_MAXLINK:
                return Optional.of(new fattr4_maxlink(NFSv4Defaults.NFS4_MAXLINK));
            case nfs4_prot.FATTR4_MAXNAME:
                return Optional.of(new fattr4_maxname(NFSv4Defaults.NFS4_MAXFILENAME));
            case nfs4_prot.FATTR4_MAXREAD:
                return Optional.of(new fattr4_maxread(NFSv4Defaults.NFS4_MAXIOBUFFERSIZE));
            case nfs4_prot.FATTR4_MAXWRITE:
                fattr4_maxwrite maxwrite = new fattr4_maxwrite(NFSv4Defaults.NFS4_MAXIOBUFFERSIZE);
                return Optional.of(maxwrite);
            case nfs4_prot.FATTR4_MIMETYPE:
                return Optional.empty();
            case nfs4_prot.FATTR4_MODE:
                return Optional.of(new fattr4_mode(stat.getMode() & 07777));
            case nfs4_prot.FATTR4_NO_TRUNC:
                return Optional.of(new fattr4_no_trunc(true));
            case nfs4_prot.FATTR4_NUMLINKS:
                return Optional.of(new fattr4_numlinks(stat.getNlink()));
            case nfs4_prot.FATTR4_OWNER:
                String owner_s = context.getFs().getIdMapper().uidToPrincipal(stat.getUid());
                utf8str_mixed user = new utf8str_mixed(owner_s);
                return Optional.of(new fattr4_owner(user));
            case nfs4_prot.FATTR4_OWNER_GROUP:
                String group_s = context.getFs().getIdMapper().gidToPrincipal(stat.getGid());
                utf8str_mixed group = new utf8str_mixed(group_s);
                return Optional.of(new fattr4_owner(group));
            case nfs4_prot.FATTR4_QUOTA_AVAIL_HARD:
                return Optional.empty();
            case nfs4_prot.FATTR4_QUOTA_AVAIL_SOFT:
                return Optional.empty();
            case nfs4_prot.FATTR4_QUOTA_USED:
                return Optional.empty();
            case nfs4_prot.FATTR4_RAWDEV:
                specdata4 dev = new specdata4();
                dev.specdata1 = 0;
                dev.specdata2 = 0;
                return Optional.of(new fattr4_rawdev(dev));
            case nfs4_prot.FATTR4_SPACE_AVAIL:
                fsStat = getFsStat(fs, inode);
                fattr4_space_avail spaceAvail = new fattr4_space_avail(fsStat.getTotalSpace() - fsStat.getUsedSpace());
                return Optional.of(spaceAvail);
            case nfs4_prot.FATTR4_SPACE_FREE:
                fsStat = getFsStat(fs, inode);
                fattr4_space_free space_free = new fattr4_space_free(fsStat.getTotalSpace() - fsStat.getUsedSpace());
                return Optional.of(space_free);
            case nfs4_prot.FATTR4_SPACE_TOTAL:
                fsStat = getFsStat(fs, inode);
                return Optional.of(new fattr4_space_total(fsStat.getTotalSpace()));
            case nfs4_prot.FATTR4_SPACE_USED:
                return Optional.of(new fattr4_space_used(stat.getSize()));
            case nfs4_prot.FATTR4_SYSTEM:
                return Optional.empty();
            case nfs4_prot.FATTR4_TIME_ACCESS:
                fattr4_time_access atime = new fattr4_time_access(stat.getATime());
                return Optional.of(atime);
            case nfs4_prot.FATTR4_TIME_BACKUP:
                return Optional.empty();
            case nfs4_prot.FATTR4_TIME_CREATE:
                fattr4_time_create ctime = new fattr4_time_create(stat.getCTime());
                return Optional.of(ctime);
            case nfs4_prot.FATTR4_TIME_DELTA:
                return Optional.empty();
            case nfs4_prot.FATTR4_TIME_METADATA:
                fattr4_time_metadata mdtime = new fattr4_time_metadata(stat.getCTime());
                return Optional.of(mdtime);
            case nfs4_prot.FATTR4_TIME_MODIFY:
                fattr4_time_modify mtime = new fattr4_time_modify(stat.getMTime());
                return Optional.of(mtime);
            case nfs4_prot.FATTR4_MOUNTED_ON_FILEID:

                /*
                 * TODO!!!:
                 */

                long mofi = stat.getFileId();

                if (mofi == 0x00b0a23a /* it's a root*/) {
                    mofi = 0x12345678;
                }

                fattr4_mounted_on_fileid mounted_on_fileid = new fattr4_mounted_on_fileid(mofi);
                return Optional.of(mounted_on_fileid);

            /**
             * this is NFSv4.1 (pNFS) specific code, which is still in the
             * development ( as protocol )
             */
            case nfs4_prot.FATTR4_FS_LAYOUT_TYPE:
                fattr4_fs_layout_types fs_layout_type = new fattr4_fs_layout_types();
                fs_layout_type.value = new int[1];
                fs_layout_type.value[0] = layouttype4.LAYOUT4_NFSV4_1_FILES;
                return Optional.of(fs_layout_type);
            case nfs4_prot.FATTR4_TIME_MODIFY_SET:
            case nfs4_prot.FATTR4_TIME_ACCESS_SET:
                throw new InvalException("getattr of write-only attributes");
            default:
                _log.debug("GETATTR for #{}", fattr);
                return Optional.empty();
        }
    }


    public static String attrMask2String(int offset) {

        String maskName = "Unknown";

        switch (offset) {

            case nfs4_prot.FATTR4_SUPPORTED_ATTRS:
                maskName = " FATTR4_SUPPORTED_ATTRS ";
                break;
            case nfs4_prot.FATTR4_TYPE:
                maskName = " FATTR4_TYPE ";
                break;
            case nfs4_prot.FATTR4_FH_EXPIRE_TYPE:
                maskName = " FATTR4_FH_EXPIRE_TYPE ";
                break;
            case nfs4_prot.FATTR4_CHANGE:
                maskName = " FATTR4_CHANGE ";
                break;
            case nfs4_prot.FATTR4_SIZE:
                maskName = " FATTR4_SIZE ";
                break;
            case nfs4_prot.FATTR4_LINK_SUPPORT:
                maskName = " FATTR4_LINK_SUPPORT ";
                break;
            case nfs4_prot.FATTR4_SYMLINK_SUPPORT:
                maskName = " FATTR4_SYMLINK_SUPPORT ";
                break;
            case nfs4_prot.FATTR4_NAMED_ATTR:
                maskName = " FATTR4_NAMED_ATTR ";
                break;
            case nfs4_prot.FATTR4_FSID:
                maskName = " FATTR4_FSID ";
                break;
            case nfs4_prot.FATTR4_UNIQUE_HANDLES:
                maskName = " FATTR4_UNIQUE_HANDLES ";
                break;
            case nfs4_prot.FATTR4_LEASE_TIME:
                maskName = " FATTR4_LEASE_TIME ";
                break;
            case nfs4_prot.FATTR4_RDATTR_ERROR:
                maskName = " FATTR4_RDATTR_ERROR ";
                break;
            case nfs4_prot.FATTR4_FILEHANDLE:
                maskName = " FATTR4_FILEHANDLE ";
                break;
            case nfs4_prot.FATTR4_ACL:
                maskName = " FATTR4_ACL ";
                break;
            case nfs4_prot.FATTR4_ACLSUPPORT:
                maskName = " FATTR4_ACLSUPPORT ";
                break;
            case nfs4_prot.FATTR4_ARCHIVE:
                maskName = " FATTR4_ARCHIVE ";
                break;
            case nfs4_prot.FATTR4_CANSETTIME:
                maskName = " FATTR4_CANSETTIME ";
                break;
            case nfs4_prot.FATTR4_CASE_INSENSITIVE:
                maskName = " FATTR4_CASE_INSENSITIVE ";
                break;
            case nfs4_prot.FATTR4_CASE_PRESERVING:
                maskName = " FATTR4_CASE_PRESERVING ";
                break;
            case nfs4_prot.FATTR4_CHOWN_RESTRICTED:
                maskName = " FATTR4_CHOWN_RESTRICTED ";
                break;
            case nfs4_prot.FATTR4_FILEID:
                maskName = " FATTR4_FILEID ";
                break;
            case nfs4_prot.FATTR4_FILES_AVAIL:
                maskName = " FATTR4_FILES_AVAIL ";
                break;
            case nfs4_prot.FATTR4_FILES_FREE:
                maskName = " FATTR4_FILES_FREE ";
                break;
            case nfs4_prot.FATTR4_FILES_TOTAL:
                maskName = " FATTR4_FILES_TOTAL ";
                break;
            case nfs4_prot.FATTR4_FS_LOCATIONS:
                maskName = " FATTR4_FS_LOCATIONS ";
                break;
            case nfs4_prot.FATTR4_HIDDEN:
                maskName = " FATTR4_HIDDEN ";
                break;
            case nfs4_prot.FATTR4_HOMOGENEOUS:
                maskName = " FATTR4_HOMOGENEOUS ";
                break;
            case nfs4_prot.FATTR4_MAXFILESIZE:
                maskName = " FATTR4_MAXFILESIZE ";
                break;
            case nfs4_prot.FATTR4_MAXLINK:
                maskName = " FATTR4_MAXLINK ";
                break;
            case nfs4_prot.FATTR4_MAXNAME:
                maskName = " FATTR4_MAXNAME ";
                break;
            case nfs4_prot.FATTR4_MAXREAD:
                maskName = " FATTR4_MAXREAD ";
                break;
            case nfs4_prot.FATTR4_MAXWRITE:
                maskName = " FATTR4_MAXWRITE ";
                break;
            case nfs4_prot.FATTR4_MIMETYPE:
                maskName = " FATTR4_MIMETYPE ";
                break;
            case nfs4_prot.FATTR4_MODE:
                maskName = " FATTR4_MODE ";
                break;
            case nfs4_prot.FATTR4_NO_TRUNC:
                maskName = " FATTR4_NO_TRUNC ";
                break;
            case nfs4_prot.FATTR4_NUMLINKS:
                maskName = " FATTR4_NUMLINKS ";
                break;
            case nfs4_prot.FATTR4_OWNER:
                maskName = " FATTR4_OWNER ";
                break;
            case nfs4_prot.FATTR4_OWNER_GROUP:
                maskName = " FATTR4_OWNER_GROUP ";
                break;
            case nfs4_prot.FATTR4_QUOTA_AVAIL_HARD:
                maskName = " FATTR4_QUOTA_AVAIL_HARD ";
                break;
            case nfs4_prot.FATTR4_QUOTA_AVAIL_SOFT:
                maskName = " FATTR4_QUOTA_AVAIL_SOFT ";
                break;
            case nfs4_prot.FATTR4_QUOTA_USED:
                maskName = " FATTR4_QUOTA_USED ";
                break;
            case nfs4_prot.FATTR4_RAWDEV:
                maskName = " FATTR4_RAWDEV ";
                break;
            case nfs4_prot.FATTR4_SPACE_AVAIL:
                maskName = " FATTR4_SPACE_AVAIL ";
                break;
            case nfs4_prot.FATTR4_SPACE_FREE:
                maskName = " FATTR4_SPACE_FREE ";
                break;
            case nfs4_prot.FATTR4_SPACE_TOTAL:
                maskName = " FATTR4_SPACE_TOTAL ";
                break;
            case nfs4_prot.FATTR4_SPACE_USED:
                maskName = " FATTR4_SPACE_USED ";
                break;
            case nfs4_prot.FATTR4_SYSTEM:
                maskName = " FATTR4_SYSTEM ";
                break;
            case nfs4_prot.FATTR4_TIME_ACCESS:
                maskName = " FATTR4_TIME_ACCESS ";
                break;
            case nfs4_prot.FATTR4_TIME_ACCESS_SET:
                maskName = " FATTR4_TIME_ACCESS_SET ";
                break;
            case nfs4_prot.FATTR4_TIME_BACKUP:
                maskName = " FATTR4_TIME_BACKUP ";
                break;
            case nfs4_prot.FATTR4_TIME_CREATE:
                maskName = " FATTR4_TIME_CREATE ";
                break;
            case nfs4_prot.FATTR4_TIME_DELTA:
                maskName = " FATTR4_TIME_DELTA ";
                break;
            case nfs4_prot.FATTR4_TIME_METADATA:
                maskName = " FATTR4_TIME_METADATA ";
                break;
            case nfs4_prot.FATTR4_TIME_MODIFY:
                maskName = " FATTR4_TIME_MODIFY ";
                break;
            case nfs4_prot.FATTR4_TIME_MODIFY_SET:
                maskName = " FATTR4_TIME_MODIFY_SET ";
                break;
            case nfs4_prot.FATTR4_MOUNTED_ON_FILEID:
                maskName = " FATTR4_MOUNTED_ON_FILEID ";
                break;
            case nfs4_prot.FATTR4_FS_LAYOUT_TYPE:
                maskName = " FATTR4_FS_LAYOUT_TYPE ";
                break;
            case nfs4_prot.FATTR4_LAYOUT_HINT:
                maskName = " FATTR4_LAYOUT_HINT ";
                break;
            case nfs4_prot.FATTR4_LAYOUT_TYPE:
                maskName = " FATTR4_LAYOUT_TYPE ";
                break;
            case nfs4_prot.FATTR4_LAYOUT_BLKSIZE:
                maskName = " FATTR4_LAYOUT_BLKSIZE ";
                break;
            case nfs4_prot.FATTR4_LAYOUT_ALIGNMENT:
                maskName = " FATTR4_LAYOUT_ALIGNMENT ";
                break;
            case nfs4_prot.FATTR4_FS_LOCATIONS_INFO:
                maskName = " FATTR4_FS_LOCATIONS_INFO ";
                break;
            case nfs4_prot.FATTR4_MDSTHRESHOLD:
                maskName = " FATTR4_MDSTHRESHOLD ";
                break;
            case nfs4_prot.FATTR4_RETENTION_GET:
                maskName = " FATTR4_RETENTION_GET ";
                break;
            case nfs4_prot.FATTR4_RETENTION_SET:
                maskName = " FATTR4_RETENTION_SET ";
                break;
            case nfs4_prot.FATTR4_RETENTEVT_GET:
                maskName = " FATTR4_RETENTEVT_GET ";
                break;
            case nfs4_prot.FATTR4_RETENTEVT_SET:
                maskName = " FATTR4_RETENTEVT_SET ";
                break;
            case nfs4_prot.FATTR4_RETENTION_HOLD:
                maskName = " FATTR4_RETENTION_HOLD ";
                break;
            case nfs4_prot.FATTR4_MODE_SET_MASKED:
                maskName = " FATTR4_MODE_SET_MASKED ";
                break;
            case nfs4_prot.FATTR4_FS_CHARSET_CAP:
                maskName = " FATTR4_FS_CHARSET_CAP ";
                break;
            default:
                maskName += "(" + offset + ")";
        }

        return maskName;

    }


    static int unixType2NFS(int type) {

        int ret = 0;

        int mask = 0770000;

        switch (type & mask) {

            case Stat.S_IFREG:
                ret = nfs_ftype4.NF4REG;
                break;
            case Stat.S_IFDIR:
                ret = nfs_ftype4.NF4DIR;
                break;
            case Stat.S_IFLNK:
                ret = nfs_ftype4.NF4LNK;
                break;
            case Stat.S_IFSOCK:
                ret = nfs_ftype4.NF4SOCK;
                break;
            case Stat.S_IFBLK:
                ret = nfs_ftype4.NF4BLK;
                break;
            case Stat.S_IFCHR:
                ret = nfs_ftype4.NF4CHR;
                break;
            case Stat.S_IFIFO:
                ret = nfs_ftype4.NF4FIFO;
                break;
            default:
                _log.info("Unknown mode [{}]", Integer.toOctalString(type));
                ret = 0;

        }

        return ret;
    }

}
