cmake_minimum_required(VERSION 3.10.2 FATAL_ERROR)

project(mbzirc_ros)

find_package(ament_cmake REQUIRED)

find_package(geometry_msgs REQUIRED)
find_package(ignition-math6 REQUIRED)
set(IGN_MATH_VER ${ignition-math6_VERSION_MAJOR})
find_package(ignition-msgs8 REQUIRED)
find_package(ignition-transport11 REQUIRED)
set(IGN_TRANSPORT_VER ${ignition-transport11_VERSION_MAJOR})
find_package(ignition-common4 REQUIRED)
set(IGN_COMMON_VER ${ignition-common4_VERSION_MAJOR})
find_package(mavros_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ros_ign_interfaces REQUIRED)
find_package(rosgraph_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(cv_bridge REQUIRED)

add_executable(fixed_wing_bridge src/fixed_wing_bridge.cc)
ament_target_dependencies(fixed_wing_bridge
  PUBLIC
  rclcpp
  mavros_msgs
)
target_link_libraries(fixed_wing_bridge PUBLIC
  ignition-msgs8
  ignition-transport11
)

add_executable(optical_frame_publisher src/optical_frame_publisher.cc)
ament_target_dependencies(optical_frame_publisher
  geometry_msgs
  rclcpp
  tf2
  tf2_ros
  sensor_msgs
)

add_executable(pose_tf_broadcaster src/pose_tf_broadcaster.cc)
ament_target_dependencies(pose_tf_broadcaster
  geometry_msgs
  rclcpp
  tf2
  tf2_ros
  tf2_msgs
)

add_executable(video_target_relay src/video_target_relay.cc)
ament_target_dependencies(video_target_relay
  cv_bridge
  rclcpp
  ros_ign_interfaces
  sensor_msgs
  ignition-transport${IGN_TRANSPORT_VER}
  ignition-common${IGN_COMMON_VER}
)

add_executable(video_stream_publisher examples/video_stream_publisher.cc)
ament_target_dependencies(video_stream_publisher
  rclcpp
  sensor_msgs
)

add_executable(usv_bridge src/usv_bridge.cc)
ament_target_dependencies(usv_bridge
  PUBLIC
  rclcpp
  std_msgs
)
target_link_libraries(usv_bridge PUBLIC
  ignition-math${IGN_MATH_VER}::ignition-math${IGN_MATH_VER}
)

# Resources
install(TARGETS
  fixed_wing_bridge
  optical_frame_publisher
  pose_tf_broadcaster
  usv_bridge
  video_target_relay
  video_stream_publisher
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME})


if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(launch_testing_ament_cmake REQUIRED)
  ament_find_gtest()

  add_executable(test_ros_api test/ros_api/test_ros_api.cc)
  ament_target_dependencies(test_ros_api
    geometry_msgs
    rclcpp
    rosgraph_msgs
    ros_ign_interfaces
    sensor_msgs
    std_msgs
    tf2_msgs
  )
  target_link_libraries(test_ros_api
    ${GTEST_LIBRARIES}
  )
  target_include_directories(test_ros_api PUBLIC ${GTEST_INCLUDE_DIRS})

  install(TARGETS
    test_ros_api
    DESTINATION lib/${PROJECT_NAME}
  )

  add_launch_test(test/launch/test_ros_api.launch.py
    TIMEOUT 300
  )
endif()

ament_package()
