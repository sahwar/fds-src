/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model;

import com.formationds.commons.model.abs.ModelBase;
import com.formationds.commons.model.type.ManagerType;
import com.formationds.commons.model.type.ServiceStatus;

/**
 * @author ptinius
 */
public abstract class Service
  extends ModelBase {
  private static final long serialVersionUID = -1577170593630479004L;

  private Long uuid;

  private String name;
  private String autoName;
  private Integer port;
  private ServiceStatus status;
  private final ManagerType type;

  protected Service( final ManagerType aType ) {
	  this.type = aType;
  }

  public String getName( ) {
    return name;
  }

  public void setName( final String name ) {
    this.name = name;
  }
  public String getAutoName( ) {
    return autoName;
  }
  
  public void setAutoName( String aName ){
	  this.autoName = aName;
  }

  public Long getUuid( ) {
    return uuid;
  }
  
  public void setUuid( Long aUuid ){
	  this.uuid = aUuid;
  }

  public ManagerType getType( ) {
    return type;
  }

  public ServiceStatus getStatus( ) {
    return status;
  }
  
  public void setStatus( ServiceStatus aStatus ){
	  this.status = aStatus;
  }

  public Integer getPort( ) {
    return port;
  }
  
  public void setPort( Integer aPort ){
	  this.port = aPort;
  }
}
