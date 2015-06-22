angular.module( 'node-management' ).factory( '$node_service', ['$http_fds', '$interval', '$rootScope', function( $http_fds, $interval, $rootScope ){

    var service = {};

    var pollerId;
    service.nodes = [];
    service.detachedNodes = [];

    service.FDS_UP = 'UP';
    service.FDS_DOWN = 'DOWN';
    service.FDS_MIGRATION = 'MIGRATION';
    service.FDS_REMOVED = 'REMOVED';
    service.FDS_UNKNOWN = 'UNKNOWN';
    service.FDS_DISCOVERED = 'DISCOVERED';
    service.FDS_RUNNING = 'RUNNING';
    service.FDS_NOT_RUNNING = 'NOT_RUNNING';
    service.FDS_ERROR = 'ERROR';
    service.FDS_UNREACHABLE = 'UNREACHABLE';
    service.FDS_LIMITED = 'LIMITED';
    service.FDS_DEGRADED = 'DEGRADED';
    service.FDS_UNEXPECTED_EXIT = 'UNEXPECTED_EXIT';
    
    service.FDS_ACTIVE = 'ACTIVE';
    service.FDS_INACTIVE = 'INACTIVE';
    service.FDS_ERROR = 'ERROR';

    service.getOverallStatus = function( node ){

        if ( node.am === service.DOWN ||
            node.om === service.DOWN ||
            node.sm === service.DOWN ||
            node.hw === service.DOWN ||
            node.dm === service.DOWN ){
            return service.DOWN;
        }

        if ( node.am === service.UNKNOWN ||
            node.om === service.UNKNOWN ||
            node.sm === service.UNKNOWN ||
            node.hw === service.UNKNOWN ||
            node.dm === service.UNKNOWN ){
            return service.FDS_NODE_ATTENTION;
        }

        return service.UP;
    };

    service.addNodes = function( nodes ){

        nodes.forEach( function( node ){
            
            var am = {
                name: 'AM',
                type: 'AM',
                status: {
                    state: 'RUNNING'
                }
            };
            
            var sm = {
                name: 'SM',
                type: 'SM',
                status: {
                    state: 'RUNNING'
                }
            };
            
            var dm = {
                name: 'DM',
                type: 'DM',
                status: {
                    state: 'RUNNING'
                }
            };
            
            node.serviceMap.AM = [am];
            node.serviceMap.DM = [dm];
            node.serviceMap.SM = [sm];
            
            $http_fds.post( webPrefix + '/nodes/' + node.id, node )
                .then( getNodes );
//            console.log( '/api/config/services/' + node.uuid + ' BODY: {am: ' + node.am + ', sm:' + node.sm + ', dm: ' + node.dm + '}' );
        });
    };
    
    service.removeNode = function( node, callback ){
        
        // right now we stop all services when we deactivate a node
        $http_fds.delete( webPrefix + '/nodes/' + node.id, node )
            .then( function(){
                if ( angular.isFunction( callback ) ){
                    callback();
                }
            })
            .then( getNodes );
    };

    var getNodes = function(){
        
        if ( pollerId === -1 ){
            poll();
        }
        
        return $http_fds.get( webPrefix + '/nodes',
            function( data ){

                service.nodes = [];
                service.detachedNodes = [];
                
                // separate out the discovered nodes from the active ones
                for( var i = 0; angular.isDefined( data ) && i < data.length; i++ ){
                    
                    var node = data[i];
                    
                    if ( node.state === service.DISCOVERED ){
                        service.detachedNodes.push( node );
                    }
                    else {
                        service.nodes.push( node );
                    }
                }
            });
    };
    
    service.refresh = function(){
        getNodes();
    };

    var poll = function(){
        pollerId = $interval( getNodes, 30000 );
    }();    
    
    $rootScope.$on( 'fds::authentication_logout', function(){
        console.log( 'node poller cancelled' );
        $interval.cancel( pollerId );
    });

    $rootScope.$on( 'fds::authentication_success', function(){
        service.refresh();
    });

    return service;

}]);
