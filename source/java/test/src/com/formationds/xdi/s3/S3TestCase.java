package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.S3ClientOptions;
import com.amazonaws.services.s3.model.*;
import com.formationds.apis.AmService;
import com.formationds.apis.ConfigurationService;
import com.formationds.apis.VolumeSettings;
import com.formationds.apis.VolumeType;
import com.formationds.demo.Main;
import com.formationds.security.Authenticator;
import com.formationds.security.AuthorizationToken;
import com.formationds.util.Configuration;
import com.formationds.util.Size;
import com.formationds.util.SizeUnit;
import com.sun.security.auth.UserPrincipal;
import org.apache.commons.io.IOUtils;
import org.apache.thrift.protocol.TBinaryProtocol;
import org.apache.thrift.transport.TSocket;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.util.Arrays;

public class S3TestCase {
    //  @Test
    public void cleanupAwsVolumes() throws Exception {
        new Configuration("foo", new String[] {"--console"});
        AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("AKIAINOGA4D75YX26VXQ", "/ZE1BUJ/vJ8BDESUvf5F3pib7lJW+pBa5FTakmjf"));
        Arrays.stream(Main.VOLUMES)
                .forEach(v -> {
                    ObjectListing objectListing = client.listObjects(v);
                    String[] keys = objectListing.getObjectSummaries()
                            .stream()
                            .map(o -> o.getKey())
                            .toArray(i -> new String[i]);

                    DeleteObjectsResult result = client.deleteObjects(new DeleteObjectsRequest(v).withKeys(keys));
                    System.out.println("Deleted " + result.getDeletedObjects().size() + " keys in " + v);
                });
    }

    //@Test
    public void deleteMultipleObjects() throws Exception {
        new Configuration("foo", new String[0]);
        AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("fabrice", "7VpLGuZy7VCKq2B/Z4yEOw=="));
        //AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("fabrice", "9VpLGuZy7VCKq2B/Z4yEOw=="));
        client.setS3ClientOptions(new S3ClientOptions().withPathStyleAccess(true));
        client.setEndpoint("http://localhost:9999");

        Bucket bucket = client.createBucket("foo");
        ObjectMetadata metadata = new ObjectMetadata();
        //metadata.setContentType("image/jpg");
        client.putObject("foo", "foo.jpg", new FileInputStream("/home/fabrice/Downloads/cat3.jpg"), metadata);
        DeleteObjectsResult result = client.deleteObjects(new DeleteObjectsRequest("foo").withKeys("foo.jpg", "bar.jpg"));
        System.out.println(result);
    }

    //@Test
    public void testMount() throws Exception {
        TSocket amTransport = new TSocket("192.168.33.10", 9988);
        amTransport.open();
        AmService.Iface am = new AmService.Client(new TBinaryProtocol(amTransport));

        String omHost = "192.168.33.10";
        TSocket omTransport = new TSocket(omHost, 9090);
        omTransport.open();
        ConfigurationService.Iface config = new ConfigurationService.Client(new TBinaryProtocol(omTransport));

        config.createVolume("fds", "Volume1", new VolumeSettings(1024 * 4, VolumeType.BLOCK, new Size(20, SizeUnit.GB).totalBytes()));
        Thread.sleep(2000);
        am.attachVolume("fds", "Volume1");
    }

    //@Test
    public void testFdsImplementation() throws Exception {
        new Configuration("foo", new String[0]);
        //AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("AKIAINOGA4D75YX26VXQ", "/ZE1BUJ/vJ8BDESUvf5F3pib7lJW+pBa5FTakmjf"));

        AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("fabrice", "7VpLGuZy7VCKq2B/Z4yEOw=="));
        //AmazonS3Client client = new AmazonS3Client(new BasicAWSCredentials("fabrice", "9VpLGuZy7VCKq2B/Z4yEOw=="));
        client.setS3ClientOptions(new S3ClientOptions().withPathStyleAccess(true));
        client.setEndpoint("http://localhost:8000");
        Bucket bucket = client.createBucket("foo");
        ObjectMetadata metadata = new ObjectMetadata();
        //metadata.setContentType("image/jpg");
        client.putObject("foo", "foo.jpg", new FileInputStream("/home/fabrice/Downloads/cat3.jpg"), metadata);
        S3Object foo = client.getObject("foo", "foo.jpg");
        int read = IOUtils.copy(foo.getObjectContent(), new ByteArrayOutputStream());
        System.out.println("Read " + read + " bytes");
    }

    //@Test
    public void makeKey() throws Exception {
        String key = new AuthorizationToken(Authenticator.KEY, new UserPrincipal("fabrice")).getKey().toBase64();
        System.out.println(key);
    }

    @Test
    public void makeJUnitHappy() {

    }
}
