/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */

#include <libudev.h>

#include "fds_uuid.h"
#include "disk_common.h"
#include "disk_label.h"
#include "disk_obj_iter.h"
#include "disk_plat_module.h"
#include "platform_disk_obj.h"               // TODO(donavan) the .h file is named wrong
#include "disk_print_iter.h"
#include "disk_constants.h"

namespace fds
{
    PmDiskObj::~PmDiskObj()
    {
        if (dsk_my_dev != NULL)
        {
            udev_device_unref(dsk_my_dev);
            dsk_my_dev = NULL;
        }

        if (dsk_parent != NULL)
        {
            dsk_parent->rs_mutex()->lock();
            dsk_part_link.chain_rm_init();
            fds_verify(dsk_common == dsk_parent->dsk_common);
            dsk_parent->rs_mutex()->unlock();

            dsk_common = NULL;
            dsk_parent = NULL;  // free parent obj when refcnt = 0.
        } else {
            if (dsk_common != NULL)
            {
                delete dsk_common;
            }
        }
        fds_verify(dsk_label == NULL);
        fds_verify(dsk_part_link.chain_empty());
        fds_verify(dsk_disc_link.chain_empty());
        fds_verify(dsk_type_link.chain_empty());
        fds_verify(dsk_part_head.chain_empty_list());

        LOGNORMAL << "free " << rs_name;
    }

    PmDiskObj::PmDiskObj() : dsk_part_link(this), dsk_disc_link(this), dsk_raw_path(NULL),
        dsk_parent(NULL), dsk_my_dev(NULL), dsk_common(NULL), dsk_label(NULL)
    {
        rs_name[0]   = '\0';
        dsk_cap_gb   = 0;
        dsk_part_idx = 0;
        dsk_part_cnt = 0;
        dsk_my_devno = 0;
        dsk_raw_plen = 0;
    }

    // dsk_set_mount_point
    // -------------------
    //
    void PmDiskObj::dsk_set_mount_point(const char *mnt)
    {
        dsk_mount_pt.assign(mnt);
    }

    // dsk_get_mount_point
    // -------------------
    //
    const std::string &PmDiskObj::dsk_get_mount_point()
    {
        if (dsk_mount_pt.empty())
        {
            if (DiskPlatModule::dsk_get_hdd_inv()->dsk_need_simulation() == true)
            {
                // In simulation, the mount point is the same as the device.
                dsk_mount_pt.assign(rs_name);
            }
        }
        return dsk_mount_pt;
    }

    // dsk_update_device
    // -----------------
    //
    void PmDiskObj::dsk_update_device(struct udev_device *dev, PmDiskObj::pointer ref,
                                      const std::string  &blk_path,
                                      const std::string  &dev_path)
    {
        if (dev == NULL)
        {
            dev = dsk_my_dev;
        }else {
            fds_assert(dsk_my_dev == NULL);
            fds_assert(dsk_parent == NULL);
            dsk_my_dev = dev;
        }

        dsk_cap_gb   = strtoull(udev_device_get_sysattr_value(dev, "size"), NULL, 10) >> 21;
        dsk_my_devno = udev_device_get_devnum(dev);
        dsk_raw_path = udev_device_get_devpath(dev);
        dsk_raw_plen = strlen(dsk_raw_path);
        strncpy(rs_name, dev_path.c_str(), sizeof(rs_name));

        if (ref == NULL)
        {
            dsk_parent = this;
            dsk_common = new DiskCommon(blk_path);
        }else {
            fds_assert(ref->dsk_common != NULL);
            fds_assert(ref->dsk_common->dsk_blk_path == blk_path);
            dsk_fixup_family_links(ref);
        }

        if (dsk_parent == this)
        {
            dsk_read_uuid();
        }else {
            /*  Generate uuid for this partition slide, don't have to be persistent. */
            rs_uuid = fds_get_uuid64(get_uuid());
        }
    }

    // dsk_get_info
    // ------------
    //
    PmDiskObj::pointer PmDiskObj::dsk_get_info(const std::string &dev_path)
    {
        ChainList            *slices;
        ChainIter             iter;
        PmDiskObj::pointer    found;

        found = NULL;

        if (dsk_parent != NULL)
        {
            fds_verify(dsk_parent->dsk_parent == dsk_parent);
            fds_assert(dsk_parent->dsk_common == dsk_common);

            dsk_parent->rs_mutex()->lock();
            slices = &dsk_parent->dsk_part_head;
            chain_foreach(slices, iter)
            {
                found = slices->chain_iter_current<PmDiskObj>(iter);
                fds_verify(found->dsk_parent == dsk_parent);
                fds_verify(found->dsk_common == dsk_common);

                if (strncmp(found->rs_name, dev_path.c_str(), sizeof(found->rs_name)) != 0)
                {
                    found = NULL;
                    continue;
                }
                break;
            }
            dsk_parent->rs_mutex()->unlock();
        }
        return found;
    }

    // dsk_read_uuid
    // -------------
    //
    void PmDiskObj::dsk_read_uuid()
    {
        fds_uint64_t    uuid;
        DiskLabel      *tmp;

        if (dsk_label == NULL)
        {
            tmp = new DiskLabel(this);
            rs_mutex()->lock();

            if (dsk_label == NULL)
            {
                dsk_label = tmp;
            }else {
                delete tmp;
            }
            rs_mutex()->unlock();
        }
        dsk_label->dsk_label_read();

        rs_mutex()->lock();
        rs_uuid = dsk_label->dsk_label_my_uuid();

        if (rs_uuid.uuid_get_val() == 0)
        {
            uuid    = fds_get_uuid64(get_uuid());
            rs_uuid = ResourceUUID(uuid);
            dsk_label->dsk_label_save_my_uuid(rs_uuid);
        }else {
            LOGNORMAL << "Read uuid " << std::hex << rs_uuid.uuid_get_val()
                      << " from " << rs_name << std::dec;
        }
        rs_mutex()->unlock();
    }

    // dsk_dev_foreach
    // ---------------
    //
    void PmDiskObj::dsk_dev_foreach(DiskObjIter *iter, bool parent_only)
    {
        if ((dsk_parent == NULL) || ((parent_only == true) && (dsk_parent == this)))
        {
            return;
        }
        int             cnt;
        ChainIter       i;
        ChainList      *l;
        DiskObjArray    disks(dsk_parent->dsk_part_cnt << 1);

        cnt = 0;
        dsk_parent->rs_mutex()->lock();
        l = &dsk_parent->dsk_part_head;
        chain_foreach(l, i) {
            disks[cnt++] = l->chain_iter_current<PmDiskObj>(i);
        }
        dsk_parent->rs_mutex()->unlock();

        for (int j = 0; j < cnt; j++)
        {
            if (iter->dsk_iter_fn(disks[j]) == false)
            {
                break;
            }
        }
    }

    // dsk_fixup_family_links
    // ----------------------
    //
    void PmDiskObj::dsk_fixup_family_links(PmDiskObj::pointer ref)
    {
        ChainIter             iter;
        ChainList            *slices, tmp;
        PmDiskObj::pointer    parent, disk;

        parent = ref->dsk_parent;
        fds_assert(parent != NULL);

        // The shortest raw path is the parent object.
        if (dsk_raw_plen < parent->dsk_raw_plen)
        {
            parent = this;

            // Need to reparent all devices to this obj.
            ref->rs_mutex()->lock();
            slices = &ref->dsk_part_head;
            chain_foreach(slices, iter) {
                disk = slices->chain_iter_current<PmDiskObj>(iter);
                fds_verify(disk->dsk_parent == parent);

                disk->dsk_parent = this;
            }
            tmp.chain_transfer(slices);
            ref->rs_mutex()->unlock();

            rs_mutex()->lock();
            dsk_part_head.chain_transfer(&tmp);
            rs_mutex()->unlock();
            fds_verify(ref->dsk_parent == this);
        }
        dsk_parent = parent;
        dsk_common = parent->dsk_common;
        fds_verify(dsk_common != NULL);

        if (dsk_part_link.chain_empty())
        {
            parent->rs_mutex()->lock();
            parent->dsk_part_cnt++;
            parent->dsk_part_head.chain_add_back(&dsk_part_link);
            parent->rs_mutex()->unlock();
        }
    }

    std::ostream &operator<< (std::ostream &os, PmDiskObj::pointer obj)
    {
        ChainIter    i;
        ChainList   *l;

        if (obj->dsk_parent == obj)
        {
            os << obj->rs_name << " [uuid " << std::hex
            << obj->rs_uuid.uuid_get_val() << std::dec
            << " - " << obj->dsk_cap_gb << " GB]\n";
            os << obj->dsk_common->dsk_get_blk_path() << std::endl;
            os << obj->dsk_raw_path << std::endl;

            DiskPrintIter    iter;
            obj->dsk_dev_foreach(&iter);
        }else {
            os << "  " << obj->rs_name << " [uuid " << std::hex
            << obj->rs_uuid.uuid_get_val() << std::dec
            << " - " << obj->dsk_cap_gb << " GB]\n";

            if (obj->dsk_raw_path != NULL)
            {
                os << "  " << obj->dsk_raw_path << "\n";
            }
        }
        os << std::endl;
        return os;
    }

    // dsk_blk_dev_path
    // ----------------
    //
    bool PmDiskObj::dsk_blk_dev_path(const char *raw, std::string &blk, std::string &dev)
    {
        const char   *block;

        // Check for a sdX block device
        block = strstr(raw, "block/sd");

        if (block == NULL)
        {
            // check for virtual box device (?)
            block = strstr(raw, "block/xvd");

            if (block == NULL)
            {
                return false;
            }
        }

        // trim off the end device name (like dirname without altering *raw, and keep the trailing '/')
        blk.assign(raw, (size_t) (block - raw) + sizeof("block"));
        dev.assign("/dev");

        block = strrchr(raw, '/');

        // Append the device name to the output string. e.g., sdX
        dev.append(block);

        return true;
    }

    // dsk_read
    // --------
    //
    ssize_t PmDiskObj::dsk_read(void *buf, fds_uint32_t sector, int sec_cnt)
    {
        int        fd;
        ssize_t    rt;

        fd = open(rs_name, O_RDWR | O_SYNC);
        rt = pread(fd, buf,
                   fds_disk_sector_to_byte(sec_cnt), fds_disk_sector_to_byte(sector));
        close(fd);
        return rt;
    }

    // dsk_write
    // ---------
    //
    ssize_t PmDiskObj::dsk_write(bool sim, void *buf, fds_uint32_t sector, int sec_cnt)
    {
        int        fd;
        ssize_t    rt;

        /* Don't touch sda device */
        if (sim == true)
        {
            LOGNORMAL << "Skipping real disk in sim env..." << rs_name;
            return 0;
        }
        fds_verify((sector + sec_cnt) <= 16384);  // TODO(Vy): no hardcode
        fd = open(rs_name, O_RDWR | O_SYNC);
        rt = pwrite(fd, buf,
                    fds_disk_sector_to_byte(sec_cnt), fds_disk_sector_to_byte(sector));
        close(fd);

        if (rt < 0)
        {
            perror("Error");
            return rt;
        }

        LOGNORMAL << "Write superblock to " << rs_name << ", sector " << sector
        << ", ret " << rt << ", sect cnt " << sec_cnt;
        return rt;
    }
}  // namespace fds
