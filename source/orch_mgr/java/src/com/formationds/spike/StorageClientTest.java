package com.formationds.spike;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import FDS_ProtocolInterface.*;
import org.apache.thrift.TException;
import org.apache.thrift.protocol.TProtocol;

import java.nio.ByteBuffer;

public class StorageClientTest {
    public static void main(String[] args) throws Exception {
        int port = 6666;
        new FdspServer().start(port, new FDSP_DataPathResp.Processor<>(new DataResponseHandler()));

        TProtocol protocol = new FdspClient().handshake("localhost", 8904);
        Mutable<Dlt> dltCache = new Mutable<>();

        FDSP_DataPathReq.Client dataClient = new FDSP_DataPathReq.Client(protocol);
        FDSP_MetaDataPathReq.Client metadataClient = new FDSP_MetaDataPathReq.Client(protocol);
        metadataClient.UpdateCatalogObject(new FDSP_MsgHdrType(), new FDSP_UpdateCatalogType());

        FDS_ObjectIdType objectId = new FDS_ObjectIdType("poop", (byte) 0);
        Dlt dlt = dltCache.awaitValue();
        dataClient.PutObject(new FDSP_MsgHdrType(), new FDSP_PutObjType(objectId, 4, 0, (int) dlt.getVersion(), ByteBuffer.wrap(new byte[]{0, 1, 2, 3}), ByteBuffer.wrap(dlt.serialize())));
    }
}

class MetadataResponseHandler implements FDSP_MetaDataPathReq.Iface {
    @Override
    public void UpdateCatalogObject(FDSP_MsgHdrType fdsp_msg, FDSP_UpdateCatalogType cat_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(cat_obj_req);
    }

    @Override
    public void QueryCatalogObject(FDSP_MsgHdrType fdsp_msg, FDSP_QueryCatalogType cat_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(cat_obj_req);
    }

    @Override
    public void DeleteCatalogObject(FDSP_MsgHdrType fdsp_msg, FDSP_DeleteCatalogType cat_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(cat_obj_req);
    }

    @Override
    public void GetVolumeBlobList(FDSP_MsgHdrType fds_msg, FDSP_GetVolumeBlobListReqType blob_list_req) throws TException {
        System.out.println(fds_msg);
        System.out.println(blob_list_req);
    }
}

class DataResponseHandler implements FDSP_DataPathResp.Iface {
    @Override
    public void GetObjectResp(FDSP_MsgHdrType fdsp_msg, FDSP_GetObjType get_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(get_obj_req);
    }

    @Override
    public void PutObjectResp(FDSP_MsgHdrType fdsp_msg, FDSP_PutObjType put_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(put_obj_req);
    }

    @Override
    public void DeleteObjectResp(FDSP_MsgHdrType fdsp_msg, FDSP_DeleteObjType del_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(del_obj_req);
    }

    @Override
    public void OffsetWriteObjectResp(FDSP_MsgHdrType fdsp_msg, FDSP_OffsetWriteObjType offset_write_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(offset_write_obj_req);
    }

    @Override
    public void RedirReadObjectResp(FDSP_MsgHdrType fdsp_msg, FDSP_RedirReadObjType redir_write_obj_req) throws TException {
        System.out.println(fdsp_msg);
        System.out.println(redir_write_obj_req);
    }
}