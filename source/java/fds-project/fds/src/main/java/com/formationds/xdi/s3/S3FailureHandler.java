package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.protocol.ApiException;
import com.formationds.security.AuthenticationToken;
import com.formationds.spike.later.SyncRequestHandler;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import javax.servlet.ServletException;
import java.util.function.Function;

public class S3FailureHandler implements Function<AuthenticationToken, SyncRequestHandler> {
    private static final Logger logger = LogManager.getLogger(S3FailureHandler.class);

    private Function<AuthenticationToken, SyncRequestHandler> supplier;

    public S3FailureHandler(Function<AuthenticationToken, SyncRequestHandler> supplier) {
        this.supplier = supplier;
    }

    public static S3Failure makeS3Failure(String requestedResource, ApiException e) {
        switch (e.getErrorCode()) {
            case MISSING_RESOURCE:
                return new S3Failure(S3Failure.ErrorCode.NoSuchKey, "No such resource", requestedResource);

            case BAD_REQUEST:
                return new S3Failure(S3Failure.ErrorCode.InvalidRequest, "Invalid request", requestedResource);

            case INTERNAL_SERVER_ERROR:
                return new S3Failure(S3Failure.ErrorCode.InternalError, e.getMessage(), requestedResource);

            case SERVICE_NOT_READY:
                return new S3Failure(S3Failure.ErrorCode.ServiceUnavailable, "The service is unavailable (no storage nodes?)", requestedResource);

            case RESOURCE_ALREADY_EXISTS:
                return new S3Failure(S3Failure.ErrorCode.BucketAlreadyExists, "The requested bucket name is not available. The bucket namespace is shared by all users of the system. Please select a different name and try again.", requestedResource);

            case RESOURCE_NOT_EMPTY:
                return new S3Failure(S3Failure.ErrorCode.BucketNotEmpty, "Bucket is not empty", requestedResource);

            default:
                return new S3Failure(S3Failure.ErrorCode.InternalError, e.getMessage(), requestedResource);
        }
    }

    @Override
    public SyncRequestHandler apply(AuthenticationToken authenticationToken) {
        return (ctx) -> {
            String requestedResource = ctx.getRequestURI();

            try {
                return supplier.apply(authenticationToken).handle(ctx);
            } catch (ApiException e) {
                return makeS3Failure(requestedResource, e);
            } catch (SecurityException e) {
                if (logger.isTraceEnabled()) {
                    logger.trace("S3::FAILURE: " + e.getMessage(), e);
                }

                return new S3Failure(S3Failure.ErrorCode.AccessDenied, "Access denied", requestedResource);
            } catch (ServletException e) {
                return new S3Failure(S3Failure.ErrorCode.InvalidRequest, e.getMessage(), requestedResource);
            }
        };
    }
}
