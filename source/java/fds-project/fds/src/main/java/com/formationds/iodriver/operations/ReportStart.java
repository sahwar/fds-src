package com.formationds.iodriver.operations;

import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.stream.Stream;

import com.formationds.commons.NullArgumentException;
import com.formationds.iodriver.endpoints.Endpoint;
import com.formationds.iodriver.reporters.AbstractWorkloadEventListener;

/**
 * Report workload body beginning.
 */
public class ReportStart extends AbstractOperation
{
    /**
     * Constructor.
     * 
     * @param bucketName The bucket to report begin for.
     */
    public ReportStart(String bucketName)
    {
        if (bucketName == null) throw new NullArgumentException("bucketName");

        _bucketName = bucketName;
    }

    @Override
    // @eclipseFormat:off
    public void accept(Endpoint endpoint,
                       AbstractWorkloadEventListener reporter) throws ExecutionException
    // @eclipseFormat:on
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (reporter == null) throw new NullArgumentException("reporter");

        reporter.reportStart(_bucketName);
    }

    @Override
    public Stream<SimpleImmutableEntry<String, String>> toStringMembers()
    {
        return Stream.concat(super.toStringMembers(),
                             Stream.of(memberToString("bucketName", _bucketName)));
    }
    
    /**
     * The bucket to report begin for.
     */
    private final String _bucketName;
}
