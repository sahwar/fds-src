package com.formationds.smoketest;

import com.formationds.apis.MediaPolicy;
import com.formationds.apis.VolumeSettings;
import com.formationds.apis.VolumeType;
import com.formationds.commons.Fds;
import com.formationds.nfs.*;
import com.formationds.util.ServerPortFinder;
import com.formationds.xdi.AsyncAm;
import com.formationds.xdi.RealAsyncAm;
import com.formationds.xdi.XdiClientFactory;
import com.formationds.xdi.XdiConfigurationApi;
import org.dcache.nfs.vfs.VirtualFileSystem;
import org.joda.time.Duration;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Ignore;
import org.junit.Test;

import java.util.UUID;

import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;


@Ignore
public class NfsTest extends BaseAmTest {
    private static VirtualFileSystem amVfs;
    private static ExportResolver resolver;

    @Test
    public void testMakeJunitHappy() throws Exception {
    }

    private static XdiConfigurationApi config;
    private String volumeName;
    private String fileName;
    private static final int OBJECT_SIZE = 1024 * 1024 * 2;
    private static AsyncAm asyncAm;


    @BeforeClass
    public static void setUpOnce() throws Exception {
        XdiClientFactory xdiCf = new XdiClientFactory();
        config = new XdiConfigurationApi(xdiCf.remoteOmService(Fds.getFdsHost(), 9090));
        int responsePort = new ServerPortFinder().findPort("Async AM response", 10000);
        asyncAm = new RealAsyncAm(Fds.getFdsHost(), 8899, responsePort, Duration.standardSeconds(30));
        asyncAm.start();
        resolver = mock(ExportResolver.class);
        when(resolver.nfsExportId(anyString())).thenReturn(0);
        IoOps ops = new AmOps(asyncAm);
        amVfs = new XdiVfs(resolver, new Counters(), ops);
    }

    @Before
    public void setUp() throws Exception {
        volumeName = UUID.randomUUID().toString();
        fileName = UUID.randomUUID().toString();
        config.createVolume(XdiVfs.DOMAIN, volumeName, new VolumeSettings(OBJECT_SIZE, VolumeType.OBJECT, 0, 0, MediaPolicy.HDD_ONLY), 0);

    }
}
