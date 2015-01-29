package com.formationds.xdi.s3;

import com.formationds.apis.BlobDescriptor;
import com.formationds.security.AuthenticationToken;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.Xdi;
import org.eclipse.jetty.server.Request;

import java.util.Map;

public class PutObjectAcl implements RequestHandler {
    public static final String X_AMZ_ACL = "x-amz-acl";
    public static final String PUBLIC_READ = "public-read";
    public static final String PUBLIC_READ_WRITE = "public-read-write";
    public static final String PRIVATE = "private";
    private Xdi xdi;
    private AuthenticationToken token;

    public PutObjectAcl(Xdi xdi, AuthenticationToken token) {
        this.xdi = xdi;
        this.token = token;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String bucketName = requiredString(routeParameters, "bucket");
        String objectName = requiredString(routeParameters, "object");

        String aclName = request.getHeader(X_AMZ_ACL).toLowerCase();
        if (PUBLIC_READ.equals(aclName) ||
                PUBLIC_READ_WRITE.equals(aclName) ||
                PRIVATE.equals(aclName)) {
            BlobDescriptor blobDescriptor = xdi.statBlob(token, S3Endpoint.FDS_S3, bucketName, objectName);
            Map<String, String> metadata = blobDescriptor.getMetadata();
            metadata.put(X_AMZ_ACL, aclName);
            xdi.updateMetadata(token, S3Endpoint.FDS_S3, bucketName, objectName, metadata);
        }

        return new TextResource("");
    }
}