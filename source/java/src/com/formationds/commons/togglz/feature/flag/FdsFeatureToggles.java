/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.togglz.feature.flag;

import com.formationds.commons.togglz.FdsFeatureManagerProvider;
import com.formationds.commons.togglz.feature.annotation.Firebreak;
import com.formationds.commons.togglz.feature.annotation.Snapshot;
import com.formationds.commons.togglz.feature.annotation.Statistics;
import org.togglz.core.Feature;
import org.togglz.core.annotation.Label;

/**
 * @author ptinius
 */
public enum FdsFeatureToggles
  implements Feature {
  @Label("Snapshot Feature")
  @Snapshot
  SNAPSHOT_ENDPOINT,

  @Label("Statistics Feature")
  @Statistics
  STATISTICS_ENDPOINT,

  @Label("Firebreak Feature")
  @Firebreak
  FIREBREAK;

  /**
   * @return Returns {@code true} if the feature associated with {@code this}
   * is enabled
   */
  public boolean isActive() {
    return FdsFeatureManagerProvider.getFeatureManager()
                                    .isActive( this );
  }
}
