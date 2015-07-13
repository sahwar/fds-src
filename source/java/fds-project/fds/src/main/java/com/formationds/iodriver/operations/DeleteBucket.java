package com.formationds.iodriver.operations;

import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.stream.Stream;

import com.amazonaws.AmazonClientException;
import com.amazonaws.services.s3.AmazonS3Client;

import com.formationds.commons.NullArgumentException;
import com.formationds.iodriver.endpoints.S3Endpoint;
import com.formationds.iodriver.reporters.AbstractWorkflowEventListener;

/**
 * Delete an S3 bucket.
 */
public class DeleteBucket extends S3Operation
{
    /**
     * Constructor.
     * 
     * @param bucketName The name of the bucket to delete.
     */
    public DeleteBucket(String bucketName)
    {
        if (bucketName == null) throw new NullArgumentException("bucketName");

        _bucketName = bucketName;
    }

    @Override
    public void accept(S3Endpoint endpoint,
                       AbstractWorkflowEventListener listener) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (listener == null) throw new NullArgumentException("listener");

        AmazonS3Client client = getClientWrapped(endpoint);
        try
        {
            client.deleteBucket(_bucketName);
        }
        catch (AmazonClientException e)
        {
            throw new ExecutionException("Error deleting bucket.", e);
        }
    }

    @Override
    protected Stream<SimpleImmutableEntry<String, String>> toStringMembers()
    {
        return Stream.concat(super.toStringMembers(),
                             Stream.of(memberToString("bucketName", _bucketName)));
    }
    
    /**
     * The name of the bucket to delete.
     */
    private final String _bucketName;
}
