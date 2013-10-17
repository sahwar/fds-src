/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <unistd.h>
#include <fds_module.h>
#include <disk-mgr/dm_io.h>
#include <concurrency/ThreadPool.h>
#include <iostream>

using namespace std;

class DiskReqTest : public diskio::DiskRequest
{
  public:
    DiskReqTest(fds::fds_threadpool *const pool,
                meta_vol_io_t       &vio,
                meta_obj_id_t       &oid,
                fds::ObjectBuf      *buf,
                bool                block);
    ~DiskReqTest();

    void req_submit() {
        fdsio::Request::req_submit();
    }
    void req_complete();
    void req_verify();
    void req_gen_pattern();

  private:
    int                        tst_iter;
    fds::ObjectBuf             tst_verf;
    fds::fds_threadpool *const tst_pool;
};

// \DiskReqTest
// ------------
//
DiskReqTest::DiskReqTest(fds::fds_threadpool *const pool,
                         meta_vol_io_t       &vio,
                         meta_obj_id_t       &oid,
                         fds::ObjectBuf      *buf,
                         bool                block)
    : tst_pool(pool), tst_iter(0), diskio::DiskRequest(vio, oid, buf, block)
{
    tst_verf.size = buf->size;
    tst_verf.data.reserve(buf->size);
}

DiskReqTest::~DiskReqTest()
{
    delete dat_buf;
}

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
pdio_write(fds::fds_threadpool *pool, DiskReqTest *cur)
{
    static int      wr_count;
    DiskReqTest     *req;
    meta_vol_io_t   vio;
    meta_obj_id_t   oid;
    fds::ObjectBuf  *buf;
    diskio::DataIO  &pio = diskio::DataIO::disk_singleton();

    if (cur == nullptr) {
        vadr_set_inval(vio.vol_adr);
        vio.vol_rsvd    = 0;
        vio.vol_blk_len = 0;
        oid.oid_hash_hi = random();
        oid.oid_hash_lo = random();

        buf = new fds::ObjectBuf;
        buf->size = 8 << diskio::DataIO::disk_io_blk_shift();
        buf->data.reserve(buf->size);

        req = new DiskReqTest(pool, vio, oid, buf, oid.oid_hash_hi % 2);
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
        tst_pool->schedule(pdio_read, this);
    } else {
        req_verify();
        if (tst_iter < 30000) {
            if (tst_iter % 2) {
                tst_pool->schedule(pdio_write, tst_pool, this);
            } else {
                dat_buf->data.clear();
                tst_pool->schedule(pdio_read, this);
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
    for (int i = 0; i < dat_buf->size; i++) {
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
    for (int i = 0; i < dat_buf->size; i++) {
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

int
main(int argc, char **argv)
{
    fds::Module *io_test_vec[] = {
        &diskio::gl_dataIOMod,
        nullptr
    };
    fds::ModuleVector    io_test(argc, argv, io_test_vec);
    fds::fds_threadpool  pool(100);

    io_test.mod_execute();
    for (int i = 0; i < 100000; i++) {
        pool.schedule(pdio_write, &pool, nullptr);
    }
}
