/*
 * Copyright 2014 by Formation Data Systems, Inc.
 * vim: noai:ts=8:sw=2:tw=100:syntax=cpp:et
 */
/**
 *
 */

namespace c_glib FDS_ProtocolInterface
namespace cpp FDS_ProtocolInterface
namespace java com.formationds.protocol

const string INITIAL_XDI_VERSION = "0_8"
const string CURRENT_XDI_VERSION = INITIAL_XDI_VERSION

enum BlobListOrder {
    UNSPECIFIED = 0,
    LEXICOGRAPHIC,
    BLOBSIZE
}

enum ErrorCode {
    OK = 0,
    INTERNAL_SERVER_ERROR,
    MISSING_RESOURCE,
    BAD_REQUEST,
    RESOURCE_ALREADY_EXISTS,
    RESOURCE_NOT_EMPTY,
    SERVICE_NOT_READY,
    SERVICE_SHUTTING_DOWN,
    TIMEOUT,
}

enum PatternSemantics {
    PCRE,
    PREFIX,
    PREFIX_AND_DELIMITER
}

exception ApiException {
    1: string message,
    2: ErrorCode errorCode,
}

exception NotMasterDomain {
    1: string message,
}

/* Can be consolidated when apis and fdsp merge or whatever */
struct BlobDescriptor {
     1: required string name,
     2: required i64 byteCount,
     3: required map<string, string> metadata
}

struct VolumeAccessMode {
  1: optional bool can_write = true;
  2: optional bool can_cache = true;
}
