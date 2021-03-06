package com.formationds.om.repository.query;

import java.util.Objects;

public class OrderBy {

	private String fieldName;
	private boolean ascending;

	public OrderBy(){

	}

	public OrderBy( String aFieldName, boolean isAscending ){

		this.fieldName = aFieldName;
		this.ascending = isAscending;
	}

	public String getFieldName(){
		return fieldName;
	}

	public void setFieldName( String aFieldName ){
		this.fieldName = aFieldName;
	}

	public boolean isAscending(){
		return ascending;
	}

	public void setAscending( boolean isAscending ){
		this.ascending = isAscending;
	}

	@Override
    public int hashCode()
    {
        return Objects.hash( ascending, fieldName );
    }

    @Override
    public boolean equals( Object obj )
    {
        if ( this == obj )
        {
            return true;
        }
        if ( obj == null )
        {
            return false;
        }
        if ( !( obj instanceof OrderBy ) )
        {
            return false;
        }
        OrderBy other = (OrderBy) obj;

        return Objects.equals( this.ascending, other.ascending ) &&
               Objects.equals( this.fieldName, other.fieldName );
    }
}
