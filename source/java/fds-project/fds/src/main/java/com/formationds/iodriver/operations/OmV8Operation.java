package com.formationds.iodriver.operations;

import javax.net.ssl.HttpsURLConnection;

import com.formationds.iodriver.endpoints.OmV8Endpoint;
import com.formationds.iodriver.reporters.AbstractWorkloadEventListener;

public interface OmV8Operation extends HttpsOperation
{
    void accept(OmV8Endpoint endpoint,
                HttpsURLConnection connection,
                AbstractWorkloadEventListener listener) throws ExecutionException;
}
