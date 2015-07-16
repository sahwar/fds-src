package com.formationds.fdsdiff.workloads;

import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;
import java.util.stream.Stream;

import com.formationds.client.v08.model.Tenant;
import com.formationds.client.v08.model.User;
import com.formationds.client.v08.model.Volume;
import com.formationds.commons.NullArgumentException;
import com.formationds.fdsdiff.SystemContent;
import com.formationds.iodriver.endpoints.OmV7Endpoint;
import com.formationds.iodriver.operations.GetTenants;
import com.formationds.iodriver.operations.GetUsers;
import com.formationds.iodriver.operations.ListVolumes;
import com.formationds.iodriver.operations.Operation;
import com.formationds.iodriver.workloads.Workload;

public final class GetSystemConfigWorkload extends Workload
{
	public GetSystemConfigWorkload(SystemContent contentContainer, boolean logOperations)
	{
		super(logOperations);
		
		if (contentContainer == null) throw new NullArgumentException("contentContainer");
		
		_contentContainer = contentContainer;
	}

	public SystemContent getContentContainer()
	{
	    return _contentContainer;
	}
	
    @Override
    public Class<?> getEndpointType()
    {
        return OmV7Endpoint.class;
    }
	
	@Override
	protected List<Stream<Operation>> createOperations()
	{
		Consumer<Collection<Tenant>> readTenants = _contentContainer::setTenants;
		Consumer<Collection<Volume>> readVolumes = _contentContainer::setVolumes;
		Consumer<Collection<User>> readUsers = _contentContainer::setUsers;
		
		return Collections.singletonList(Stream.of(new GetTenants(readTenants),
		                                           new ListVolumes(readVolumes),
		                                           new GetUsers(readUsers)));
	}
	
	private final SystemContent _contentContainer;
}
