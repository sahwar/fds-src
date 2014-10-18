/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model.capacity;

import com.formationds.commons.model.abs.Calculated;

/**
 * @author ptinius
 */
public class CapacityFull
  extends Calculated {
  private static final long serialVersionUID = 4608718129718022030L;

  private Integer percentage;

  /**
   * default constructor
   */
  public CapacityFull() {
  }

  /**
   * @param percentage the {@link java.lang.Integer} representing the
   *                   percentage full.
   */
  public CapacityFull( final Integer percentage ) {
    this.percentage = percentage;
  }

  /**
   * @return Returns the {@link Integer} representing the percentage full
   */
  public Integer getPercentage() {
    return percentage;
  }

  /**
   * @param percentage the {@link Integer} representing the percentage full
   */
  public void setPercentage( final Integer percentage ) {
    this.percentage = percentage;
  }
}