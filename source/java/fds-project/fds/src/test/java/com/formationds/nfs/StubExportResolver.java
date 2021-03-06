package com.formationds.nfs;

import com.formationds.util.ConsumerWithException;
import com.google.common.collect.Lists;

import java.io.IOException;
import java.util.Collection;

public class StubExportResolver implements ExportResolver {
    public static final int EXPORT_ID = 1;
    private String volumeName;
    private int maxObjectSize;
    private long maxVolumeCapacityInBytes;

    public StubExportResolver(String volumeName, int maxObjectSize, long maxVolumeCapacityInBytes) {
        this.volumeName = volumeName;
        this.maxObjectSize = maxObjectSize;
        this.maxVolumeCapacityInBytes = maxVolumeCapacityInBytes;
    }

    @Override
    public Collection<String> exportNames() {
        return Lists.newArrayList(volumeName);
    }

    @Override
    public int nfsExportId(String volumeName) {
        if (volumeName.equals(this.volumeName)) {
            return EXPORT_ID;
        } else {
            throw new RuntimeException("No such volume");
        }
    }

    @Override
    public String volumeName(int nfsExportId) {
        if (nfsExportId == EXPORT_ID) {
            return volumeName;
        } else {
            throw new RuntimeException("No such volume");
        }
    }

    @Override
    public boolean exists(String volumeName) {
        return this.volumeName.equals(volumeName);
    }

    @Override
    public int objectSize(String volume) {
        return maxObjectSize;
    }

    @Override
    public long maxVolumeCapacityInBytes(String volume) throws IOException {
        return maxVolumeCapacityInBytes;
    }

    @Override
    public void addVolumeDeleteEventHandler(ConsumerWithException<String> consumer) {

    }
}
