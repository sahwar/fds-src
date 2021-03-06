/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.crud;

import com.formationds.om.repository.query.QueryCriteria;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Properties;

/**
 * @author ptinius
 */
public interface CRUDRepository<T, PrimaryKey extends Serializable> {

    /**
     * @return the class
     */
    Class<T> getEntityClass();

    /**
     * Get the entity name.
     *
     * <p/>
     * The default implementation here returns the fully-qualified name of the
     * entity class, which is consistent with a JPA implementation default naming
     * strategy (i.e., no Table annotation or override in the @Entity annotation)
     * For a JPA implementation, which is assumed by the default implemnentation,
     * this is the name of the entity class.
     *
     * @return the underlying entity name, for example the mapped table name in jpa land.
     */
    default String getEntityName() { return getEntityClass().getName(); }

    // TODO: add list of column mappings?
    //LinkedHashMap<String, ColumnDefinition> getColumnMap();

    /**
     * Add a Entity persist listener for pre/post persistence callbacks.
     * <p/>
     * These are passed-through to the underlying data store.
     *
     * @param l the listener
     */
    public void addEntityPersistListener( EntityPersistListener<T> l );

    /**
     * Remove the entity persist listener.
     *
     * @param l the listener to remove
     */
    public void removeEntityPersistListener( EntityPersistListener<T> l );

    /**
     * @return the number of entities
     */
    long countAll();

    /**
     * @param entity the search criteria
     *
     * @return the number of entities
     */
    long countAllBy( final T entity );

    /**
     * @param entity the entity to save
     *
     * @return Returns the saved entity
     */
    <R extends T> R save( final R entity );

    /**
     * Persist the specified entities.
     * <p/>
     * This implementations converts the array of entities to a collection
     * and calls {@link #save(Collection)}
     *
     * @param entities the array of entities to persist
     *
     * @return the persisted events.
     *
     * @throws RuntimeException if the save for any entity fails
     */
    default <R extends T> List<R> save(final R... entities ) {
        return (entities != null ? save( Arrays.asList( entities )) : new ArrayList<>(0));
    }

    /**
     * Persist the specified entities.
     * <p/>
     * This implementation iterates over the set of entities and persists them
     * individually.  Sub-classes that support a bulk-save mechanism should
     * override this method to improve performance.
     *
     * @param entities the array of entities to persist
     *
     * @return the persisted events.
     *
     * @throws RuntimeException if the save for any entity fails
     */
    default <R extends T> List<R> save(final Collection<R> entities) {
        List<R> persisted = new ArrayList<>( );
        if (entities == null) {
            return persisted;
        }

        for (R e : entities) {
            persisted.add( save( e ) );
        }
        return persisted;
    }

    /**
     * @param queryCriteria the search criteria
     *
     * @return Returns the search criteria results
     */
    List<? extends T> query( final QueryCriteria queryCriteria );

    // TODO: delete( QueryCriteria )
    // TODO: update( T entity );
    // TODO: update( T model, QueryCriteria)
    /**
     * @param entity the entity to delete
     */
    void delete( final T entity );

    /**
     * @param entities the entities to delete
     */
    default void delete(final T... entities ) {
        if (entities != null ) {
            delete( Arrays.asList( entities ) );
        }
    }

    /**
     * Delete the specified entities.
     *
     * @param entities the array of entities to delete
     *
     * @throws RuntimeException if the delete for any entity fails
     */
    default void delete(final Collection<T> entities) {
        if (entities != null) {
            for ( T e : entities ) {
                delete( e );
            }
        }
    }

    /**
     *
     * @param connectionProperties
     */
    default public void open(Properties connectionProperties) {}

    /**
     * close the repository
     */
    void close();
}
