package com.formationds.iodriver.operations;

import com.amazonaws.services.s3.AmazonS3Client;

import com.formationds.commons.NullArgumentException;
import com.formationds.iodriver.endpoints.S3Endpoint;
import com.formationds.iodriver.reporters.AbstractWorkflowEventListener;

/**
 * Execute an arbitrary operation on an S3 endpoint.
 */
public class LambdaS3Operation extends S3Operation
{
    /**
     * Implement this interface.
     */
    @FunctionalInterface
    public interface Delegate
    {
        // @eclipseFormat:off
        void exec(S3Endpoint endpoint,
                  AmazonS3Client client,
                  AbstractWorkflowEventListener reporter) throws ExecutionException;
        // @eclipseFormat:on
    }

    /**
     * Constructor.
     * 
     * @param action A delegate that does not take any arguments.
     */
    public LambdaS3Operation(Runnable action)
    {
        this(createDelegate(action));
    }

    /**
     * Constructor.
     * 
     * @param delegate A delegate that will get the endpoint, S3 client, and listener that S3
     *            operations normally get.
     */
    public LambdaS3Operation(Delegate delegate)
    {
        if (delegate == null) throw new NullArgumentException("delegate");

        _delegate = delegate;
    }

    @Override
    // @eclipseFormat:off
    public void exec(S3Endpoint endpoint,
                     AmazonS3Client client,
                     AbstractWorkflowEventListener reporter) throws ExecutionException
    // @eclipseFormat:on
    {
        _delegate.exec(endpoint, client, reporter);
    }

    /**
     * The delegate to call when this operation is executed.
     */
    private final Delegate _delegate;

    /**
     * Create a full delegate from an action.
     * 
     * @param action The action to wrap.
     * 
     * @return A full delegate that just ignores the parameters.
     */
    private static Delegate createDelegate(Runnable action)
    {
        if (action == null) throw new NullArgumentException("action");

        return (e, c, r) -> action.run();
    }
}
