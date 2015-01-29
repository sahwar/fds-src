package com.formationds.iodriver;

import java.util.function.Consumer;
import java.util.stream.Stream;

import com.formationds.iodriver.endpoints.Endpoint;
import com.formationds.iodriver.operations.ExecutionException;
import com.formationds.iodriver.operations.Operation;

public class Workload<EndpointT extends Endpoint<EndpointT, OperationT>,
                      OperationT extends Operation<OperationT, EndpointT>>
{
    public Workload() { }

    public final void runOn(EndpointT endpoint) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");

        ensureInitialized();
        
        untunnel(ops -> ops.forEach(tunnel(op -> op.accept(endpoint), ExecutionException.class)),
                 _operations,
                 ExecutionException.class);
    }

    public final void setUp(EndpointT endpoint) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");

        ensureInitialized();
        
        untunnel(ops -> ops.forEach(tunnel(op -> op.accept(endpoint), ExecutionException.class)),
                 _setup,
                 ExecutionException.class);
    }

    public final void tearDown(EndpointT endpoint) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");

        ensureInitialized();
        
//        untunnel(ops -> ops.forEach(tunnel(op -> op.accept(endpoint), ExecutionException.class)),
//                 _teardown,
//                 ExecutionException.class);
    }
    
    protected Stream<OperationT> createOperations()
    {
        return Stream.empty();
    }
    
    protected Stream<OperationT> createSetup()
    {
        return Stream.empty();
    }
    
    protected Stream<OperationT> createTeardown()
    {
        return Stream.empty();
    }
    
    protected void ensureInitialized()
    {
        if (_operations == null)
        {
            _operations = createOperations();
            _setup = createSetup();
            _teardown = createTeardown();
        }
    }
    
    @FunctionalInterface
    private interface ExceptionThrowingConsumer<T, E extends Exception>
    {
        void accept(T t) throws E;
    }

    private final <T extends Throwable> void rethrowTunneled(TunneledException e,
                                                             Class<T> expectedType) throws T
    {
        if (e == null) throw new NullArgumentException("e");
        if (expectedType == null) throw new NullArgumentException("expectedType");

        try
        {
            throw e.getCause();
        }
        catch (RuntimeException re)
        {
            throw re;
        }
        catch (Throwable t)
        {
            if (expectedType.isAssignableFrom(t.getClass()))
            {
                @SuppressWarnings("unchecked")
                T typedT = (T)t;
                throw typedT;
            }
            throw new RuntimeException("Unexpected tunneled exception type.", t);
        }
    }

    private final <T, E extends Exception>
            Consumer<? super T>
            tunnel(ExceptionThrowingConsumer<? super T, E> func, Class<E> tunnelType)
    {
        if (func == null) throw new NullArgumentException("func");
        if (tunnelType == null) throw new NullArgumentException("tunnelType");

        return arg ->
        {
            try
            {
                func.accept(arg);
            }
            catch (RuntimeException re)
            {
                throw re;
            }
            catch (Throwable t)
            {
                if (tunnelType.isAssignableFrom(t.getClass()))
                {
                    throw new TunneledException(t);
                }
                else
                {
                    throw new RuntimeException("Unexpected exception.", t);
                }
            }
        };
    }

    private <T, E extends Exception> void untunnel(ExceptionThrowingConsumer<? super T, E> func,
                                                   T object,
                                                   Class<E> tunnelType) throws E
    {
        try
        {
            func.accept(object);
        }
        catch (TunneledException e)
        {
            rethrowTunneled(e, tunnelType);
        }
    }

    private Stream<OperationT> _operations;

    private Stream<OperationT> _setup;

    private Stream<OperationT> _teardown;
}
