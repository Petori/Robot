cmake_minimum_required(VERSION 2.8.3)
project(kinova_arm_moveit_demo)

add_compile_options(-std=c++11)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED
             COMPONENTS
             moveit_core
             moveit_ros_planning
             moveit_ros_planning_interface
             pluginlib
             cmake_modules
             geometric_shapes
             robotiq_2f_gripper_control
)

find_package(Boost REQUIRED system filesystem date_time thread)

##################
## Add messages ##
##################

add_message_files(
	FILES
	targetState.msg
	targetsVector.msg
        toolposeChange.msg
)

generate_messages(
   DEPENDENCIES
   actionlib_msgs
   std_msgs  # Or other packages containing msgs
)


catkin_package(
  INCLUDE_DIRS include
  CATKIN_DEPENDS
    moveit_core
    moveit_ros_planning_interface
    interactive_markers
)
find_package(Eigen3 REQUIRED)

find_package(kinova_driver REQUIRED)

###########
## Build ##
###########
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

include_directories(SYSTEM ${Boost_INCLUDE_DIR} ${EIGEN_INCLUDE_DIRS})
include_directories(${catkin_INCLUDE_DIRS})
link_directories(${catkin_LIBRARY_DIRS})

include_directories(${kinova_driver_INCLUDE_DIRS})

add_executable(motion_plan src/motion_plan.cpp)
target_link_libraries(motion_plan ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS motion_plan DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(work_scene src/work_scene.cpp)
target_link_libraries(work_scene ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS work_scene DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(pick_place src/pick_place.cpp include/pick_place.h)
target_link_libraries(pick_place ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS pick_place DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(test_accuracy src/test_accuracy.cpp include/test_accuracy.h)
target_link_libraries(test_accuracy ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS test_accuracy DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

## added by zp in 2019/4/15
add_executable(our_pick_place src/our_pick_place.cpp)
target_link_libraries(our_pick_place ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS our_pick_place DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(KN_real src/KN_real.cpp)
target_link_libraries(KN_real ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS KN_real DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(UR_real src/UR_real.cpp)
target_link_libraries(UR_real ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS UR_real DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(KN_sim_new src/KN_sim_new.cpp)
target_link_libraries(KN_sim_new ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS KN_sim_new DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(forPID src/forPID.cpp)
target_link_libraries(forPID ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS forPID DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

add_executable(UR_real_new src/UR_real_new.cpp)
target_link_libraries(UR_real_new ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${kinova_driver_LIBRARIES})
install(TARGETS UR_real_new DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
## end

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
