package com.formationds.iodriver.operations;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.function.Supplier;

import com.amazonaws.AmazonClientException;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.model.ObjectMetadata;
import com.formationds.commons.NullArgumentException;
import com.formationds.iodriver.endpoints.S3Endpoint;
import com.formationds.iodriver.reporters.WorkflowEventListener;

/**
 * Create an object in an S3 bucket.
 */
public final class CreateObject extends S3Operation
{
    /**
     * Constructor. Actions will be reported.
     * 
     * @param bucketName The name of the bucket to create the object in.
     * @param key The key of the object to create.
     * @param content The content of the object to create.
     */
    public CreateObject(String bucketName, String key, String content)
    {
        this(bucketName, key, content, true);
    }

    /**
     * Constructor.
     * 
     * @param bucketName The name of the bucket to create the object in.
     * @param key The key of the object to create.
     * @param content The content of the object to create.
     * @param doReporting Whether this object's actions should be reported to workload listener.
     *            Should be false during setup or warmup or teardown, for example.
     */
    public CreateObject(String bucketName, String key, String content, boolean doReporting)
    {
        this(bucketName, key, getBytes(content), doReporting);
    }

    /**
     * Constructor.
     * 
     * @param bucketName The name of the bucket to create the object in.
     * @param key The key of the object to create.
     * @param content The content of the object to create.
     * @param doReporting Whether this object's actions should be reported to workload listener.
     *            Should be false during setup or warmup or teardown, for example.
     */
    public CreateObject(String bucketName, String key, byte[] content, boolean doReporting)
    {
        this(bucketName, key, () -> toInputStream(content), getLength(content), doReporting);
    }

    /**
     * Constructor.
     * 
     * @param bucketName The name of the bucket to create.
     * @param key The key of the object to create.
     * @param input Supplies content for objects.
     * @param contentLength The length of the content returned by {@code input}.
     * @param doReporting Whether this object's actions should be reported to workload listener.
     *            Should be false during setup or warmup or teardown, for example.
     */
    public CreateObject(String bucketName,
                        String key,
                        Supplier<InputStream> input,
                        long contentLength,
                        boolean doReporting)
    {
        if (bucketName == null) throw new NullArgumentException("bucketName");
        if (key == null) throw new NullArgumentException("key");
        if (input == null) throw new NullArgumentException("input");
        if (contentLength < 0) throw new IllegalArgumentException("contentLength must be >= 0.");

        _bucketName = bucketName;
        _contentLength = contentLength;
        _key = key;
        _input = input;
        _doReporting = doReporting;
    }

    @Override
    public void exec(S3Endpoint endpoint, AmazonS3Client client, WorkflowEventListener reporter) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (client == null) throw new NullArgumentException("client");
        if (reporter == null) throw new NullArgumentException("reporter");

        ObjectMetadata metadata = new ObjectMetadata();
        metadata.setContentLength(_contentLength);
        try (InputStream input = _input.get())
        {
            client.putObject(_bucketName, _key, input, metadata);
        }
        catch (AmazonClientException e)
        {
            throw new ExecutionException("Error creating file.", e);
        }
        catch (IOException e)
        {
            throw new ExecutionException("Error closing input stream.", e);
        }

        if (_doReporting)
        {
            reporter.reportIo(_bucketName, IO_COST);
        }
    }

    /**
     * The name of the bucket to create the object in.
     */
    private final String _bucketName;

    /**
     * The length in bytes of the object's content.
     */
    private final long _contentLength;

    /**
     * Whether actions should be reported.
     */
    private final boolean _doReporting;

    /**
     * Supplies content when creating objects.
     */
    private final Supplier<InputStream> _input;

    /**
     * The key of the object to create.
     */
    private final String _key;

    /**
     * The number of system I/Os this operation costs.
     */
    private static final int IO_COST;
    
    /**
     * Static constructor.
     */
    static
    {
        IO_COST = 3;
    }
    
    /**
     * Get the length of a byte array. Called from constructor.
     * 
     * @param content The content to get the length of.
     * 
     * @return The length of {@code content}.
     */
    private static final int getLength(byte[] content)
    {
        if (content == null) throw new NullArgumentException("content");

        return content.length;
    }

    /**
     * Get the bytes for a string.
     * 
     * @param content The string to turn into bytes.
     * 
     * @return The bytes of the string in UTF-8.
     */
    private static final byte[] getBytes(String content)
    {
        if (content == null) throw new NullArgumentException("content");

        return content.getBytes(StandardCharsets.UTF_8);
    }

    /**
     * Create a stream from a byte array.
     * 
     * @param content The byte array to stream.
     * 
     * @return {@code content} as a stream.
     */
    private static final ByteArrayInputStream toInputStream(byte[] content)
    {
        if (content == null) throw new NullArgumentException("content");

        return new ByteArrayInputStream(content);
    }
}
