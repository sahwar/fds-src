package com.formationds.iodriver;

import com.formationds.commons.NullArgumentException;
import com.formationds.iodriver.endpoints.Endpoint;
import com.formationds.iodriver.validators.Validator;
import com.formationds.iodriver.workloads.Workload;

/**
 * Coordinates executing a workload on an endpoint.
 */
public final class Driver
{
    /**
     * Constructor.
     * 
     * @param endpoint The endpoint to run {@code workload} on.
     * @param workload The workload to run on {@code endpoint}.
     * @param validator Performs final check on the data {@code listener} gathered.
     */
    public Driver(Endpoint endpoint, Workload workload, Validator validator)
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (workload == null) throw new NullArgumentException("workload");
        if (validator == null) throw new NullArgumentException("validator");

        _endpoint = endpoint;
        _workload = workload;
        _validator = validator;
    }

    /**
     * Run the {@link #getWorkload() workload} setup if it hasn't already been run.
     * 
     * @throws ExecutionException when an error occurs running the workload setup.
     */
    public void ensureSetUp(WorkloadContext context) throws ExecutionException
    {
        if (context == null) throw new NullArgumentException("context");
        
        if (!_isSetUp)
        {
            getWorkload().setUp(getEndpoint(), context);
            _isSetUp = true;
        }
    }

    /**
     * Run the {@link #getWorkload() workload} teardown if it's currently set up.
     * 
     * @throws ExecutionException when an error occurs running the workload teardown.
     */
    public void ensureTearDown(WorkloadContext context) throws ExecutionException
    {
        if (context == null) throw new NullArgumentException("context");
        
        if (_isSetUp)
        {
            getWorkload().tearDown(getEndpoint(), context);
            _isSetUp = false;
        }
    }

    /**
     * Get the endpoint to run the {@link #getWorkload() workload} on.
     * 
     * @return The current property value.
     */
    public Endpoint getEndpoint()
    {
        return _endpoint;
    }

    /**
     * Get the result of running the workload from the {@link #getValidator() validator}.
     * 
     * @return 0 for success, other for failure.
     */
    public int getResult(WorkloadContext context)
    {
        if (context == null) throw new NullArgumentException("context");
        
        Validator validator = getValidator();
        if (validator.isValid(context))
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    /**
     * Get the validator used to decide whether this workload succeeded or failed based on data
     * collected by the {@link #getListener() listener}.
     * 
     * @return The current property value.
     */
    public Validator getValidator()
    {
        return _validator;
    }

    /**
     * Get the workload that is run on the {@link #getEndpoint() endpoint}.
     * 
     * @return The current property value.
     */
    public Workload getWorkload()
    {
        return _workload;
    }

    /**
     * Run the {@link #getWorkload() workload}.
     * 
     * @throws ExecutionException when an error occurs during execution.
     */
    public void runWorkload(WorkloadContext context) throws ExecutionException
    {
        if (context == null) throw new NullArgumentException("context");
        
        ensureSetUp(context);
        try
        {
            getWorkload().runOn(getEndpoint(), context);
        }
        finally
        {
            ensureTearDown(context);
        }
    }

    public static Driver newDriver(Endpoint endpoint,
                                   Workload workload,
                                   Validator validator)
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (workload == null) throw new NullArgumentException("workload");
        if (validator == null) throw new NullArgumentException("validator");

        return new Driver(endpoint, workload, validator);
    }
    
    /**
     * The service endpoint that {@link #_workload} is run on.
     */
    private final Endpoint _endpoint;

    /**
     * Whether {@link #_workload} has run its setup routine on {@link #_endpoint} without running
     * teardown after.
     */
    private boolean _isSetUp;

    /**
     * The instructions to run on {@link #_endpoint}.
     */
    private final Workload _workload;

    /**
     * Checks the data gathered by {@link #_listener} and determines if {@link #_workload} was
     * successful.
     */
    private final Validator _validator;
}
