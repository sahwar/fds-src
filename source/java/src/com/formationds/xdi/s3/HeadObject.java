package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.protocol.BlobDescriptor;
import com.formationds.security.AuthenticationToken;
import com.formationds.spike.later.HttpContext;
import com.formationds.spike.later.SyncRequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.StaticFileHandler;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.Xdi;
import com.formationds.xdi.swift.SwiftUtility;
import org.joda.time.DateTime;

import java.util.Map;

public class HeadObject implements SyncRequestHandler {
    private Xdi xdi;
    private AuthenticationToken token;

    public HeadObject(Xdi xdi, AuthenticationToken token) {
        this.xdi = xdi;
        this.token = token;
    }

    @Override
    public Resource handle(HttpContext contex) throws Exception {
        String volume = contex.getRouteParameter("bucket");
        String object = contex.getRouteParameter("object");

        BlobDescriptor stat = xdi.statBlob(token, S3Endpoint.FDS_S3, volume, object).get();
        Map<String, String> metadata = stat.getMetadata();
        String contentType = metadata.getOrDefault("Content-Type", StaticFileHandler.getMimeType(object));
        String lastModified = metadata.getOrDefault("Last-Modified", SwiftUtility.formatRfc1123Date(DateTime.now()));
        String etag = "\"" + metadata.getOrDefault("etag", "") + "\"";
        long byteCount = stat.getByteCount();
        Resource result = new TextResource("")
                .withContentType(contentType)
                .withHeader("Content-Length", Long.toString(byteCount))
                .withHeader("Last-Modified", lastModified)
                .withHeader("ETag", etag);

        for(Map.Entry<String, String> entry : S3UserMetadataUtility.extractUserMetadata(metadata).entrySet())
            result = result.withHeader(entry.getKey(), entry.getValue());

        return result;
    }
}
