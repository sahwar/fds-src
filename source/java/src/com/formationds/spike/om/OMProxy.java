/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.spike.om;

import com.formationds.util.thrift.svc.SvcLayerClient;
import com.google.common.net.HostAndPort;

/**
 * @author ptinius
 */
public class OMProxy {

    public static void main(String[] args) throws Exception {

        SvcLayerClient client =
            new SvcLayerClient( HostAndPort.fromParts( "localhost", 7000 ) );

        client.getDomainNodes().values().forEach(
            ( n ) -> n.forEach( System.out::println ) );
    }
}
