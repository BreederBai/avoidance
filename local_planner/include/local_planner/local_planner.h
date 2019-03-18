#ifndef LOCAL_PLANNER_LOCAL_PLANNER_H
#define LOCAL_PLANNER_LOCAL_PLANNER_H

#include <sensor_msgs/image_encodings.h>
#include "avoidance_output.h"
#include "box.h"
#include "candidate_direction.h"
#include "cost_parameters.h"
#include "histogram.h"

#include <dynamic_reconfigure/server.h>
#include <local_planner/LocalPlannerNodeConfig.h>

#include <Eigen/Dense>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <tf/transform_listener.h>

#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud2.h>

#include <nav_msgs/GridCells.h>
#include <nav_msgs/Path.h>

#include <ros/time.h>
#include <deque>
#include <string>
#include <vector>

namespace avoidance {

class StarPlanner;
class TreeNode;

/**
* @brief struct to contain the parameters needed for the model based trajectory
*planning
* when MPC_AUTO_MODE is set to 1 (default) then all members are used for the
*jerk limited
* trajectory on the flight controller side
* when MPC_AUTO_MODE is set to 0, only up_accl, down_accl, xy_acc are used on
*the
* flight controller side
**/
struct ModelParameters {
  // clang-format off
  int mpc_auto_mode = 1; // Auto sub-mode - 0: default line tracking, 1 jerk-limited trajectory
  float jerk_min = 8.0f; // Velocity-based jerk limit
  float up_acc = 10.0f;   // Maximum vertical acceleration in velocity controlled modes upward
  float up_vel = 3.0f;   // Maximum vertical ascent velocity
  float down_acc = 10.0f; // Maximum vertical acceleration in velocity controlled modes down
  float down_vel = 1.0f; // Maximum vertical descent velocity
  float xy_acc = 5.0f;  // Maximum horizontal acceleration for auto mode and
                      // maximum deceleration for manual mode
  float xy_vel = 1.0f;   // Desired horizontal velocity in mission
  float takeoff_speed = 1.0f; // Takeoff climb rate
  float land_speed = 0.7f;   // Landing descend rate
  // limitations given by sensors
  float distance_sensor_max_height = 5.0f;
  float distance_sensor_max_vel = 5.0f;

  float min_takeoff_alt = 2.5f; // Minimum takeoff altitude
  // clang-format on
};

class LocalPlanner {
 private:
  bool adapt_cost_params_;
  bool reach_altitude_ = false;
  bool waypoint_outside_FOV_ = false;

  int e_FOV_max_, e_FOV_min_;
  size_t dist_incline_window_size_ = 50;
  int origin_;
  int tree_age_ = 0;
  int children_per_node_;
  int n_expanded_nodes_;

  float curr_yaw_fcu_frame_deg_, curr_yaw_histogram_frame_deg_;
  float curr_pitch_deg_;  // for pitch angles the histogram frame matches the
                          // fcu frame
  float velocity_around_obstacles_;
  float velocity_far_from_obstacles_;
  ros::Time integral_time_old_;
  float no_progress_slope_;
  float new_yaw_;
  float velocity_sigmoid_slope_ = 1.0;
  float min_realsense_dist_ = 0.2f;
  float smoothing_margin_degrees_ = 30.f;
  float max_point_age_s_ = 10;

  waypoint_choice waypoint_type_;
  ros::Time last_path_time_;
  ros::Time last_pointcloud_process_time_;

  std::vector<int> e_FOV_idx_;
  std::vector<int> z_FOV_idx_;
  std::deque<float> goal_dist_incline_;
  std::vector<float> cost_path_candidates_;
  std::vector<int> cost_idx_sorted_;
  std::vector<int> closed_set_;

  std::vector<TreeNode> tree_;
  std::unique_ptr<StarPlanner> star_planner_;
  costParameters cost_params_;

  pcl::PointCloud<pcl::PointXYZI> final_cloud_;

  Eigen::Vector3f position_ = Eigen::Vector3f::Zero();
  Eigen::Vector3f velocity_ = Eigen::Vector3f::Zero();
  Eigen::Vector3f goal_ = Eigen::Vector3f::Zero();
  Eigen::Vector3f position_old_ = Eigen::Vector3f::Zero();

  Histogram polar_histogram_ = Histogram(ALPHA_RES);
  Histogram to_fcu_histogram_ = Histogram(ALPHA_RES);
  Eigen::MatrixXf cost_matrix_;

  /**
  * @brief     calculates the cost function weights to fly around or over
  *            obstacles based on the progress towards the goal over time
  **/
  void evaluateProgressRate();
  /**
  * @brief     fills message to send histogram to the FCU
  **/
  void updateObstacleDistanceMsg(Histogram hist);
  /**
  * @brief     fills message to send empty histogram to the FCU
  **/
  void updateObstacleDistanceMsg();
  /**
  * @brief     creates a polar histogram representation of the pointcloud
  * @param[in] send_to_fcu, true if the histogram is sent to the FCU
  **/
  void create2DObstacleRepresentation(bool send_to_fcu);
  /**
  * @brief     generates an image represention of the polar histogram
  * @param     histogram, polar histogram representing obstacles
  * @returns   histogram image
  **/
  void generateHistogramImage(Histogram& histogram);

 public:
  float h_FOV_ = 59.0f;
  float v_FOV_ = 46.0f;
  Box histogram_box_;
  std::vector<uint8_t> histogram_image_data_;
  std::vector<uint8_t> cost_image_data_;
  bool use_vel_setpoints_;
  bool currently_armed_ = false;
  bool smooth_waypoints_ = true;
  bool send_obstacles_fcu_ = false;
  bool disable_rise_to_goal_altitude_ = false;

  double timeout_critical_;
  double timeout_termination_;
  double starting_height_ = 0.0;
  float speed_ = 1.0f;
  float ground_distance_ = 2.0;

  sensor_msgs::LaserScan distance_data_ = {};
  Eigen::Vector3f last_sent_waypoint_ = Eigen::Vector3f::Zero();

  // original_cloud_vector_ contains n complete clouds from the cameras
  std::vector<pcl::PointCloud<pcl::PointXYZ>> original_cloud_vector_;

  ModelParameters model_params_; // PX4 Firmware paramters

  LocalPlanner();
  ~LocalPlanner();

  /**
  * @brief     setter method for vehicle position
  * @param[in] pos, vehicle position message coming from the FCU
  * @param[in] q, vehicle orientation message coming from the FCU
  **/
  void setPose(const Eigen::Vector3f& pos, const Eigen::Quaternionf& q);
  /**
  * @brief     setter method for mission goal
  * @param[in] mgs, goal message coming from the FCU
  **/
  void setGoal(const Eigen::Vector3f& goal);
  /**
  * @brief     getter method for current goal
  * @returns   position of the goal
  **/
  Eigen::Vector3f getGoal() const;
  /**
  * @brief    setter method for mission goal
  **/
  void applyGoal();
  /**
  * @brief     sets parameters from ROS parameter server
  * @param     config, struct containing all the parameters
  * @param     level, bitmask to group together reconfigurable parameters
  **/
  void dynamicReconfigureSetParams(avoidance::LocalPlannerNodeConfig& config,
                                   uint32_t level);
  /**
  * @brief     getter method for current vehicle position and orientation
  * @returns   vehicle position and orientation
  **/
  Eigen::Vector3f getPosition() const;

  /**
  * @brief     getter method to visualize the pointcloud used for planning
  * @returns   reference to pointcloud
  **/
  const pcl::PointCloud<pcl::PointXYZI>& getPointcloud() const;

  /**
  * @brief     setter method for vehicle velocity
  * @param[in] vel, velocity message coming from the FCU
  **/
  void setCurrentVelocity(const Eigen::Vector3f& vel);

  /**
  * @brief     getter method to visualize the tree in rviz
  * @param[in] tree, the whole tree built during planning (vector of nodes)
  * @param[in] closed_set, velocity message coming from the FCU
  * @param[in] path_node_positions, velocity message coming from the FCU
  **/
  void getTree(std::vector<TreeNode>& tree, std::vector<int>& closed_set,
               std::vector<Eigen::Vector3f>& path_node_positions) const;
  /**
  * @brief     getter method for obstacle distance information
  * @param     obstacle_distance, obstacle distance message to fill
  **/
  void getObstacleDistanceData(sensor_msgs::LaserScan& obstacle_distance);

  /**
  * @brief     getter method of the local planner algorithm
  * @param[in] output of a local planner iteration
  **/
  avoidanceOutput getAvoidanceOutput() const;

  /**
  * @brief     determines the way the obstacle is avoided and the algorithm to
  *            use
  **/
  void determineStrategy();
  /**
  * @brief     starts a iteration of the local planner algorithm
  **/
  void runPlanner();
};
}

#endif  // LOCAL_PLANNER_H
