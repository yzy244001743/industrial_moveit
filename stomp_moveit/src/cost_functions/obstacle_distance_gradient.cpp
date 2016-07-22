/*
 * obstacle_distance_gradien.cpp
 *
 *  Created on: Jul 22, 2016
 *      Author: Jorge Nicho
 */

#include <stomp_moveit/cost_functions/obstacle_distance_gradient.h>
#include <ros/console.h>
#include <pluginlib/class_list_macros.h>
#include <moveit/robot_state/conversions.h>

PLUGINLIB_EXPORT_CLASS(stomp_moveit::cost_functions::ObstacleDistanceGradient,stomp_moveit::cost_functions::StompCostFunction)


namespace stomp_moveit
{
namespace cost_functions
{

ObstacleDistanceGradient::ObstacleDistanceGradient() :
    name_("ObstacleDistanceGradient"),
    robot_state_(),
    collision_robot_df_()
{

}

ObstacleDistanceGradient::~ObstacleDistanceGradient()
{

}

bool ObstacleDistanceGradient::initialize(moveit::core::RobotModelConstPtr robot_model_ptr,
                                          const std::string& group_name, XmlRpc::XmlRpcValue& config)
{
  robot_model_ptr_ = robot_model_ptr;
  group_name_ = group_name;
  return configure(config);
}

bool ObstacleDistanceGradient::configure(const XmlRpc::XmlRpcValue& config)
{

  try
  {
    // check parameter presence
    auto members = {"cost_weight" ,"voxel_size","max_distance"};
    for(auto& m : members)
    {
      if(!config.hasMember(m))
      {
        ROS_ERROR("%s failed to find the '%s' parameter",getName().c_str(),m);
        return false;
      }
    }

    XmlRpc::XmlRpcValue c = config;
    max_distance_ = static_cast<double>(c["max_distance"]);
    cost_weight_ = static_cast<double>(c["cost_weight"]);
    voxel_size_ = static_cast<double>(c["voxel_size"]);
  }
  catch(XmlRpc::XmlRpcException& e)
  {
    ROS_ERROR("%s failed to parse configuration parameters",name_.c_str());
    return false;
  }

  // initialize distance field based Collision Robot
  if(!collision_robot_df_)
  {
    ros::Time start = ros::Time::now();
    ROS_INFO("%s creating distance field",getName().c_str());
    double bandwidth = max_distance_/voxel_size_;
    collision_robot_df_.reset(new distance_field::CollisionRobotOpenVDB(robot_model_ptr_,voxel_size_,max_distance_,bandwidth, bandwidth));
    ros::Duration duration = ros::Time::now() - start;
    ROS_INFO("%s completed distance field after %f seconds",getName().c_str(), duration.toSec());
  }

  return true;
}

bool ObstacleDistanceGradient::setMotionPlanRequest(const planning_scene::PlanningSceneConstPtr& planning_scene,
                                                    const moveit_msgs::MotionPlanRequest &req,
                                                    const stomp_core::StompConfiguration &config,
                                                    moveit_msgs::MoveItErrorCodes& error_code)
{
  using namespace moveit::core;

  planning_scene_ = planning_scene;
  plan_request_ = req;
  error_code.val = moveit_msgs::MoveItErrorCodes::SUCCESS;

  // initialize distance request
  distance_request_.group_name = group_name_;
  distance_request_.acm = &planning_scene_->getAllowedCollisionMatrix();

  // storing robot state
  robot_state_.reset(new RobotState(robot_model_ptr_));
  if(!robotStateMsgToRobotState(req.start_state,*robot_state_,true))
  {
    ROS_ERROR("%s Failed to get current robot state from request",getName().c_str());
    return false;
  }

  return true;
}

bool ObstacleDistanceGradient::computeCosts(const Eigen::MatrixXd& parameters, std::size_t start_timestep,
                                            std::size_t num_timesteps, int iteration_number, int rollout_number,
                                            Eigen::VectorXd& costs, bool& validity)
{

  if(!robot_state_)
  {
    ROS_ERROR("%s Robot State has not been updated",getName().c_str());
    return false;
  }

  // allocating
  costs = Eigen::VectorXd::Zero(num_timesteps);
  const moveit::core::JointModelGroup* joint_group = robot_model_ptr_->getJointModelGroup(group_name_);

  if(parameters.cols()<start_timestep + num_timesteps)
  {
    ROS_ERROR_STREAM("Size in the 'parameters' matrix is less than required");
    return false;
  }

  // request the distance at each state
  collision_detection::DistanceResult res;
  double cost;
  for (auto t=start_timestep; t<start_timestep + num_timesteps; ++t)
  {
    robot_state_->setJointGroupPositions(joint_group,parameters.col(t));
    robot_state_->update();

    collision_robot_df_->distanceSelf(distance_request_,res,*robot_state_);

    if(res.minimum_distance.min_distance >= max_distance_)
    {
      cost = 0; // away from obstacle
    }
    else if(res.minimum_distance.min_distance < 0)
    {
      cost = 1.0; // in collision
    }
    else
    {
      cost = (max_distance_ - res.minimum_distance.min_distance)/max_distance_;
    }

    costs(t) = cost;
  }

  validity = true;
  return true;
}

void ObstacleDistanceGradient::done(bool success,int total_iterations,double final_cost)
{
  robot_state_.reset();
}

} /* namespace cost_functions */
} /* namespace stomp_moveit */
