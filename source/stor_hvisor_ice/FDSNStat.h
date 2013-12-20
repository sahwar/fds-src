/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#ifndef STOR_HVISOR_FDSN_STAT_H_
#define STOR_HVISOR_FDSN_STAT_H_

#include <util/fds_stat.h>

namespace fds {

/* These enums are indices to array, don't change their order. */
enum
{
    FDSN_GO_CHK_BKET_EXIST       = 0,
    FDSN_GO_ALLOC_BLOB_REQ       = 1,
    FDSN_GO_ENQUEUE_IO           = 2,
    FDSN_GO_ADD_WAIT_QUEUE       = 3,
    FDSN_GO_TOTAL                = 4,
    FDSN_MAX_STAT                = 5
};

extern void fdsn_register_stat(void);

} // namespace fds

#endif /* STOR_HVISOR_FDSN_STAT_H_ */
