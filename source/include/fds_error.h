/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_FDS_ERROR_H_
#define SOURCE_INCLUDE_FDS_ERROR_H_

#include <sstream>
#include <string>
#include <fds_types.h>
#include <fdsp/FDSP_types.h>

namespace fds {
    // TODO(Rao): There is already a mismatch between enums and strings.
    // Ideally this errstrs isn't needed.  We need an error string catalog
    // that we lookup.  We need to remove this array.
    extern const char* fds_errstrs[];

    /* DO NOT change the order */
    typedef enum {
        ERR_OK                   = 0,
        ERR_DUPLICATE            = 1,
        ERR_HASH_COLLISION       = 2,
        ERR_DISK_WRITE_FAILED    = 3,
        ERR_DISK_READ_FAILED     = 4,
        ERR_CAT_QUERY_FAILED     = 5,
        ERR_CAT_ENTRY_NOT_FOUND  = 6,
        ERR_INVALID_ARG          = 7,
        ERR_PENDING_RESP         = 8,
        ERR_NOT_FOUND            = 9,
        ERR_VOL_ADMISSION_FAILED = 10,
        ERR_GET_DLT_FAILED       = 11,
        ERR_GET_DMT_FAILED       = 12,
        ERR_NOT_IMPLEMENTED      = 13,
        ERR_OUT_OF_MEMORY        = 14,
        ERR_DUPLICATE_UUID       = 15,
        ERR_TRANS_JOURNAL_OUT_OF_IDS = 16,
        ERR_TRANS_JOURNAL_REQUEST_QUEUED = 17,
        ERR_NODE_NOT_ACTIVE      = 18,
        ERR_VOL_NOT_EMPTY        = 19,
        ERR_BLOB_NOT_FOUND       = 20,
        ERR_NOT_READY            = 21,
        ERR_INVALID_DLT          = 22,
        ERR_PERSIST_STATE_MISMATCH = 23,
        ERR_EXCEED_MIN_IOPS      = 24,
        ERR_UNAUTH_ACCESS        = 25,
        ERR_DISK_FILE_UNLINK_FAILED = 26,
        ERR_SERIALIZE_FAILED     = 27,
        ERR_NO_BYTES_WRITTEN     = 28,
        ERR_NO_BYTES_READ        = 29,
        ERR_VOL_NOT_FOUND        = 30,
        ERR_NETWORK_CORRUPT      = 31,
        ERR_ONDISK_DATA_CORRUPT  = 32,
        ERR_INVALID_DMT          = 33,
        ERR_CATSYNC_NOT_PROGRESS = 34,
        ERR_DMT_EQUAL            = 35,
        ERR_DMT_FORWARD          = 36,

        /* I/O error range */
        ERR_IO_DLT_MISMATCH      = 100,

        /* Metadata error range */
        ERR_BLOB_OFFSET_INVALID  = 500,

        /* Migration error range [1000-1500) */
        ERR_MIGRATION_DUPLICATE_REQUEST = 1000,

        /* FdsActor err range [1500-2000) */
        ERR_FAR_INVALID_REQUEST = 1500,
        /* FDS actor is shutdown */
        ERR_FAR_SHUTDOWN,

        /* Storage manager error range [2000-2500) */
        ERR_SM_NOT_IN_SYNC_MODE = 2000,
        ERR_SM_TOKENSTATEDB_KEY_NOT_FOUND,
        ERR_SM_TOKENSTATEDB_DUPLICATE_KEY,

        /* Network errors */
        ERR_NETWORK_TRANSPORT = 3000,

        ERR_MAX
    } fds_errno_t;

    class Error {
        fds_errno_t _errno;
  public:
        Error();
        Error(fds_errno_t errno_arg);  //NOLINT
        Error(fds_uint32_t errno_fdsp); //NOLINT
        Error(const Error& err);

        bool OK() const;

        bool ok() const;
        fds_errno_t GetErrno() const;
        std::string GetErrstr() const;

        FDS_ProtocolInterface::FDSP_ErrType getFdspErr() const;
        Error& operator=(const Error& rhs);

        Error& operator=(const fds_errno_t& rhs);

        bool operator==(const Error& rhs) const;
        bool operator==(const fds_errno_t& rhs) const;
        bool operator!=(const Error& rhs) const;
        bool operator!=(const fds_errno_t& rhs) const;
        ~Error();
    };

    std::ostream& operator<<(std::ostream& out, const Error& err);

    typedef enum {
        FDSN_StatusOK                                              ,
        FDSN_StatusCreated                                         ,

        /**
         * Errors that prevent the S3 request from being issued or response from
         * being read
         **/
        FDSN_StatusInternalError                                   ,
        FDSN_StatusOutOfMemory                                     ,
        FDSN_StatusInterrupted                                     ,
        FDSN_StatusInvalidBucketNameTooLong                        ,
        FDSN_StatusInvalidBucketNameFirstCharacter                 ,
        FDSN_StatusInvalidBucketNameCharacter                      ,
        FDSN_StatusInvalidBucketNameCharacterSequence              ,
        FDSN_StatusInvalidBucketNameTooShort                       ,
        FDSN_StatusInvalidBucketNameDotQuadNotation                ,
        FDSN_StatusQueryParamsTooLong                              ,
        FDSN_StatusFailedToInitializeRequest                       ,
        FDSN_StatusMetaDataHeadersTooLong                          ,
        FDSN_StatusBadMetaData                                     ,
        FDSN_StatusBadContentType                                  ,

        FDSN_StatusContentTypeTooLong                              ,
        FDSN_StatusBadMD5                                          ,
        FDSN_StatusMD5TooLong                                      ,
        FDSN_StatusBadCacheControl                                 ,
        FDSN_StatusCacheControlTooLong                             ,
        FDSN_StatusBadContentDispositionFilename                   ,
        FDSN_StatusContentDispositionFilenameTooLong               ,
        FDSN_StatusBadContentEncoding                              ,
        FDSN_StatusContentEncodingTooLong                          ,
        FDSN_StatusBadIfMatchETag                                  ,
        FDSN_StatusIfMatchETagTooLong                              ,
        FDSN_StatusBadIfNotMatchETag                               ,
        FDSN_StatusIfNotMatchETagTooLong                           ,
        FDSN_StatusHeadersTooLong                                  ,
        FDSN_StatusKeyTooLong                                      ,
        FDSN_StatusUriTooLong                                      ,
        FDSN_StatusXmlParseFailure                                 ,
        FDSN_StatusEmailAddressTooLong                             ,
        FDSN_StatusUserIdTooLong                                   ,
        FDSN_StatusUserDisplayNameTooLong                          ,
        FDSN_StatusGroupUriTooLong                                 ,
        FDSN_StatusPermissionTooLong                               ,
        FDSN_StatusTargetBucketTooLong                             ,
        FDSN_StatusTargetPrefixTooLong                             ,
        FDSN_StatusTooManyGrants                                   ,
        FDSN_StatusBadGrantee                                      ,
        FDSN_StatusBadPermission                                   ,
        FDSN_StatusXmlDocumentTooLarge                             ,
        FDSN_StatusNameLookupError                                 ,
        FDSN_StatusFailedToConnect                                 ,
        FDSN_StatusServerFailedVerification                        ,
        FDSN_StatusConnectionFailed                                ,
        FDSN_StatusAbortedByCallback                               ,
        FDSN_StatusRequestTimedOut                                 ,
        FDSN_StatusEntityEmpty                                     ,
        FDSN_StatusEntityDoesNotExist                              ,
        /**
         * Errors from the S3 service
         **/
        FDSN_StatusErrorAccessDenied                               ,
        FDSN_StatusErrorAccountProblem                             ,
        FDSN_StatusErrorAmbiguousGrantByEmailAddress               ,
        FDSN_StatusErrorBadDigest                                  ,
        FDSN_StatusErrorBucketAlreadyExists                        ,
        FDSN_StatusErrorBucketNotExists                            ,
        FDSN_StatusErrorBucketAlreadyOwnedByYou                    ,
        FDSN_StatusErrorBucketNotEmpty                             ,
        FDSN_StatusErrorCredentialsNotSupported                    ,
        FDSN_StatusErrorCrossLocationLoggingProhibited             ,
        FDSN_StatusErrorEntityTooSmall                             ,
        FDSN_StatusErrorEntityTooLarge                             ,

        FDSN_StatusErrorMissingContentLength                       ,

        /* keep this as the last*/
        FDSN_StatusErrorUnknown
    } FDSN_Status;

    std::ostream& operator<<(std::ostream& os, FDSN_Status status);

    FDSN_Status getStatusFromError(const Error& error);
    std::string toString(FDSN_Status status);

}  // namespace fds

#endif  // SOURCE_INCLUDE_FDS_ERROR_H_
