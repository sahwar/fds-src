angular.module("form-directives").directive("triStateCheck",function(){return{restrict:"E",replace:!0,transclude:!1,templateUrl:"scripts/directives/widgets/tristatecheck/tristatecheck.html",scope:{checkState:"="},controller:function($scope){var UNCHECKED=!1,CHECKED=!0,PARTIAL="partial";$scope.uncheck=function(){$scope.checkState=UNCHECKED},$scope.check=function(){$scope.checkState=CHECKED},$scope.partial=function(){$scope.checkState=PARTIAL}}}});