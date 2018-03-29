// Copyright (c) 2013, Maria Teresa Lazaro Grañon
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
//   Redistributions in binary form must reproduce the above copyright notice, this
//   list of conditions and the following disclaimer in the documentation and/or
//   other materials provided with the distribution.
//
//   Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "ros_handler.h"

RosHandler::RosHandler (int idRobot, int nRobots, TypeExperiment typeExperiment){

  _idRobot = idRobot;
  _nRobots = nRobots;
  _typeExperiment = typeExperiment;

  _gtPoses = new SE2[nRobots];
  _robotMsg = new cg_mrslam::SLAM[nRobots];
  _msgEmpty = true;
  _subgt = new ros::Subscriber[nRobots];
  _subRobotMsg = new ros::Subscriber[nRobots];
  _timeLastPing = new ros::Time[nRobots];
  
  _odomTopic = "odom";
  _scanTopic = "base_scan";

  _baseFrameId = "base_link";
  _trobotlaser = SE2(0,0,0);

  _useOdom = false;
  _useLaser = false;


  std::string fullns = ros::this_node::getNamespace();
  std::string delimiter = "_";
  _rootns = fullns.substr(0, fullns.find(delimiter));

  ROS_INFO_STREAM("Robot " << _idRobot <<": NAMESPACE: " << fullns);
  ROS_INFO_STREAM("Robot " << _idRobot <<": ROOT NAMESPACE: " << _rootns);
}

void RosHandler::pingCallback(const cg_mrslam::Ping::ConstPtr& msg){
  int robot = msg->robotFrom;
  ROS_INFO_STREAM("Robot " << _idRobot <<": Received Ping from robot " << robot);
  
  _timeLastPing[robot] = ros::Time::now();
}

void RosHandler::groundTruthCallback(const nav_msgs::Odometry::ConstPtr& msg, SE2 *gtpose)
{ 
  gtpose->setTranslation(Eigen::Vector2d(msg->pose.pose.position.x,  msg->pose.pose.position.y));
  gtpose->setRotation(Eigen::Rotation2Dd(tf::getYaw(msg->pose.pose.orientation)));
}

void RosHandler::odomCallback(const nav_msgs::Odometry::ConstPtr& msg)
{ 
  _odom = *msg;
}

void RosHandler::scanCallback(const sensor_msgs::LaserScan::ConstPtr& msg)
{
  _laserscan = *msg;
}

void RosHandler::robotCallback(const cg_mrslam::SLAM::ConstPtr& msg, cg_mrslam::SLAM *robotMsg)
{
  *robotMsg = *msg;
  _msgEmpty = false;
}

SE2 RosHandler::getOdom(){
  SE2 odomSE2;
  odomSE2.setTranslation(Eigen::Vector2d(_odom.pose.pose.position.x, _odom.pose.pose.position.y));
  odomSE2.setRotation(Eigen::Rotation2Dd(tf::getYaw(_odom.pose.pose.orientation)));
  return odomSE2;
}

RobotLaser* RosHandler::getLaser(){

  LaserParameters lparams(0, _laserscan.ranges.size(), _laserscan.angle_min,  _laserscan.angle_increment, _laserscan.range_max, 0.1, 0);
  lparams.laserPose = _trobotlaser;

  RobotLaser* rlaser = new RobotLaser;
  rlaser->setLaserParams(lparams);
  rlaser->setOdomPose(getOdom());
  std::vector<double> ranges(_laserscan.ranges.size());
  for (size_t i =0; i < _laserscan.ranges.size(); i++){
    ranges[i] = _laserscan.ranges[i];
  }
  rlaser->setRanges(ranges);
  rlaser->setTimestamp(_laserscan.header.stamp.sec + _laserscan.header.stamp.nsec * pow(10, -9));
  rlaser->setLoggerTimestamp(rlaser->timestamp());
  rlaser->setHostname("hostname");

  return rlaser;
}

RobotMessage* RosHandler::getRobotMsg(int robot){
  RobotMessage* robotMsg;
  createRobotMsg(_robotMsg[robot], robotMsg);
  //ROS_INFO("Robot %i: Created RobotMsg %i", robot);
  return robotMsg;  
}

void RosHandler::init(){

  if (_useOdom){
    //Init Odom
    nav_msgs::Odometry::ConstPtr odommsg = ros::topic::waitForMessage<nav_msgs::Odometry>(_odomTopic);
    _odom = *odommsg;
  }
  
  if (_useLaser){
  //Init scan
    sensor_msgs::LaserScan::ConstPtr lasermsg = ros::topic::waitForMessage<sensor_msgs::LaserScan>(_scanTopic);
    _laserscan = *lasermsg;
    _laserMaxRange = _laserscan.range_max;

    tf::StampedTransform laserTransform;
    try {
      tf::TransformListener listener;
      listener.waitForTransform(_baseFrameId, lasermsg->header.frame_id,
				ros::Time::now(), ros::Duration(0.5));
      listener.lookupTransform(_baseFrameId, lasermsg->header.frame_id,
			       ros::Time::now(), laserTransform);
      _trobotlaser = SE2(laserTransform.getOrigin().x(),
			 laserTransform.getOrigin().y(),
			 tf::getYaw(laserTransform.getRotation()));
    }
    catch (tf::TransformException ex){
      ROS_ERROR("cg_mrslam: %s",ex.what());
      ros::Duration(1.0).sleep();
    }

    ROS_INFO_STREAM("Robot " << _idRobot << ": Robot-laser transform: (" << _trobotlaser.translation().x() << ", " << _trobotlaser.translation().y() << ", " << _trobotlaser.rotation().angle() << ")");
  }

  if (_typeExperiment == SIM){
    //Init ground-truth
    for (int r = 0; r < _nRobots; r++){
      std::stringstream nametopic;
      nametopic << _rootns << "_" << r << "/base_pose_ground_truth";
      nav_msgs::Odometry::ConstPtr gtmsg = ros::topic::waitForMessage<nav_msgs::Odometry>(nametopic.str());
      _gtPoses[r] = SE2(gtmsg->pose.pose.position.x, gtmsg->pose.pose.position.y, tf::getYaw(gtmsg->pose.pose.orientation));
    }
  }

}

void RosHandler::run(){
  if (_useOdom) //Subscribe Odom
    _subOdom = _nh.subscribe<nav_msgs::Odometry>(_odomTopic, 1, &RosHandler::odomCallback, this);
    
  if (_useLaser) //Subscribe Laser
    _subScan = _nh.subscribe<sensor_msgs::LaserScan>(_scanTopic, 1,  &RosHandler::scanCallback, this);

  if (_typeExperiment == BAG){
    //subscribe pings
    _subPing = _nh.subscribe<cg_mrslam::Ping>("ping_msgs", 1, &RosHandler::pingCallback, this);
  } else if (_typeExperiment == SIM){
    //subscribe ground truth
    for (int r = 0; r < _nRobots; r++){
      std::stringstream nametopic;
      nametopic << _rootns << "_" << r << "/base_pose_ground_truth";
      _subgt[r] = _nh.subscribe<nav_msgs::Odometry>(nametopic.str(), 1, boost::bind(&RosHandler::groundTruthCallback, this, _1, &_gtPoses[r]));
    }
  }// else if (_typeExperiment == REAL){
    //publish ping, sent and received messages
  _pubRecv = _nh.advertise<cg_mrslam::SLAM>("recv_msgs", 1);
  _pubSent = _nh.advertise<cg_mrslam::SLAM>("sent_msgs", 1);
  _pubPing = _nh.advertise<cg_mrslam::Ping>("ping_msgs", 1);
  //}
  for (int r = 0; r < _nRobots; r++){
    if (r != _idRobot){
      std::stringstream robotopic;
      robotopic << _rootns << "_" << r << "/sent_msgs";
      _subRobotMsg[r] = _nh.subscribe<cg_mrslam::SLAM>(robotopic.str(), 10, boost::bind(&RosHandler::robotCallback, this, _1, &_robotMsg[r]));
      ROS_INFO_STREAM("Robot " << _idRobot << ": Subscribed to: " << robotopic.str());
    }
  }
}


void RosHandler::publishPing(int idRobotFrom){
  cg_mrslam::Ping rosmsg;

  rosmsg.header.stamp = ros::Time::now();

  rosmsg.robotFrom = idRobotFrom;
  rosmsg.robotTo   = _idRobot;
  
  _pubPing.publish(rosmsg);
}

void RosHandler::createComboMsg(ComboMessage* cmsg, cg_mrslam::SLAM& dslamMsg){
  dslamMsg.header.stamp = ros::Time::now();

  dslamMsg.robotId = cmsg->robotId();
  dslamMsg.type = cmsg->type();
  
  cg_mrslam::RobotLaser laser;
  laser.nodeId   = cmsg->nodeId;
  laser.readings = cmsg->readings;
  laser.minAngle = cmsg->minangle;
  laser.angleInc = cmsg->angleincrement;
  laser.maxRange = cmsg->maxrange;
  laser.accuracy = cmsg->accuracy;
  dslamMsg.laser = laser;

  dslamMsg.vertices.resize(cmsg->vertexVector.size());
  for (size_t i = 0; i < cmsg->vertexVector.size(); i++){
    dslamMsg.vertices[i].id = cmsg->vertexVector[i].id;
    dslamMsg.vertices[i].estimate[0] = cmsg->vertexVector[i].estimate[0];
    dslamMsg.vertices[i].estimate[1] = cmsg->vertexVector[i].estimate[1];
    dslamMsg.vertices[i].estimate[2] = cmsg->vertexVector[i].estimate[2];   
  }
}

void RosHandler::createCondensedGraphMsg(CondensedGraphMessage* gmsg,  cg_mrslam::SLAM& dslamMsg){
  dslamMsg.header.stamp = ros::Time::now();

  dslamMsg.robotId = gmsg->robotId();
  dslamMsg.type = gmsg->type();
  
  dslamMsg.edges.resize(gmsg->edgeVector.size());
  for (size_t i = 0; i < gmsg->edgeVector.size(); i++){
    dslamMsg.edges[i].idFrom = gmsg->edgeVector[i].idfrom;
    dslamMsg.edges[i].idTo = gmsg->edgeVector[i].idto;
    dslamMsg.edges[i].estimate[0] = gmsg->edgeVector[i].estimate[0];
    dslamMsg.edges[i].estimate[1] = gmsg->edgeVector[i].estimate[1];
    dslamMsg.edges[i].estimate[2] = gmsg->edgeVector[i].estimate[2];
    dslamMsg.edges[i].information[0] = gmsg->edgeVector[i].information[0];
    dslamMsg.edges[i].information[1] = gmsg->edgeVector[i].information[1];
    dslamMsg.edges[i].information[2] = gmsg->edgeVector[i].information[2];
    dslamMsg.edges[i].information[3] = gmsg->edgeVector[i].information[3];
    dslamMsg.edges[i].information[4] = gmsg->edgeVector[i].information[4];
    dslamMsg.edges[i].information[5] = gmsg->edgeVector[i].information[5];
  }

  dslamMsg.closures = gmsg->closures;
}

void RosHandler::createDSlamMsg(RobotMessage* msg, cg_mrslam::SLAM& dslamMsg){
  ComboMessage* cmsg = dynamic_cast<ComboMessage*>(msg);
  if (cmsg)
    createComboMsg(cmsg, dslamMsg);
  else{
    CondensedGraphMessage* gmsg = dynamic_cast<CondensedGraphMessage*>(msg);
    if (gmsg)
      createCondensedGraphMsg(gmsg, dslamMsg);
  }
}

void RosHandler::createRobotMsg(cg_mrslam::SLAM& slamMsg, RobotMessage* robotMsg){
  if (!_msgEmpty){
    ROS_INFO("Robot %i: Received msg type is: %i", _idRobot, slamMsg.type);  //robotMsg->header.stamp = ros::Time::now();
    if (slamMsg.type == 7){
      CondensedGraphMessage* gmsg = dynamic_cast<CondensedGraphMessage*>(robotMsg);
      gmsg = new CondensedGraphMessage(slamMsg.robotId);
      
      gmsg->edgeVector.resize(slamMsg.edges.size());
      for (size_t i = 0; i < slamMsg.edges.size(); i++){
        gmsg->edgeVector[i].idfrom      = slamMsg.edges[i].idFrom;
        gmsg->edgeVector[i].idto        = slamMsg.edges[i].idTo;
        gmsg->edgeVector[i].estimate[0] = slamMsg.edges[i].estimate[0];
        gmsg->edgeVector[i].estimate[1] = slamMsg.edges[i].estimate[1];
        gmsg->edgeVector[i].estimate[2] = slamMsg.edges[i].estimate[2];
        gmsg->edgeVector[i].information[0] = slamMsg.edges[i].information[0];
        gmsg->edgeVector[i].information[1] = slamMsg.edges[i].information[1];
        gmsg->edgeVector[i].information[2] = slamMsg.edges[i].information[2];
        gmsg->edgeVector[i].information[3] = slamMsg.edges[i].information[3];
        gmsg->edgeVector[i].information[4] = slamMsg.edges[i].information[4];
        gmsg->edgeVector[i].information[5] = slamMsg.edges[i].information[5];
      }
      gmsg->closures = slamMsg.closures;
      * robotMsg = * gmsg;
    } else if (slamMsg.type == 4){
      ComboMessage* cmsg = dynamic_cast<ComboMessage*>(robotMsg);
      cmsg = new ComboMessage(slamMsg.robotId);
      cmsg->nodeId          = slamMsg.laser.nodeId;
      cmsg->readings        = slamMsg.laser.readings;
      cmsg->minangle        = slamMsg.laser.minAngle;
      cmsg->angleincrement  = slamMsg.laser.angleInc;
      cmsg->maxrange        = slamMsg.laser.maxRange;
      cmsg->accuracy        = slamMsg.laser.accuracy;
      cmsg->vertexVector.resize(slamMsg.vertices.size());
      for (size_t i = 0; i < slamMsg.vertices.size(); i++){
        cmsg->vertexVector[i].id          = slamMsg.vertices[i].id;
        cmsg->vertexVector[i].estimate[0] = slamMsg.vertices[i].estimate[0];
        cmsg->vertexVector[i].estimate[1] = slamMsg.vertices[i].estimate[1];
        cmsg->vertexVector[i].estimate[2] = slamMsg.vertices[i].estimate[2];   
      }
      * robotMsg = * cmsg;
    } else {
      ROS_INFO("Robot %i: Message type unknown", _idRobot);
      robotMsg = nullptr;
    }
  } else {
    //ROS_INFO("Robot %i: SLAM MSG is empty", _idRobot);
    robotMsg = nullptr;
  }
}

void RosHandler::publishSentMsg(RobotMessage* msg){
  cg_mrslam::SLAM dslamMsg;

  createDSlamMsg(msg, dslamMsg);
  _pubSent.publish(dslamMsg);
}

void RosHandler::publishReceivedMsg(RobotMessage* msg){
  cg_mrslam::SLAM dslamMsg;

  createDSlamMsg(msg, dslamMsg);
  _pubRecv.publish(dslamMsg);
}
