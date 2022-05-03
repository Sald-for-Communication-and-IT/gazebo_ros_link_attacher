#include <boost/thread/recursive_mutex.hpp>
#include <gazebo/common/Plugin.hh>
#include <ros/ros.h>
#include "gazebo_ros_link_attacher.h"
#include "gazebo_ros_link_attacher/Attach.h"
#include "gazebo_ros_link_attacher/AttachRequest.h"
#include "gazebo_ros_link_attacher/AttachResponse.h"
#include <ignition/math/Pose3.hh>

namespace gazebo
{
  // Register this plugin with the simulator
  GZ_REGISTER_WORLD_PLUGIN(GazeboRosLinkAttacher)

  // Constructor
  GazeboRosLinkAttacher::GazeboRosLinkAttacher() :
    nh_("link_attacher_node")
  {
  }


  // Destructor
  GazeboRosLinkAttacher::~GazeboRosLinkAttacher()
  {
  }

  void GazeboRosLinkAttacher::Load(physics::WorldPtr _world, sdf::ElementPtr /*_sdf*/)
  {
    // Make sure the ROS node for Gazebo has already been initialized                                                                                    
    if (!ros::isInitialized())
    {
      ROS_FATAL_STREAM("A ROS node for Gazebo has not been initialized, unable to load plugin. "
        << "Load the Gazebo system plugin 'libgazebo_ros_api_plugin.so' in the gazebo_ros package)");
      return;
    }
    
    this->world = _world;
    this->physics = this->world->Physics();
    this->physics_mutex = this->physics->GetPhysicsUpdateMutex();

    this->attach_service_ = this->nh_.advertiseService("attach", &GazeboRosLinkAttacher::attach_callback, this);
    ROS_INFO_STREAM("Attach service at: " << this->nh_.resolveName("attach"));
    this->detach_service_ = this->nh_.advertiseService("detach", &GazeboRosLinkAttacher::detach_callback, this);
    ROS_INFO_STREAM("Detach service at: " << this->nh_.resolveName("detach"));
    ROS_INFO("Link attacher node initialized.");
  }

  bool GazeboRosLinkAttacher::attach(std::string model1, std::string link1,
                                     std::string model2, std::string link2, geometry_msgs::Point offset)
  {

    // look for any previous instance of the joint first.
    // if we try to create a joint in between two links
    // more than once (even deleting any reference to the first one)
    // gazebo hangs/crashes
    fixedJoint j;
    if(this->getJoint(model1, link1, model2, link2, j)){
        ROS_INFO_STREAM("Joint already existed, reusing it.");
        if(!this->alignLinks(j.model2+"::"+j.link2, j.model1+"::"+j.link1, 
                             ignition::math::Pose3d(offset.x, offset.y, offset.z, 0, 0, 0))){
          return false;
        }
        j.joint->Attach(j.l1, j.l2);
        return true;
    }
    else{
        ROS_INFO_STREAM("Creating new joint.");
    }
    j.model1 = model1;
    j.link1 = link1;
    j.model2 = model2;
    j.link2 = link2;
    j.offset = ignition::math::Pose3d(offset.x, offset.y, offset.z, 0.0, 0.0, 0.0);

    if(!this->alignLinks(j.model2+"::"+j.link2, j.model1+"::"+j.link1, j.offset)){
      return false;
    }

    ROS_DEBUG_STREAM("Getting BasePtr of " << model1);
    physics::BasePtr b1 = this->world->ModelByName(model1);

    if (b1 == NULL){
      ROS_ERROR_STREAM(model1 << " model was not found");
      return false;
    }
    ROS_DEBUG_STREAM("Getting BasePtr of " << model2);
    physics::BasePtr b2 = this->world->ModelByName(model2);
    if (b2 == NULL){
      ROS_ERROR_STREAM(model2 << " model was not found");
      return false;
    }

    ROS_DEBUG_STREAM("Casting into ModelPtr");
    physics::ModelPtr m1(dynamic_cast<physics::Model*>(b1.get()));
    j.m1 = m1;
    physics::ModelPtr m2(dynamic_cast<physics::Model*>(b2.get()));
    j.m2 = m2;

    ROS_DEBUG_STREAM("Getting link: '" << link1 << "' from model: '" << model1 << "'");
    physics::LinkPtr l1 = m1->GetLink(link1);
    if (l1 == NULL){
      ROS_ERROR_STREAM(link1 << " link was not found");
      return false;
    }
    if (l1->GetInertial() == NULL){
        ROS_ERROR_STREAM("link1 inertia is NULL!");
    }
    else
        ROS_DEBUG_STREAM("link1 inertia is not NULL, for example, mass is: " << l1->GetInertial()->Mass());
    j.l1 = l1;
    ROS_DEBUG_STREAM("Getting link: '" << link2 << "' from model: '" << model2 << "'");
    physics::LinkPtr l2 = m2->GetLink(link2);
    if (l2 == NULL){
      ROS_ERROR_STREAM(link2 << " link was not found");
      return false;
    }
    if (l2->GetInertial() == NULL){
        ROS_ERROR_STREAM("link2 inertia is NULL!");
    }
    else
        ROS_DEBUG_STREAM("link2 inertia is not NULL, for example, mass is: " << l2->GetInertial()->Mass());
    j.l2 = l2;

    ROS_DEBUG_STREAM("Links are: "  << l1->GetName() << " and " << l2->GetName());

    ROS_DEBUG_STREAM("Creating revolute joint on model: '" << model1 << "'");
    j.joint = this->physics->CreateJoint("revolute", m1);
    this->joints.push_back(j);

    ROS_DEBUG_STREAM("Attach");
    j.joint->Attach(l1, l2);
    ROS_DEBUG_STREAM("Loading links");
    j.joint->Load(l1, l2, ignition::math::Pose3d());
    ROS_DEBUG_STREAM("SetModel");
    j.joint->SetModel(m2);
    /*
     * If SetModel is not done we get:
     * ***** Internal Program Error - assertion (this->GetParentModel() != __null)
     failed in void gazebo::physics::Entity::PublishPose():
     /tmp/buildd/gazebo2-2.2.3/gazebo/physics/Entity.cc(225):
     An entity without a parent model should not happen

     * If SetModel is given the same model than CreateJoint given
     * Gazebo crashes with
     * ***** Internal Program Error - assertion (self->inertial != __null)
     failed in static void gazebo::physics::ODELink::MoveCallback(dBodyID):
     /tmp/buildd/gazebo2-2.2.3/gazebo/physics/ode/ODELink.cc(183): Inertial pointer is NULL
     */

    ROS_DEBUG_STREAM("SetHightstop");
    j.joint->SetUpperLimit(0, 0);
    ROS_DEBUG_STREAM("SetLowStop");
    j.joint->SetLowerLimit(0, 0);
    ROS_DEBUG_STREAM("Init");
    j.joint->Init();
    ROS_INFO_STREAM("Attach finished.");

    return true;
  }

  bool GazeboRosLinkAttacher::detach(std::string model1, std::string link1,
                                     std::string model2, std::string link2, geometry_msgs::Point offset)
  {
      // search for the instance of joint and do detach
      fixedJoint j;
      if(this->getJoint(model1, link1, model2, link2, j)){
        {
          boost::recursive_mutex::scoped_lock lock(*this->physics_mutex);
          j.joint->Detach();
        }
       if(this->alignLinks(j.model2+"::"+j.link2, j.model1+"::"+j.link1, 
                           ignition::math::Pose3d(offset.x, offset.y, offset.z, 0, 0, 0)))
        {
            return true;
        }
      }
  
    return false;
  }

  bool GazeboRosLinkAttacher::getJoint(std::string model1, std::string link1,
                                       std::string model2, std::string link2,
                                       fixedJoint &joint){
    fixedJoint j;
    for(std::vector<fixedJoint>::iterator it = this->joints.begin(); it != this->joints.end(); ++it){
        j = *it;
        if ((j.model1.compare(model1) == 0) && (j.model2.compare(model2) == 0)
                && (j.link1.compare(link1) == 0) && (j.link2.compare(link2) == 0)){
            joint = j;
            return true;
        }
    }
    return false;

  }

  bool GazeboRosLinkAttacher::attach_callback(gazebo_ros_link_attacher::Attach::Request &req,
                                              gazebo_ros_link_attacher::Attach::Response &res)
  {
    ROS_INFO_STREAM("Received request to attach model: '" << req.model_name_1
                    << "' using link: '" << req.link_name_1 << "' with model: '"
                    << req.model_name_2 << "' using link: '" <<  req.link_name_2 << "'");
    if (! this->attach(req.model_name_1, req.link_name_1,
                       req.model_name_2, req.link_name_2, req.offset)){
      ROS_ERROR_STREAM("Could not make the attach.");
      res.ok = false;
    }
    else{
      ROS_INFO_STREAM("Attach was succesful");
      res.ok = true;
    }
    return true;

  }

  bool GazeboRosLinkAttacher::detach_callback(gazebo_ros_link_attacher::Attach::Request &req,
                                              gazebo_ros_link_attacher::Attach::Response &res){
      ROS_INFO_STREAM("Received request to detach model: '" << req.model_name_1
                      << "' using link: '" << req.link_name_1 << "' with model: '"
                      << req.model_name_2 << "' using link: '" <<  req.link_name_2 << "'");
      if (! this->detach(req.model_name_1, req.link_name_1,
                         req.model_name_2, req.link_name_2, req.offset)){
        ROS_ERROR_STREAM("Could not make the detach.");
        res.ok = false;
      }
      else{
        ROS_INFO_STREAM("Detach was succesful");
        res.ok = true;
      }
      return true;
  }

  bool GazeboRosLinkAttacher::alignLinks(std::string child_link, std::string parent_link, ignition::math::Pose3d pose)
  { 
    ROS_INFO("Starting link alignment!");

    gazebo::physics::LinkPtr body = boost::dynamic_pointer_cast<gazebo::physics::Link>(this->world->EntityByName(child_link));
    gazebo::physics::LinkPtr frame = boost::dynamic_pointer_cast<gazebo::physics::Link>(this->world->EntityByName(parent_link));
    
    if (!body)
    {
      ROS_ERROR("Updating LinkState: link [%s] does not exist", child_link.c_str());
      return false;
    }
    
    ignition::math::Vector3d target_pos = pose.Pos();
    ignition::math::Quaterniond target_rot = pose.Rot();
    ignition::math::Pose3d target_pose(target_pos,target_rot);
    ignition::math::Vector3d target_linear_vel(0,0,0);
    ignition::math::Vector3d target_angular_vel(0,0,0);

    if (frame)
    {
  
      ignition::math::Pose3d  frame_pose = frame->WorldPose(); // - myBody->GetCoMPose();
      ignition::math::Vector3d frame_linear_vel = frame->WorldLinearVel();
      // ignition::math::Vector3d frame_angular_vel = frame->WorldAngularVel();

      ignition::math::Vector3d frame_pos = frame_pose.Pos();
      ignition::math::Quaterniond frame_rot = frame_pose.Rot();

      //std::cout << " debug : " << frame->GetName() << " : " << frame_pose << " : " << target_pose << std::endl;
      target_pose = target_pose + frame_pose;

      // target_linear_vel -= frame_linear_vel;
      // target_angular_vel -= frame_angular_vel;
    }
    else if (parent_link == "" || parent_link == "world" || parent_link == "map" || parent_link == "/map")
    {
      ROS_INFO("Updating LinkState: reference_frame is empty/world/map, using inertial frame");
    }
    else
    {
      ROS_ERROR("Updating LinkState: reference_frame is not a valid entity name");
      return false;
    }

    //std::cout << " debug : " << target_pose << std::endl;
    //boost::recursive_mutex::scoped_lock lock(*world->GetMRMutex());

    bool is_paused = this->world->IsPaused();
    if (!is_paused) this->world->SetPaused(true);
    body->SetWorldPose(target_pose);
    this->world->SetPaused(is_paused);

    // set body velocity to desired twist
    // body->SetLinearVel(target_linear_vel);
    // body->SetAngularVel(target_angular_vel);
    ROS_INFO("Link alignment successful!");
    return true;
  }

}
