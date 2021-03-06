/**
 * Copyright (c) 2015 Formation Data Systems.  All rights reserved.
 */

package com.formationds.client.v08.model;

/**
 * A tenant is a product entity that manages a set of Tenant Users and a collection of
 * application volumes.  A tenant exists within the Global Domain and is associated with zero or more
 *
 */
public class Tenant extends AbstractResource<Long> {

    public static final Tenant SYSTEM = new Tenant( 1L, "system" );

    private Tenant(){
    	super( 0L, "None" );
    }
    
    public Tenant( Long id, String name ) {
        super( id, name );
    }

    @Override
    public String toString() {
        final StringBuilder sb = new StringBuilder( "Tenant{" );
        sb.append( super.toString() ).append( '}' );
        return sb.toString();
    }
}
