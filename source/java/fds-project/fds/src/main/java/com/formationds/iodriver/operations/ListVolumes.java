package com.formationds.iodriver.operations;

import java.lang.reflect.Type;
import java.net.URI;
import java.util.ArrayList;
import java.util.Collection;
import java.util.function.Consumer;

import javax.net.ssl.HttpsURLConnection;

import com.formationds.client.v08.model.Volume;
import com.formationds.commons.Fds;
import com.formationds.commons.NullArgumentException;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.iodriver.endpoints.HttpException;
import com.formationds.iodriver.endpoints.OmV8Endpoint;
import com.formationds.iodriver.reporters.AbstractWorkloadEventListener;
import com.google.gson.reflect.TypeToken;

public final class ListVolumes extends AbstractOmV8Operation
{
    public ListVolumes(Consumer<Collection<Volume>> setter)
    {
        if (setter == null) throw new NullArgumentException("setter");
        
        _setter = setter;
    }
    
    @Override
    public void accept(OmV8Endpoint endpoint,
                       HttpsURLConnection connection,
                       AbstractWorkloadEventListener listener) throws ExecutionException
    {
        if (endpoint == null) throw new NullArgumentException("endpoint");
        if (connection == null) throw new NullArgumentException("connection");
        if (listener == null) throw new NullArgumentException("listener");
        
        String volumes;
        try
        {
            volumes = endpoint.doRead(connection);
        }
        catch (HttpException e)
        {
            throw new ExecutionException("Error listing volumes.", e);
        }

        _setter.accept(ObjectModelHelper.toObject(volumes, _VOLUME_LIST_TYPE));
    }

    @Override
    public URI getRelativeUri()
    {
        URI base = Fds.Api.V08.getBase();
        URI getVolumes = Fds.Api.V08.getVolumes();
        
        return base.relativize(getVolumes);
    }

    @Override
    public String getRequestMethod()
    {
        return "GET";
    }
    
    static
    {
        _VOLUME_LIST_TYPE = new TypeToken<ArrayList<Volume>>() {}.getType();
    }
    
    private final Consumer<Collection<Volume>> _setter;
    
    private static final Type _VOLUME_LIST_TYPE;
}
