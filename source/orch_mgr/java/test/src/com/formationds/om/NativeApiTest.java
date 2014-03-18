package com.formationds.om;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.util.MutableAcceptor;
import org.junit.Test;

import java.lang.management.ManagementFactory;

public class NativeApiTest {
    @Test
    public void JunitIsSilly() {

    }

    public void testInit() throws Exception {
        System.out.println(ManagementFactory.getRuntimeMXBean().getName());
        new NativeApi();
        NativeApi.startOm(new String[0]);
        Thread.sleep(10000);
        System.out.println("Listing nodes");
        MutableAcceptor<String> acceptor = new MutableAcceptor<>();
        Thread.sleep(1000);
    }
}
