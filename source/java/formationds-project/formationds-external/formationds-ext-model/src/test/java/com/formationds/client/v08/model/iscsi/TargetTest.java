/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.client.v08.model.iscsi;

import com.formationds.client.v08.model.VolumeSettings;
import com.formationds.rest.v08.model.GSONTest;
import com.google.gson.FieldNamingPolicy;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.LongSerializationPolicy;
import org.junit.Assert;
import org.junit.Test;

import java.util.UUID;

/**
 * @author ptinius
 */
public class TargetTest
{
    static public Gson gson( ) { return new GsonBuilder().setDateFormat( "YYYYMMdd-hhmmss.SSSSS" )
                                                         .setFieldNamingPolicy( FieldNamingPolicy.IDENTITY )
                                                         .setPrettyPrinting()
                                                         .registerTypeAdapter( VolumeSettings.class,
                                                                              new GSONTest.VolumeSettingsAdapter() )
                                                         .setLongSerializationPolicy( LongSerializationPolicy.STRING ).create( ); }

    @Test
    public void testTargetGroup() {
        final Target target =
            new Target.Builder()
                      .withIncomingUser( new Credentials( "username",
                                                           "userpassword" ) )
                      .withIncomingUser( new Credentials( "username1",
                                                           "userpassword1" ) )
                      .withOutgoingUser( new Credentials( "ouser","opasswd" ) )
                      .withLun( new LUN.Builder()
                                       .withLun( "volume_0" )
                                       .withAccessType( LUN.AccessType.RW )
                                       .build( ) )
                      .withInitiator( new Initiator( "82:*:00:*" ) )
                      .withInitiator( new Initiator( "83:*:00:*" ) )
                      .build( );

        target.setId( 0L );
        target.setName( UUID.randomUUID().toString() );

        Assert.assertEquals( target.getId( )
                                   .longValue( ), 0L );
        Assert.assertNotNull( target.getName( ) );

        Assert.assertTrue( target.getIncomingUsers().size() == 2 );
        for( final Credentials user : target.getIncomingUsers() )
        {
            Assert.assertNotNull( user.getUsername() );
            Assert.assertNotNull( user.getPassword( ) );
        }

        Assert.assertTrue( target.getOutgoingUsers().size() == 1 );
        for( final Credentials user : target.getOutgoingUsers( ) )
        {
            Assert.assertNotNull( user.getUsername() );
            Assert.assertNotNull( user.getPassword() );
        }

        Assert.assertTrue( target.getLuns( ).size( ) == 1 );
        for( final LUN lun : target.getLuns() )
        {
            Assert.assertNotNull( lun.getLunName() );
            Assert.assertNotNull( lun.getAccessType() );
            Assert.assertNotNull( lun.getIQN() );
        }

        Assert.assertTrue( target.getInitiators().size() == 2 );
        for( final Initiator initiator : target.getInitiators() )
        {
            Assert.assertNotNull( initiator.getWWNMask() );
        }

        System.out.println( gson().toJson( target ) );
    }
}
