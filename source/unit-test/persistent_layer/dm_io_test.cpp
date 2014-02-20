/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <unistd.h>
#include <fds_module.h>
#include <persistent_layer/dm_io.h>
#include <persistent_layer/pm_unit_test.h>
#include <concurrency/ThreadPool.h>
#include <iostream>

using namespace std;

namespace fds {

// \pdio_read
// ----------
//
static void
pdio_read(DiskReqTest *req)
{
    static int     rd_count;
    diskio::DataIO &pio = diskio::DataIO::disk_singleton();

    pio.disk_read(req);
    rd_count++;
    if ((rd_count % 10000) == 0) {
        cout << "Issued " << rd_count << " read requests..." << endl;
    }
}

// \pdio_write
// -----------
//
static void
pdio_write(fds_threadpool *wr, fds_threadpool *rd, DiskReqTest *cur)
{
    static int      wr_count;
    DiskReqTest     *req;
    meta_vol_io_t   vio;
    meta_obj_id_t   oid;
    ObjectBuf       *buf;
    diskio::DataIO  &pio = diskio::DataIO::disk_singleton();
    diskio::DataTier tier;

    if (cur == nullptr) {
        vadr_set_inval(vio.vol_adr);
        vio.vol_rsvd    = 0;
        vio.vol_blk_len = 0;
        oid.oid_hash_hi = random();
        oid.oid_hash_lo = random();

        buf = new ObjectBuf;
        buf->size = 8 << diskio::DataIO::disk_io_blk_shift();
        buf->data.reserve(buf->size);

        /*
         * Randomly pick a tier
         */
        tier = diskio::diskTier;
        if ((oid.oid_hash_hi % 2) == 0) {
          tier = diskio::flashTier;
        }

        req = new DiskReqTest(wr, rd, vio, oid, buf, oid.oid_hash_hi % 2, tier);
        req->req_gen_pattern();
    } else {
        req = cur;
    }
    pio.disk_write(req);
    wr_count++;
    if ((wr_count % 10000) == 0) {
        cout << "Issued " << wr_count << " write requests..." << endl;
    }
}

// \req_complete
// -------------
//
void
DiskReqTest::req_complete()
{
    fdsio::Request::req_complete();

    tst_iter++;
    if (tst_iter == 1) {
        tst_rd->schedule(pdio_read, this);
    } else {
        req_verify();
        if (tst_iter < 30) {
            if (tst_iter % 2) {
                tst_wr->schedule(pdio_write, tst_wr, tst_rd, this);
            } else {
                dat_buf->data.clear();
                tst_rd->schedule(pdio_read, this);
            }
        } else {
          delete this;
            return;
        }
    }
}

// \req_verify
// -----------
//
void
DiskReqTest::req_verify()
{
    std::string::iterator s1, s2;

    s1 = dat_buf->data.begin();
    s2 = tst_verf.data.begin();
    for (uint i = 0; i < dat_buf->size; i++) {
        fds_verify(*s1 == *s2);
        s1++;
        s2++;
    }
}

// \DiskReqTest
// ------------
//
void
DiskReqTest::req_gen_pattern()
{
    char *p, start, save[200];
    std::string::iterator s1, s2;

    fds_verify(dat_buf->size == tst_verf.size);
    snprintf(save, sizeof(save), "[0x%p] - 0x%llx 0x%llx", this,
             idx_oid.oid_hash_hi, idx_oid.oid_hash_lo);
    p  = save;
    s1 = dat_buf->data.begin();
    s2 = tst_verf.data.begin();

    start = (idx_oid.oid_hash_hi % 2) ? 'A' : 'a';
    for (uint i = 0; i < dat_buf->size; i++) {
        if (*p != '\0') {
            *s1 = *p;
            *s2 = *p;
            p++;
        } else {
            *s1 = start + (i % 26);
            *s2 = *s1;
        }
        s1++;
        s2++;
    }
}

} // namespace fds

int
main(int argc, char **argv)
{
    fds::Module *io_test_vec[] = {
        &diskio::gl_dataIOMod,
        nullptr
    };
    fds::ModuleVector    io_test(argc, argv, io_test_vec);
    fds::fds_threadpool  wr(50), rd(100);

    io_test.mod_execute();
    for (int i = 0; i < 500; i++) {
        wr.schedule(fds::pdio_write, &wr, &rd, nullptr);
    }
    sleep(10);
}
