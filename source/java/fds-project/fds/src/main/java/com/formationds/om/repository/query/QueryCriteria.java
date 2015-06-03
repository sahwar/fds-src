/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.repository.query;

import com.formationds.commons.crud.SearchCriteria;
import com.formationds.commons.model.DateRange;
import com.formationds.commons.model.Volume;
import com.formationds.commons.model.abs.ModelBase;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;

@SuppressWarnings( "UnusedDeclaration" )
public class QueryCriteria
    extends ModelBase
    implements SearchCriteria {

    private static final long serialVersionUID = 6792621380579634266L;

    private DateRange range;           // date range ; starting and ending
    private Integer points;            // number of points to provide in results
    private Long firstPoint;           // first point, i.e. row number

    /**
     * The projection or ordered list of columns to include in the results
     * <p/>
     * An empty list is assumed to mean "select *"
     */
    // TODO: we might at some point want this to be a typed object that can
    // optionally include additional common column projection information like "as xxx" clauses
    private List<String> columns = new ArrayList<>();

    //TODO: This is a hack!  When JSON comes into the server it has no way to know which Context
    // object type to turn the JSON into so it's always null due to Context having no public constructor.
    //
    // The real fix is to add a "TYPE" enum to the Context object and overwrite the "toObject" method
    // to that it can pass the correct class to GSON so we get a Volume or a ... whatever.
    //
    // For now the only time Context is used is with a Volume type so in favor of speed we are
    // changing the query to only take volumes
    private List<Volume>  contexts;    // the context
    private List<OrderBy> orderBys;    //  a list of orderby instructions assumed to be sorted 0 = most important 

    public QueryCriteria() {}

    public QueryCriteria( DateRange dateRange ) { this.range = dateRange; }

    /**
     * @return Returns the {@link com.formationds.commons.model.DateRange}
     */
    public DateRange getRange() {
        return range;
    }

    /**
     * @param range the {@link com.formationds.commons.model.DateRange}
     */
    public void setRange( final DateRange range ) {
        this.range = range;
    }

    /**
     * @param columns the columns to add to the select list
     */
    public QueryCriteria addColumns( Collection<String> columns ) {
        if ( columns == null )
            return this;

        this.columns.addAll( columns );

        return this;
    }

    /**
     * @param columns the columns to add to the select list
     */
    public QueryCriteria addColumns( String... columns ) {
        if (columns == null)
            return this;

        this.columns.addAll( Arrays.asList( columns ) );

        return this;
    }

    /**
     *
     * @return the list of columns.  An empty list is interpreted to mean "select *"
     */
    public List<String> getColumns() {
        return columns;
    }

    /**
     *
     * @return a string representing the column projection list based on the configured columms.  If the columns
     * list is empty, returns a "*".
     */
    public String getColumnString() {
        if ( columns.isEmpty() ) {
            return "*";
        }
        StringBuilder sb = new StringBuilder( );
        Iterator<String> iter = columns.iterator();
        while (iter.hasNext()) {
            sb.append( iter.next() );
            if (iter.hasNext())
                sb.append( "," );
        }
        return sb.toString();
    }

    /**
     * @return Returns the {@link java.util.List} of {@link com.formationds.commons.model.abs.Context}
     */
    public List<Volume> getContexts() {
        if ( contexts == null ) {
            this.contexts = new ArrayList<>();
        }
        return contexts;
    }

    /**
     * @param contexts the {@link java.util.List} of {@link com.formationds.commons.model.abs.Context}
     */
    public void setContexts( final List<Volume> contexts ) {
        this.contexts = contexts;
    }

    /**
     * @return Returns the {@link Integer} representing the maximum number of
     * entity to return
     */
    public Integer getPoints() {
        return points;
    }

    /**
     * @param points the {@link Integer} representing the maximum number of
     *               entity to return
     */
    public void setPoints( final Integer points ) {
        this.points = points;
    }

    /**
     * @return Returns the {@link Long} representing the first results
     */
    public Long getFirstPoint() {
        return firstPoint;
    }

    /**
     * @param firstPoint the {@link Long} representing the first results
     */
    public void setFirstPoint( final Long firstPoint ) {
        this.firstPoint = firstPoint;
    }
    
    /**
     * 
     * @return a list of {@link OrderBy} arguments for this search
     */
    public List<OrderBy> getOrderBy(){
    	if ( this.orderBys == null ){
    		this.orderBys = new ArrayList<>();
    	}

    	return this.orderBys;
    }
    
    /**
     * 
     * @param someOrders a list of ordering criteria
     */
    public void setOrderBy( List<OrderBy> someOrders ){
    	this.orderBys = someOrders;
    }
    
    /**
     * Convenience method to add a single orderby at a time
     * 
     * @param anOrderBy the order by clause to add
     */
    public void addOrderBy( OrderBy anOrderBy ){

    	if ( !getOrderBy().contains( anOrderBy ) ){
    		getOrderBy().add( anOrderBy );
    	}
    }
}
