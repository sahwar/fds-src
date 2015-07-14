package com.formationds.iodriver.reporters;

import java.util.Collections;
import java.util.Set;

import com.formationds.commons.util.logging.Logger;
import com.formationds.iodriver.model.VolumeQosSettings;
import com.formationds.iodriver.operations.Operation;

public class NullWorkflowEventListener extends AbstractWorkloadEventListener
{
    public NullWorkflowEventListener(Logger logger)
    {
        super(logger);
    }
    
    @Override
    public void addVolume(String name, VolumeQosSettings params)
    {
        // No-op.
    }
    
    @Override
    public void finished()
    {
        // No-op.
    }
    
    @Override
    public VolumeQosStats getStats(String volume)
    {
        throw new IllegalArgumentException("No volumes exist in " + getClass().getName() + ".");
    }
    
    @Override
    public Set<String> getVolumes()
    {
        return Collections.emptySet();
    }
    
    @Override
    public void reportIo(String volume, int count)
    {
        // No-op.
    }
    
    @Override
    public void reportOperationExecution(Operation operation)
    {
        // No-op.
    }
    
    @Override
    public void reportStart(String volume)
    {
        // No-op.
    }
    
    @Override
    public void reportStop(String volume)
    {
        // No-op.
    }
}
