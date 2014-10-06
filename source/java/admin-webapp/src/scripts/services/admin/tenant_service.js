angular.module( 'tenant-management' ).factory( '$tenant_service', ['$http', function( $http ){

    var service = {};

    service.getTenants = function( callback, failure ){

        return $http.get( '/api/system/tenants' )
            .success( callback ).error( failure );
    };

    service.createTenant = function( tenant, callback, failure ){

        return $http.post( '/api/system/tenants/' + tenant.name )
            .success( callback ).error( failure );
    };

    service.attachUser = function( tenant, userId, callback, failure ){

        return $http.put( '/api/system/tenants/' + tenant.id + '/' + userId )
            .success( callback ).error( failure );
    };

    return service;

}]);