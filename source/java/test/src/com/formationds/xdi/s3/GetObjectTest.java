package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.apis.BlobDescriptor;
import com.formationds.security.AuthenticationToken;
import com.formationds.web.toolkit.Resource;
import com.formationds.xdi.Xdi;
import com.google.common.collect.Maps;
import org.apache.commons.codec.binary.Hex;
import org.eclipse.jetty.server.Request;
import org.junit.Test;

import java.util.HashMap;

import static org.junit.Assert.assertEquals;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class GetObjectTest {

    public static final String IF_NONE_MATCH = "If-None-Match";

    @Test
    public void testIfMatch() throws Exception {
        Xdi xdi = mock(Xdi.class);
        byte[] digest = new byte[]{1, 2, 3};
        HashMap<String, String> metadata = Maps.newHashMap();
        metadata.put("etag", Hex.encodeHexString(digest));
        BlobDescriptor descriptor = new BlobDescriptor("poop", 0, metadata);
        when(xdi.statBlob(any(), anyString(), anyString(), anyString())).thenReturn(descriptor);
        
        GetObject handler = new GetObject(xdi, AuthenticationToken.ANONYMOUS);
        Request request = mock(Request.class);
        when(request.getHeader(IF_NONE_MATCH)).thenReturn(null);
        Resource resource = handler.handle(request, "poop", "panda");
        assertEquals(200, resource.getHttpStatus());

        byte[] nonMatchingDigest = {42};
        when(request.getHeader(IF_NONE_MATCH)).thenReturn(Hex.encodeHexString(nonMatchingDigest));
        resource = handler.handle(request, "poop", "panda");
        assertEquals(200, resource.getHttpStatus());

        when(request.getHeader(IF_NONE_MATCH)).thenReturn("\"" + Hex.encodeHexString(digest) + "\"");
        resource = handler.handle(request, "poop", "panda");
        assertEquals(304, resource.getHttpStatus());
    }
}
