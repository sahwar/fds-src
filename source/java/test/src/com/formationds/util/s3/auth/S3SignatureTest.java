package com.formationds.util.s3.auth;

import com.amazonaws.AmazonClientException;
import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.S3ClientOptions;
import com.amazonaws.services.s3.model.*;
import com.formationds.commons.util.IoStreamUtil;
import com.formationds.spike.later.AsyncWebapp;
import com.formationds.spike.later.HttpContext;
import com.formationds.spike.later.HttpPath;
import com.formationds.web.toolkit.HttpConfiguration;
import com.formationds.xdi.s3.S3Failure;
import junit.framework.Assert;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.*;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;


public class S3SignatureTest {
    @Test
    public void testBasicAuth() throws Exception {
        BasicAWSCredentials credentials = new BasicAWSCredentials("proot preet", "waxy meat");
        System.setProperty("org.eclipse.jetty.http.HttpParser.STRICT", "true"); // disable jetty header modification

        try(SignatureGeneratorTestContext ctx = new SignatureGeneratorTestContext(credentials, 10293)) {

            // buckets
            ctx.test(client -> client.createBucket("ptath"));
            ctx.test(client -> client.deleteBucket("zaub"));
            ctx.test(client -> client.listBuckets());
            ctx.test(client -> client.listObjects("buckey"));
            ctx.test(client -> client.listObjects("buckey", "truds"));

            // objects
            ObjectMetadata weirdContentType = new ObjectMetadata();
            weirdContentType.setContentType("SLOPPY PRETZEL IMAGES");


            ctx.test(client -> client.getObject("potato", "ole"));
            ctx.test(client -> client.putObject("muffins", "beans", makeStream(), new ObjectMetadata()));
            ctx.test(client -> client.putObject("muffins", "beans", makeStream(), weirdContentType));
            ctx.test(client -> client.deleteObject("mop", "cand"));
            ctx.test(client -> client.deleteObjects(new DeleteObjectsRequest("mop").withKeys("sped", "maff", "parp")));
            ctx.test(client -> client.copyObject("smoop", "src", "smoop", "dst"));

            // multipart
            ctx.test(client -> client.initiateMultipartUpload(new InitiateMultipartUploadRequest("wax", "breeks")));
            ctx.test(client -> client.uploadPart(new UploadPartRequest().withPartNumber(1).withUploadId("proop").withBucketName("preepys").withKey("meps").withInputStream(makeStream())));
            ctx.test(client -> client.copyPart(new CopyPartRequest()
                    .withPartNumber(1)
                    .withSourceBucketName("proop")
                    .withDestinationBucketName("smuckie")
                    .withSourceKey("preep")
                    .withDestinationKey("chuds")
                    .withUploadId("mops")));
            ctx.test(client -> client.abortMultipartUpload(new AbortMultipartUploadRequest("prap", "flarp", "flarrrn")));
            ctx.test(client -> client.completeMultipartUpload(new CompleteMultipartUploadRequest("prap", "flarp", "flarrrn", new ArrayList<PartETag>())));
            ctx.test(client -> client.listParts(new ListPartsRequest("buckey", "smap", "weefis")));
            ctx.test(client -> client.listMultipartUploads(new ListMultipartUploadsRequest("rooper")));

            // acls
            ctx.test(client -> client.setBucketAcl("wax", CannedAccessControlList.AuthenticatedRead));
            ctx.test(client -> client.setObjectAcl("prap", "skooge", CannedAccessControlList.AuthenticatedRead));
        }
    }

    private InputStream makeStream() {
        return new ByteArrayInputStream(new byte[]{1, 2, 3, 4});
    }

    class SignatureGeneratorTestContext implements AutoCloseable {
        public final AuthorizerReferenceServer authReferenceServer;
        public final AWSCredentials credentials;
        public final AWSCredentials badCredentials;
        private final AmazonS3Client s3client;
        private final AmazonS3Client badS3Client;
        Exception exc;

        public SignatureGeneratorTestContext(AWSCredentials credentials, int port) throws Exception {
            authReferenceServer = new AuthorizerReferenceServer(port, credentials);
            this.credentials = credentials;
            this.badCredentials = new BasicAWSCredentials(credentials.getAWSSecretKey() + "wrong", credentials.getAWSAccessKeyId() + "wrong");
            s3client = new AmazonS3Client(credentials);
            s3client.setS3ClientOptions(new S3ClientOptions().withPathStyleAccess(true));
            s3client.setEndpoint("http://localhost:" + port);

            badS3Client = new AmazonS3Client(badCredentials);
            badS3Client.setS3ClientOptions(new S3ClientOptions().withPathStyleAccess(true));
            badS3Client.setEndpoint("http://localhost:" + port);
        }

        public void test(Consumer<AmazonS3Client> operation) {
            // v2 auth
            try {
                operation.accept(s3client);
            } catch(AmazonClientException ex) {
                // do nothing
            }

            if(!authReferenceServer.isLastAuthSuccessful())
                throw new AssertionError("Expecting successful auth (v2)", authReferenceServer.lastSecurityException);

            try {
                operation.accept(badS3Client);
            } catch(AmazonClientException ex) {
                // do nothing
            }

            Assert.assertFalse(authReferenceServer.isLastAuthSuccessful());


            // try with v4 auth
            System.setProperty(SDKGlobalConfiguration.ENABLE_S3_SIGV4_SYSTEM_PROPERTY, "true");

            try {
                operation.accept(s3client);
            } catch(AmazonClientException ex) {

            }

            if(!authReferenceServer.isLastAuthSuccessful())
                throw new AssertionError("Expecting successful auth (v4)", authReferenceServer.lastSecurityException);

            try {
                operation.accept(badS3Client);
            } catch(AmazonClientException ex) {

            }

            Assert.assertFalse(authReferenceServer.isLastAuthSuccessful());

            System.clearProperty(SDKGlobalConfiguration.ENABLE_S3_SIGV4_SYSTEM_PROPERTY);
        }

        @Override
        public void close() throws Exception {
            authReferenceServer.close();
        }
    }

    static class AuthorizerReferenceServer implements AutoCloseable {
        private final AsyncWebapp app;
        private final Thread serverThread;
        private final int port;
        private final AWSCredentials credentials;
        private SecurityException lastSecurityException;

        public AuthorizerReferenceServer(int port, AWSCredentials credentials) throws Exception {
            this.port = port;
            this.credentials = credentials;
            app = new AsyncWebapp(new HttpConfiguration(port), null);
            HttpPath path = new HttpPath().withPredicate(req -> true);
            app.route(path, this::handle);
            serverThread = new Thread(app::start);
            serverThread.start();
        }

        public CompletableFuture<Void> handle(HttpContext context) {
            lastSecurityException = null;
            AuthenticationNormalizer normalizer = new AuthenticationNormalizer();
            try {
                HttpContext authCtx = normalizer.authenticatedContext(credentials, context);
                byte[] data = IoStreamUtil.buffer(authCtx.getInputStream());
            } catch(SecurityException ex) {
                lastSecurityException = ex;
            } catch (IOException e) {
                throw new RuntimeException();
            }

            closeRequest(context);
            return CompletableFuture.completedFuture(null);
        }

        private void closeRequest(HttpContext ctx) {
            try {
                S3Failure failure = new S3Failure(S3Failure.ErrorCode.NoSuchKey, "Hello, world!", "foo");
                ByteArrayOutputStream baos = new ByteArrayOutputStream();
                failure.render(baos);
                byte[] bytes = baos.toByteArray();

                ctx.setResponseStatus(404);
                ctx.addResponseHeader("Content-Length", Integer.toString(bytes.length));
                ctx.getOutputStream().write(bytes);
                ctx.getOutputStream().close();

            } catch (Exception ex) {
                throw new RuntimeException("exception while closing prior request", ex);
            }
        }

        @Override
        public void close() throws Exception {
            app.stop();
            serverThread.join();
        }

        public boolean isLastAuthSuccessful() {
            return lastSecurityException == null;
        }
        public SecurityException getLastSecurityException() {
            return lastSecurityException;
        }
    }
}

