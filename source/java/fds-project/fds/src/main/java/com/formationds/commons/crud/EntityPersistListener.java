/**
 * Copyright (c) 2014 Formation Data Systems.  All rights reserved.
 */

package com.formationds.commons.crud;

import java.util.Collection;

/**
 * Entity prePersist and postPersist listener callbacks.
 * <p/>
 * Each interface may throw a RuntimeException on error.
 *
 * @param <T>
 */
// TODO: use standard JPA @PrePersist etc entity annotations?
public interface EntityPersistListener<T> {

    /**
     * Notification that the entity is about to be saved.
     * <p/>
     * Implementations should avoid blocking operations.
     *
     * @param entity
     *
     * @throws RuntimeException if an error occurs.
     */
    default <R extends T> void prePersist( R entity ) {}

    /**
     * Notification that the entities are about to be saved.
     * <p/>
     * Implementations should avoid blocking operations.
     *
     * @param entities
     *
     * @throws RuntimeException if an error occurs.
     */
    default <R extends T> void prePersist( Collection<R> entities ) {
        entities.forEach( ( e ) -> prePersist( e ) );
    }

    /**
     * Notification that the entity was just persisted.
     *
     * @param entity
     *
     * @throws RuntimeException if an error occurs.
     */
    default <R extends T> void postPersist( R entity ) {}

    /**
     * Notification that the entity was just persisted.
     *
     * @param entities
     *
     * @throws RuntimeException if an error occurs.
     */
    default <R extends T> void postPersist( Collection<R> entities ) {
        entities.forEach( ( e ) -> postPersist( e ) );
    }
}
