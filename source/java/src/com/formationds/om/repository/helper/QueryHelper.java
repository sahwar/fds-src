/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.repository.helper;

import com.formationds.commons.calculation.Calculation;
import com.formationds.commons.model.Datapoint;
import com.formationds.commons.model.Series;
import com.formationds.commons.model.Statistics;
import com.formationds.commons.model.abs.Calculated;
import com.formationds.commons.model.abs.Metadata;
import com.formationds.commons.model.builder.DatapointBuilder;
import com.formationds.commons.model.builder.VolumeBuilder;
import com.formationds.commons.model.calculated.capacity.CapacityConsumed;
import com.formationds.commons.model.calculated.capacity.CapacityDeDupRatio;
import com.formationds.commons.model.calculated.capacity.CapacityFull;
import com.formationds.commons.model.calculated.capacity.CapacityToFull;
import com.formationds.commons.model.calculated.firebreak.FirebreaksLast24Hours;
import com.formationds.commons.model.calculated.performance.IOPsConsumed;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.commons.model.type.Metrics;
import com.formationds.commons.util.DateTimeUtil;
import com.formationds.om.repository.MetricsRepository;
import com.formationds.om.repository.SingletonMetricsRepository;
import com.formationds.om.repository.query.QueryCriteria;
import com.formationds.om.repository.query.builder.VolumeCriteriaQueryBuilder;
import com.formationds.util.SizeUnit;
import org.apache.thrift.TException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * @author ptinius
 */
public class QueryHelper {
    private static final Logger logger =
        LoggerFactory.getLogger( QueryHelper.class );

    private final MetricsRepository repo;

    public static List<Metrics> FIREBREAKS = new ArrayList<>();
    static {
        FIREBREAKS.add( Metrics.STC_SIGMA );
        FIREBREAKS.add( Metrics.LTC_SIGMA );
        FIREBREAKS.add( Metrics.STP_SIGMA );
        FIREBREAKS.add( Metrics.LTP_SIGMA );
    }

    public static List<Metrics> PERFORMANCE = new ArrayList<>();
    static {
        PERFORMANCE.add( Metrics.STP_WMA );
    }

    public static final List<Metrics> CAPACITY = new ArrayList<>( );
    static {
        CAPACITY.add( Metrics.PBYTES );
        CAPACITY.add( Metrics.LBYTES );
    }

    /**
     * default constructor
     */
    public QueryHelper() {
        this.repo =
            SingletonMetricsRepository.instance()
                                      .getMetricsRepository();
    }

    /**
     * @param datapoints the {@link List} of {@link VolumeDatapoint}
     *
     * @return Return {@link Map} representing volumes as the keys and value
     * representing a {@link List} of {@link VolumeDatapoint}
     */
    protected static Map<String, List<VolumeDatapoint>> byVolumeNameTimestamp(
        final List<VolumeDatapoint> datapoints ) {
        final Map<String, List<VolumeDatapoint>> mapped = new HashMap<>();

        final Comparator<VolumeDatapoint> VolumeDatapointComparator =
            Comparator.comparing( VolumeDatapoint::getVolumeName )
                      .thenComparing( VolumeDatapoint::getTimestamp );

        datapoints.stream()
                  .sorted( VolumeDatapointComparator )
                  .forEach( ( v ) -> {
                      if( !mapped.containsKey( v.getVolumeName() ) ) {
                          mapped.put( v.getVolumeName(), new ArrayList<>() );
                      }

                      mapped.get( v.getVolumeName() )
                            .add( v );
                  } );

        return mapped;
    }

    /**
     * @param query the {@link QueryCriteria} representing the query
     *
     * @return Returns the {@link Statistics} representing the result of {@code
     * query}
     */
    public Statistics execute( final QueryCriteria query )
        throws TException {
        final Statistics stats = new Statistics();
        if( query != null ) {
            final List<Series> series = new ArrayList<>();
            final List<Calculated> calculatedList = new ArrayList<>();

            final List<VolumeDatapoint> queryResults =
                new VolumeCriteriaQueryBuilder( repo.entity() ).searchFor( query )
                                                               .resultsList();
            final Map<String, List<VolumeDatapoint>> originated =
                byVolumeNameTimestamp( queryResults );

            if( isFirebreakQuery( query.getSeriesType() ) ) {

                series.addAll( new FirebreakHelper().processFirebreak( queryResults ) );

                calculatedList.add( new FirebreaksLast24Hours( last24Hours( series ) ) );

            } else if( isPerformanceQuery( query.getSeriesType() ) ) {

                series.addAll(
                    new SeriesHelper().getPerformanceSeries( queryResults,
                                                             query ) );
                calculatedList.add( new IOPsConsumed( 0.0 ) );

            } else if( isCapacityQuery( query.getSeriesType() ) ) {

                series.addAll(
                    new SeriesHelper().getCapacitySeries( queryResults,
                                                          query ) );

                calculatedList.add( deDupRatio() );

                final CapacityConsumed consumed = bytesConsumed();
                calculatedList.add( consumed );

                // TODO finish implementing -- once the platform has total system capacity
                final Double systemCapacity = Long.valueOf( SizeUnit.TB.totalBytes( 1 ) )
                                                  .doubleValue();
                calculatedList.add( percentageFull( consumed, systemCapacity ) );

                // TODO finish implementing  -- once Nate provides a library
                calculatedList.add( toFull() );

            } else { // individual stats
                query.getSeriesType()
                     .stream()
                     .forEach( ( m ) -> series.addAll( otherQueries( originated,
                                                                     m ) ) );
            }

            if( !series.isEmpty() ) {
                stats.setSeries( series );
            }

            if( !calculatedList.isEmpty() ) {
                stats.setCalculated( calculatedList );
            }
        }

        return stats;
    }

    /**
     * @param metrics the [@link List} of {@link Metrics}
     *
     * @return Returns {@code true} if all {@link Metrics} within the
     *         {@link List} are of performance type. Otherwise {@code false}
     */
    protected boolean isPerformanceQuery( final List<Metrics> metrics ) {
        for( final Metrics m : metrics ) {
            if( !PERFORMANCE.contains( m ) ) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param metrics the [@link List} of {@link Metrics}
     *
     * @return Returns {@code true} if all {@link Metrics} within the
     *         {@link List} are of capacity type. Otherwise {@code false}
     */
    protected boolean isCapacityQuery( final List<Metrics> metrics ) {
        for( final Metrics m : metrics ) {
            if( !CAPACITY.contains( m ) ) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param metrics the [@link List} of {@link Metrics}
     *
     * @return Returns {@code true} if all {@link Metrics} within the
     *         {@link List} are of firebreak type. Otherwise {@code false}
     */
    protected boolean isFirebreakQuery( final List<Metrics> metrics ) {
        for( final Metrics m : metrics ) {
            if( !FIREBREAKS.contains( m ) ) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param organized the {@link Map} of volume containing a {@link List}
     *                  of {@link VolumeDatapoint}
     * @param metrics   the {@link Metrics} representing the none firebreak
     *                  metric
     *
     * @return Returns q {@link List} of {@link Series}
     */
    protected List<Series> otherQueries(
        final Map<String, List<VolumeDatapoint>> organized,
        final Metrics metrics ) {
        final List<Series> series = new ArrayList<>();

        organized.forEach( ( key, volumeDatapoints ) -> {
            final Series s = new Series();
            s.setType( metrics.name() );
            volumeDatapoints.stream()
                            .distinct()
                            .filter( ( p ) -> metrics.key()
                                                     .equalsIgnoreCase( p.getKey() ) )
                            .forEach( ( p ) -> {
                                final Datapoint dp =
                                    new DatapointBuilder().withX( p.getTimestamp() )
                                                          .withY( p.getValue()
                                                                   .longValue() )
                                                          .build();
                                s.setDatapoint( dp );
                                s.setContext(
                                    new VolumeBuilder().withName( p.getVolumeName() )
                                                       .withId( String.valueOf( p.getVolumeId() ) )
                                                       .build() );
                            } );
            series.add( s );
        } );

        return series;
    }

    /**
     * @return Returns a {@link List} of {@link Metadata}
     */
    protected List<Metadata> metadata() {
        return new ArrayList<>();
    }

    /**
     * @return Returns {@link CapacityDeDupRatio}
     */
    protected CapacityDeDupRatio deDupRatio() {
        final Double lbytes =
            SingletonMetricsRepository.instance()
                                      .getMetricsRepository()
                                      .sumLogicalBytes();

        final Double pbytes =
            SingletonMetricsRepository.instance()
                                      .getMetricsRepository()
                                      .sumPhysicalBytes();

        return new CapacityDeDupRatio( Calculation.ratio( lbytes, pbytes ) );
    }

    /**
     * @return Returns {@link CapacityConsumed}
     */
    protected CapacityConsumed bytesConsumed() {
        return new CapacityConsumed( SingletonMetricsRepository.instance()
                                                               .getMetricsRepository()
                                                               .sumLogicalBytes() );
    }

    /**
     * @param consumed       the {@link CapacityConsumed} representing bytes
     *                       used
     * @param systemCapacity the {@link Double} representing system capacity in
     *                       bytes
     *
     * @return Returns {@link CapacityFull}
     */
    protected CapacityFull percentageFull( final CapacityConsumed consumed,
                                           final Double systemCapacity ) {
        return new CapacityFull(
            ( int ) Calculation.percentage( consumed.getTotal(),
                                            systemCapacity ) );
    }

    /**
     * @return Returns {@link CapacityFull}
     */
    protected CapacityToFull toFull() {
        // TODO finish implementation
        return new CapacityToFull( 24 );
    }

    /**
     * @return Returns {@link Integer} representing the number of firebreaks
     *         that have occurred in the last 24 hours.
     */
    protected Integer last24Hours( final List<Series> series ) {
        final AtomicInteger count = new AtomicInteger( 0 );
        final long twentyFourHoursAgo =
            DateTimeUtil.toUnixEpoch( LocalDateTime.now()
                                                   .minusHours( 24 ) );

        series.stream().forEach( ( s ) -> s.getDatapoints()
         .stream()
         .forEach( ( dp ) -> {
             if( dp.getY() >= twentyFourHoursAgo ) {
                 count.getAndIncrement();
             }
         } ) );

        return count.intValue();
    }
}
