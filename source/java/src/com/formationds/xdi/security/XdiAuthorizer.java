package com.formationds.xdi.security;

import com.formationds.apis.*;
import com.formationds.protocol.BlobDescriptor;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authenticator;
import com.formationds.security.Authorizer;
import com.formationds.util.blob.Mode;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.xdi.AsyncAm;
import com.formationds.xdi.XdiConfigurationApi;
import com.formationds.xdi.s3.S3Endpoint;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.collect.Maps;
import org.apache.log4j.Logger;
import org.apache.thrift.TException;

import javax.security.auth.login.LoginException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

public class XdiAuthorizer {
    private static final Logger LOG = Logger.getLogger(XdiAuthorizer.class);
    private Authenticator authenticator;
    private Authorizer authorizer;
    private ConfigurationApi config;
    private final BucketMetadataCache cache;
    private AsyncAm asyncAm;

    public XdiAuthorizer(Authenticator authenticator, Authorizer authorizer, AsyncAm asyncAm, ConfigurationApi config) {
        this.authenticator = authenticator;
        this.authorizer = authorizer;
        this.asyncAm = asyncAm;
        this.config = config;
        cache = new BucketMetadataCache(config, asyncAm);
        cache.start();
    }

    public static BlobDescriptor withAcl(BlobDescriptor blobDescriptor, XdiAcl.Value acl) {
        Map<String, String> metadata = blobDescriptor.getMetadata();
        metadata.put(XdiAcl.X_AMZ_ACL, acl.getCommonName());
        return blobDescriptor;
    }

    public void updateBlobAcl(AuthenticationToken token, String bucketName, String objectName, XdiAcl.Value newAcl) throws Exception {
        BlobDescriptor blobDescriptor = asyncAm.statBlob(S3Endpoint.FDS_S3, bucketName, objectName).get();
        if (!hasBlobPermission(token, bucketName, Intent.changePermissions, blobDescriptor.getMetadata())) {
            throw new SecurityException();
        }
        blobDescriptor = withAcl(blobDescriptor, newAcl);
        TxDescriptor tx = asyncAm.startBlobTx(S3Endpoint.FDS_S3, bucketName, objectName, 1).get();
        asyncAm.updateMetadata(S3Endpoint.FDS_S3, bucketName, objectName, tx, blobDescriptor.getMetadata()).get();
        asyncAm.commitBlobTx(S3Endpoint.FDS_S3, bucketName, objectName, tx).get();
    }

    public boolean hasBlobPermission(AuthenticationToken token, String bucket, Intent intent, Map<String, String> blobMetadata) {
        if (authorizer.ownsVolume(token, bucket)) {
            return true;
        }

        if (hasVolumePermission(token, bucket, intent)) {
            return true;
        }

        XdiAcl.Value objectAcl = XdiAcl.parse(blobMetadata);
        return objectAcl.allow(intent);
    }

    public boolean hasVolumePermission(AuthenticationToken token, String volume, Intent intent) {
        if (authorizer.ownsVolume(token, volume)) {
            return true;
        }

        Map<String, String> volumeMetadata = cache.getMetadata(volume);
        XdiAcl.Value acl = XdiAcl.parse(volumeMetadata);
        return acl.allow(intent);
    }

    public boolean hasToplevelPermission(AuthenticationToken token) {
        if (authenticator.allowAll()) {
            return true;
        } else {
            return !token.equals(AuthenticationToken.ANONYMOUS);
        }
    }

    public long tenantId(AuthenticationToken token) {
        return authorizer.tenantId(token);
    }

    public boolean allowAll() {
        return authenticator.allowAll();
    }

    public AuthenticationToken currentToken(String principal) throws LoginException {
        return authenticator.currentToken(principal);
    }

    public AuthenticationToken authenticate(String login, String password) throws LoginException {
        return authenticator.authenticate(login, password);
    }

    public AuthenticationToken parseToken(String tokenHeader) throws LoginException {
        return authenticator.parseToken(tokenHeader);
    }

    public void updateBucketAcl(AuthenticationToken token, String bucketName, XdiAcl.Value newAcl) throws Exception {
        if (!hasVolumePermission(token, bucketName, Intent.changePermissions)) {
            throw new SecurityException();
        }
        VolumeDescriptor volumeDescriptor = config.statVolume(S3Endpoint.FDS_S3, bucketName);
        String systemVolume = XdiConfigurationApi.systemFolderName(volumeDescriptor.getTenantId());
        createVolumeIfNeeded(systemVolume, volumeDescriptor.getTenantId());
        Map<String, String> metadata = new HashMap<String, String>();
        metadata.put(XdiAcl.X_AMZ_ACL, newAcl.getCommonName());
        String blobName = makeBucketAclBlobName(volumeDescriptor.getVolId());
        asyncAm.updateBlobOnce(S3Endpoint.FDS_S3, systemVolume, blobName, Mode.TRUNCATE.getValue(), ByteBuffer.allocate(0), 0, new ObjectOffset(0), metadata).get();
        cache.scheduleRefresh(false);
    }

    private void createVolumeIfNeeded(String volume, long tenantId) {
        try {
            config.createVolume(S3Endpoint.FDS_S3, volume, new VolumeSettings(1024 * 1024 * 2, VolumeType.OBJECT, 0, 0, MediaPolicy.HDD_ONLY), tenantId);
        } catch (TException e) {

        }
    }

    private String makeBucketAclBlobName(long volumeId) {
        return "fds_bucket_acl_" + volumeId;
    }


    private class BucketMetadataCache {
        private final Cache<String, CompletableFuture<Map<String, String>>> cache;
        private final ConfigurationApi config;
        private final AsyncAm am;

        BucketMetadataCache(ConfigurationApi config, AsyncAm am) {
            this.config = config;
            this.am = am;
            cache = CacheBuilder.newBuilder()
                    .expireAfterWrite(10, TimeUnit.MINUTES)
                    .build();
        }

        void start() {
            scheduleRefresh(false);
            Thread thread = new Thread(() -> {
                while (true) {
                    try {
                        Thread.sleep(5000);
                        scheduleRefresh(true);
                    } catch (Exception e) {
                        LOG.error("Error refreshing bucket metadata cache", e);
                    }
                }
            });
            thread.setName("Volume metadata cache updater");
            thread.start();
        }

        Map<String, String> getMetadata(String bucketName) {
            try {
                CompletableFuture<Map<String, String>> cf = cache.get(bucketName, () -> CompletableFuture.completedFuture(new HashMap<String, String>()));
                if (cf.isDone()) {
                    return cf.get();
                } else {
                    return new HashMap<>();
                }
            } catch (Exception e) {
                LOG.error("Error polling bucket metadata cache", e);
            }

            return new HashMap<>();
        }

        void scheduleRefresh(boolean deferrable) {
            try {
                config.listVolumes(S3Endpoint.FDS_S3)
                        .stream()
                        .forEach(vd -> {
                            String systemVolume = XdiConfigurationApi.systemFolderName(vd.getTenantId());
                            String blobName = makeBucketAclBlobName(vd.getVolId());
                            CompletableFuture<BlobDescriptor> blobFuture = am.statBlob(S3Endpoint.FDS_S3, systemVolume, blobName)
                                    .exceptionally(e -> new BlobDescriptor(vd.getName(), 0, new HashMap<String, String>()));
                            CompletableFuture<Map<String, String>> cf = blobFuture.thenApply(bd -> bd.getMetadata());
                            cache.put(vd.getName(), cf);

                            if (!deferrable) {
                                try {
                                    cf.get();
                                } catch (Exception e) {
                                    LOG.error("Error refreshing cache", e);
                                    cache.put(vd.getName(), CompletableFuture.completedFuture(Maps.newHashMap()));
                                }
                            }

                        });
            } catch (TException e) {
                throw new RuntimeException(e);
            }
        }
    }
}
