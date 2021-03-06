/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model;

import com.formationds.commons.model.abs.ModelBase;

/**
 * @author ptinius
 */
public class Datapoint
  extends ModelBase {
  private static final long serialVersionUID = 7313793869030172350L;

  private Double x;         // x-axis data point
  private Double y;         // y-axis time point

  public Datapoint() {

  }

  /**
   * @param x
   * @param y
   */
  public Datapoint(Double x, Double y) {
	  this.x = x;
	  this.y = y;
  }

  /**
   * @return Returns the {@link Long} representing the x-axis
   */
  public Double getX() {
    return x;
  }

  /**
   * @param x the {@link Long} representing the x-axis
   */
  public void setX( final Double x ) {
    this.x = x;
  }

  /**
   * @return Returns the {@link Long} representing the y-axis
   */
  public Double getY() {
    return y;
  }

  /**
   * @param y the {@link Long} representing the y-axis
   */
  public void setY( final Double y ) {
    this.y = y;
  }

  /**
   * @return a concise string representation of the datapoint
   */
  public String toString() { return String.format("Datapoint (%f, %f)", x, y); }
}
