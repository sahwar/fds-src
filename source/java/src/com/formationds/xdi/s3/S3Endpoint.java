package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.security.AuthenticatedRequestContext;
import com.formationds.security.AuthenticationToken;
import com.formationds.spike.later.AsyncWebapp;
import com.formationds.spike.later.HttpContext;
import com.formationds.spike.later.HttpPath;
import com.formationds.spike.later.SyncRequestHandler;
import com.formationds.web.toolkit.*;
import com.formationds.xdi.AsyncStreamer;
import com.formationds.xdi.Xdi;
import org.joda.time.DateTime;
import org.joda.time.format.ISODateTimeFormat;

import javax.crypto.SecretKey;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Supplier;

public class S3Endpoint {
    private final static org.apache.log4j.Logger LOG = org.apache.log4j.Logger.getLogger(S3Endpoint.class);
    private final S3Authenticator authenticator;
    public static final String FDS_S3 = "FDS_S3";
    public static final String FDS_S3_SYSTEM = "FDS_S3_SYSTEM";
    public static final String X_AMZ_COPY_SOURCE = "x-amz-copy-source";
    public static final String S3_DEFAULT_CONTENT_TYPE = "binary/octet-stream";
    private final AsyncWebapp webApp;
    private Xdi xdi;
    private Supplier<AsyncStreamer> xdiAsync;
    private SecretKey secretKey;

    public S3Endpoint(Xdi xdi, Supplier<AsyncStreamer> xdiAsync, SecretKey secretKey,
                      HttpsConfiguration httpsConfiguration, HttpConfiguration httpConfiguration) {
        this.xdi = xdi;
        this.xdiAsync = xdiAsync;
        this.secretKey = secretKey;
        webApp = new AsyncWebapp(httpConfiguration, httpsConfiguration);
        authenticator = new S3Authenticator(xdi.getAuthorizer(), secretKey);
    }

    public static String formatAwsDate(DateTime dateTime) {
        return dateTime.toString(ISODateTimeFormat.dateTime());
    }

    public void start() {
        syncRoute(new HttpPath(HttpMethod.GET, "/:bucket/:object")
                        .withUrlParam("uploadId"),
                (t) -> new MultiPartListParts(xdi, t));

        webApp.route(new HttpPath(HttpMethod.GET, "/:bucket/:object"), ctx ->
                executeAsync(ctx, new AsyncGetObject(xdiAsync.get(), authenticator, xdi.getAuthorizer())));

        syncRoute(HttpMethod.GET, "/", (t) -> new ListBuckets(xdi, t));

        syncRoute(new HttpPath(HttpMethod.PUT, "/:bucket")
                        .withUrlParam("acl")
                        .withHeader("x-amz-acl"),
                (t) -> new PutBucketAcl(xdi, t));

        syncRoute(HttpMethod.PUT, "/:bucket", (t) -> new CreateBucket(xdi, t));
        syncRoute(HttpMethod.DELETE, "/:bucket", (t) -> new DeleteBucket(xdi, t));
        syncRoute(HttpMethod.GET, "/:bucket", (t) -> new ListObjects(xdi, t));
        syncRoute(HttpMethod.HEAD, "/:bucket", (t) -> new HeadBucket(xdi, t));

        syncRoute(new HttpPath(HttpMethod.PUT, "/:bucket/:object")
                        .withHeader(S3Endpoint.X_AMZ_COPY_SOURCE),
                (t) -> new PutObject(xdi, t));

        syncRoute(new HttpPath(HttpMethod.PUT, "/:bucket/:object")
                        .withUrlParam("uploadId"),
                (t) -> new PutObject(xdi, t));

        syncRoute(new HttpPath(HttpMethod.PUT, "/:bucket/:object")
                        .withUrlParam("acl")
                        .withHeader("x-amz-acl"),
                (t) -> new PutObjectAcl(xdi, t));

        webApp.route(new HttpPath(HttpMethod.PUT, "/:bucket/:object"), ctx ->
                executeAsync(ctx, new AsyncPutObject(xdi)));

        syncRoute(new HttpPath(HttpMethod.POST, "/:bucket/:object")
                .withUrlParam("delete"), (t) -> new DeleteMultipleObjects(xdi, t));

        syncRoute(new HttpPath(HttpMethod.POST, "/:bucket/:object")
                .withUrlParam("uploads"), (t) -> new MultiPartUploadInitiate(xdi, t));

        syncRoute(new HttpPath(HttpMethod.POST, "/:bucket/:object")
                .withUrlParam("uploadId"), (t) -> new MultiPartUploadComplete(xdi, t));

        syncRoute(HttpMethod.POST, "/:bucket/:object", (t) -> new PostObjectUpload(xdi, t));

        syncRoute(HttpMethod.HEAD, "/:bucket/:object", (t) -> new HeadObject(xdi, t));
        syncRoute(HttpMethod.DELETE, "/:bucket/:object", (t) -> new DeleteObject(xdi, t));

        webApp.start();
    }

    private CompletableFuture<Void> executeAsync(HttpContext ctx, BiFunction<HttpContext, AuthenticationToken, CompletableFuture<Void>> function) {
        CompletableFuture<Void> cf = null;
        try {
            AuthenticationToken token = new S3Authenticator(xdi.getAuthorizer(), secretKey).authenticate(ctx);
            cf = function.apply(ctx, token);
        } catch (Exception e) {
            cf.completeExceptionally(e);
        }

        return cf.exceptionally(e -> {
            String requestUri = ctx.getRequestURI();
            Resource resource = new TextResource("");
            if (e.getCause() instanceof SecurityException) {
                resource = new S3Failure(S3Failure.ErrorCode.AccessDenied, "Access denied", requestUri);
            } else if (e.getCause() instanceof ApiException && ((ApiException) e.getCause()).getErrorCode().equals(ErrorCode.MISSING_RESOURCE)) {
                resource = new S3Failure(S3Failure.ErrorCode.NoSuchKey, "No such key", requestUri);
            } else {
                LOG.error("Error executing " + ctx.getRequestMethod() + " " + requestUri, e);
                resource = new S3Failure(S3Failure.ErrorCode.InternalError, "Internal error", requestUri);
            }
            resource.renderTo(ctx);
            return null;
        });
    }

    private void syncRoute(HttpMethod method, String route, Function<AuthenticationToken, SyncRequestHandler> f) {
        syncRoute(new HttpPath(method, route), f);
    }

    private void syncRoute(HttpPath path, Function<AuthenticationToken, SyncRequestHandler> f) {
        webApp.route(path, ctx -> CompletableFuture.runAsync(() -> {
                    Resource resource = new TextResource("");
                    try {
                        AuthenticationToken token = new S3Authenticator(xdi.getAuthorizer(), secretKey).authenticate(ctx);
                        AuthenticatedRequestContext.begin(token);
                        Function<AuthenticationToken, SyncRequestHandler> errorHandler = new S3FailureHandler(f);
                        resource = errorHandler.apply(token).handle(ctx);
                    } catch (SecurityException e) {
                        resource = new S3Failure(S3Failure.ErrorCode.AccessDenied, "Access denied", ctx.getRequestURI());
                    } catch (Exception e) {
                        LOG.debug("Got an exception: ", e);
                        resource = new S3Failure(S3Failure.ErrorCode.InternalError, "Internal error", ctx.getRequestURI());
                    } finally {
                        AuthenticatedRequestContext.complete();
                        resource.renderTo(ctx);
                    }
                })
        );
    }
}
