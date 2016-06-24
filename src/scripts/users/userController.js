angular.module( 'user-page' ).controller( 'userController', ['$scope', '$user_service', '$authorization', '$timeout', function( $scope, $user_service, $authorization, $timeout ){

    $scope.actionLabel = 'Actions';
//    $scope.actionItems = [{name: 'Edit User'},{name: 'Delete User'},{name: 'Disable User'}];
    $scope.users = [];
    $scope.creating = false;

    $scope.createNewUser = function(){
        $scope.userVars.selectedUser = undefined;
        $scope.userVars.next( 'createuser' );
    };
    
    $scope.actionSelected = function( action ){
    };
    
    $scope.editUser = function( user ){
        $scope.userVars.selectedUser = user;
        $scope.userVars.next( 'createuser' );
    };

    $scope.usersReturned = function( response ){
        $scope.users = response;
    };

    $scope.isAllowed = function( permission ){
        return $authorization.isAllowed( permission );
    };

    $scope.refresh = function(){
        $user_service.getUsers( $scope.usersReturned );
    };
    
    $scope.$watch( 'userVars.index', function( newVal ){
        if ( newVal === 0 ){
            $scope.userVars.selectedUser = undefined;
            $scope.refresh();
        }
    });
    
    $scope.refresh();
    
    

}]);
