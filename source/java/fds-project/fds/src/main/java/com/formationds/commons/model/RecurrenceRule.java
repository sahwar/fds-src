/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model;

import com.formationds.commons.model.abs.ModelBase;
import com.formationds.commons.model.type.iCalFrequency;
import com.formationds.commons.model.type.iCalKeys;
import com.formationds.commons.util.Numbers;
import com.formationds.commons.util.WeekDays;
import com.google.common.base.Preconditions;
import com.google.gson.annotations.SerializedName;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.text.ParseException;
import java.util.Date;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.StringTokenizer;

/**
 * @author ptinius
 */
@SuppressWarnings( "UnusedDeclaration" )
public class RecurrenceRule
    extends ModelBase {
    private static final long serialVersionUID = -6056637443021228929L;

    private static final Logger logger =
        LogManager.getLogger( RecurrenceRule.class );

    private static final String MISSING_TOKEN =
        "Missing expected token, last token: '%s'";

    private static final String RRULE_STARTSWITH = "RRULE:";

    @SerializedName( "FREQ" )
    private String frequency;
    @SerializedName( "UNTIL" )
    private Date until;
    @SerializedName( "COUNT" )
    private Integer count = null;
    @SerializedName( "INTERVAL" )
    private Integer interval = null;

    @SerializedName( "BYSECOND" )
    private Numbers<Integer> seconds = null;
    @SerializedName( "BYMINUTE" )
    private Numbers<Integer> minutes = null;
    @SerializedName( "BYHOUR" )
    private Numbers<Integer> hours = null;
    @SerializedName( "BYMONTHDAY" )
    private Numbers<Integer> monthDays = null;
    @SerializedName( "BYYEAR" )
    private Numbers<Integer> yearDays = null;
    @SerializedName( "BYWEEKNO" )
    private Numbers<Integer> weekNo = null;
    @SerializedName( "BYMONTH" )
    private Numbers<Integer> months = null;
    @SerializedName( "BYSETPOS" )
    private Numbers<Integer> position = null;

    @SerializedName( "WKST" )
    private String weekStartDay;

    @SerializedName( "BYDAY" )
    private WeekDays<String> days = null;

    /**
     * default constructor
     */
    public RecurrenceRule() {
    }

    /**
     * @param frequency the {@link String} representing the frequency
     * @param until     the {@link java.util.Date} defines a date-time value
     *                  which bounds the recurrence rule in an inclusive
     *                  manner
     */
    public RecurrenceRule( final String frequency, final Date until ) {
        setFrequency( frequency );
        setUntil( until );
    }

    /**
     * @param frequency the {@link String} representing the frequency
     * @param count     the {@link Integer} defines the number of occurrences
     *                  at which to range-bound the recurrence
     */
    public RecurrenceRule( final String frequency, final Integer count ) {
        setFrequency( frequency );
        setCount( count );
    }

    /**
     * @return Returns {@link String} representing the iCAL Frequency
     */
    public String getFrequency() {
        return frequency;
    }

    public void setFrequency( final String frequency ) {
        this.frequency = frequency;
        validate();
    }

    public Integer getInterval() {
        return interval;
    }

    public void setInterval( final Integer interval ) {
        this.interval = interval;
    }

    public Integer getCount() {
        return count;
    }

    public void setCount( final Integer count ) {
        this.count = count;
    }

    public Date getUntil() {
        return until;
    }

    public void setUntil( final Date until ) {
        this.until = until;
    }

    public String getWeekStartDay() {
        return weekStartDay;
    }

    public void setWeekStartDay( final String weekStartDay ) {
        this.weekStartDay = weekStartDay;
    }

    public Numbers<Integer> getPosition() {
        return position;
    }

    public void setPosition( final Numbers<Integer> position ) {
        this.position = position;
    }

    public Numbers<Integer> getMonths() {
        return months;
    }

    public void setMonths( final Numbers<Integer> months ) {
        this.months = months;
    }

    public Numbers<Integer> getWeekNo() {
        return weekNo;
    }

    public void setWeekNo( final Numbers<Integer> weekNo ) {
        this.weekNo = weekNo;
    }

    public Numbers<Integer> getYearDays() {
        return yearDays;
    }

    public void setYearDays( final Numbers<Integer> yearDays ) {
        this.yearDays = yearDays;
    }

    public Numbers<Integer> getMonthDays() {
        return monthDays;
    }

    public void setMonthDays( final Numbers<Integer> monthDays ) {
        this.monthDays = monthDays;
    }

    public WeekDays<String> getDays() {
        return days;
    }

    public void setDays( final WeekDays<String> days ) {
        this.days = days;
    }

    public Numbers<Integer> getMinutes() {
        return minutes;
    }

    public void setMinutes( final Numbers<Integer> minutes ) {
        this.minutes = minutes;
    }

    public Numbers<Integer> getHours() {
        return hours;
    }

    public void setHours( final Numbers<Integer> hours ) {
        this.hours = hours;
    }

    public Numbers<Integer> getSeconds() {
        return seconds;
    }

    public void setSeconds( final Numbers<Integer> seconds ) {
        this.seconds = seconds;
    }

    /**
     * @param rrule the {@link String} representing the iCal Recurrence Rule
     *
     * @return Returns the {@link RecurrenceRule}
     *
     * @throws java.text.ParseException any parsing error
     */
    public RecurrenceRule parser( final String rrule )
        throws ParseException {


        Preconditions.checkNotNull( rrule,
                                    "argument \'rrule\' must not be null." );

        final RecurrenceRule RRule = new RecurrenceRule();
        @SuppressWarnings( "UseOfStringTokenizer" )
        final StringTokenizer t = new StringTokenizer( rrule, ";=" );

        while( t.hasMoreTokens() ) {
            String token = t.nextToken();
            if( token.startsWith( RRULE_STARTSWITH ) ) {
                token = token.replace( RRULE_STARTSWITH, "" )
                             .trim();
            }

            final iCalKeys key = iCalKeys.valueOf( token );
            String next = nextToken(t, token);
            key.setRecurrenceRule(RRule, next);
        }

        return RRule;
    }

    private String nextToken( StringTokenizer t, String lastToken ) {
        try {
            return t.nextToken();
        } catch( NoSuchElementException e ) {
            throw new IllegalArgumentException( String.format( MISSING_TOKEN,
                                                               lastToken ) );
        }
    }

    private void validate() {
        iCalFrequency.valueOf( getFrequency() );
    }

    /**
     * @return Returns {@link String} representing the recurrence rule
     */
    @Override
    public final String toString() {
        final StringBuilder b = new StringBuilder();

        int i = 0;
        for (iCalKeys k : iCalKeys.values() ) {

            Optional<String> kv = k.formatKV(this);
            if (kv.isPresent()) {
                if (i++ > 0) b.append(';');
                b.append(kv.get());
            }
        }

        return b.toString();
    }
}
