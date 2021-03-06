#include <moveit/move_group_interface/move_group_interface.h>   // replace the old version "move_group.h"
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/DisplayTrajectory.h>
#include <kinova_driver/kinova_ros_types.h>
#include <actionlib/client/simple_action_client.h>
#include <kinova_msgs/SetFingersPositionAction.h>
#include <iostream>
#include <vector>
#include <tf2_ros/transform_listener.h>
//手指控制
#include "kinova_driver/kinova_tool_pose_action.h"
#include "kinova_driver/kinova_joint_angles_action.h"
#include "kinova_driver/kinova_fingers_action.h"
#include "robotiq_2f_gripper_control/gripperControl.h"  //robotiq二指手
// 消息定义
#include <std_msgs/Int8.h>
#include "kinova_arm_moveit_demo/targetsVector.h"   //自定义消息类型，所有识别定位结果
#include "kinova_arm_moveit_demo/targetState.h"     //自定义消息类型，单个识别定位结果
#include "rviz_teleop_commander/targets_tag.h"      //自定义消息类型，传递要抓取的目标标签
#include "rviz_teleop_commander/grab_result.h"      //自定义消息类型，传递当前抓取的目标标签和抓取次数
#include <Eigen/Eigen>
#include <sensor_msgs/JointState.h>                 //关节位置信息

using namespace std;
using namespace Eigen;

//-------------------------------------------------全局变量----------------------------------------------------
const int n_MAX=3;                                  //同一物品最大抓取次数
const int N_MAX=70;                                 //循环抓取允许最大识别不到的次数，超出此次数识别结束
vector<kinova_arm_moveit_demo::targetState> targets;//视觉定位结果
bool getTargets=0;                                	//当接收到视觉定位结果时getTargets置1，执行完放置后置0
geometry_msgs::Pose placePose;                      //机械臂抓取放置位置,为规划方便，将放置位置设为起始位置
vector<int> targetsTag;                           	//需要抓取的目标物的标签
bool getTargetsTag=0;                             	//当接收到需要抓取的目标物的标签时置1，等待结束后置0

//-------------------------------------------------相机相关--------------------------------------------------
//相机参数和深度信息用于计算
#define Fxy 692.97839
#define UV0 400.5
#define Zw 0.77
//手眼关系
Eigen::Matrix3d hand2eye_r;
Eigen::Vector3d hand2eye_t;
Eigen::Quaterniond hand2eye_q;

//-------------------------------------------------手爪相关------------------------------------------------
//手指client类型自定义
typedef actionlib::SimpleActionClient<kinova_msgs::SetFingersPositionAction> Finger_actionlibClient;
//机器人类型相关
string kinova_robot_type = "j2s7s300";
string Finger_action_address = "/" + kinova_robot_type + "_driver/fingers_action/finger_positions";    //手指控制服务器的名称
string base_frame = "base_link";// tf中基坐标系的名称
string tool_frame = "wrist_3_link";// tf中工具坐标系的名称
//定义手指控制client
Finger_actionlibClient* client=NULL;
//爪子开闭程度
float openVal=0.4;
float closeVal=0.9;
float highVal=0.05;
float openVal_real=0.4;
float colseVal_real=0.9;
const double FINGER_MAX = 6400;	//手指开合程度：0完全张开，6400完全闭合
//最合适的抓取参数            1       2      3      4      5      6      7      8      9      10
float closeVals[10]=    {1.200, 0.900, 1.050, 1.150, 1.200, 1.050, 0.960, 1.300, 0.950, 1.200};// 爪子闭合程度
float highVals[10]=     {0.065, 0.065, 0.050, 0.025, 0.040, 0.030, 0.020, 0.065, 0.050, 0.030};// 抓取高度
float openVals[10]=     {0.900, 0.400, 0.400, 0.800, 0.800, 0.400, 0.400, 0.400, 0.800, 0.400};// 爪子张开程度
float openVals_real[10]={0.450, 0.550, 0.500, 0.450, 0.450, 0.450, 0.450, 0.500, 0.550, 0.400};

// -------------------------------------------------函数定义-------------------------------------------------
//接收到detect_result消息的回调函数，将消息内容赋值到全局变量targets里面
void detectResultCB(const kinova_arm_moveit_demo::targetsVector &msg);

//接收targets_tag消息的回调函数，将接收到的消息更新到targetsTag里面
void tagsCB(const rviz_teleop_commander::targets_tag &msg);

//循环检测当前视觉识别中是否还有要抓取的目标
void haveGoal(const vector<int>& targetsTag, int& cur_target, kinova_arm_moveit_demo::targetState& curTargetPoint, int& n, int& goalState, const tf2_ros::Buffer &tfBuffer_);

//手抓控制函数，输入0-1之间的控制量，控制手抓开合程度，0完全张开，1完全闭合
bool fingerControl(double finger_turn);

//机械臂运动控制函数
void pickAndPlace(kinova_arm_moveit_demo::targetState curTargetPoint);

//抓取插值函数
std::vector<geometry_msgs::Pose> pickInterpolate(geometry_msgs::Pose startPose,geometry_msgs::Pose targetPose);

//放置插值函数
std::vector<geometry_msgs::Pose> placeInterpolate(geometry_msgs::Pose startPose,geometry_msgs::Pose targetPose);

//设置机械臂放置位置,经多次测试，发现必须用函数设置初始位置．
void setPlacePose();

//前往放置位置
void goPlacePose(geometry_msgs::Pose placePose);

// 转换位姿，用于控制UR实物
geometry_msgs::Pose changePoseForUR(geometry_msgs::Pose pose);

// 获得机器人工具坐标系到基坐标系的转换关系
int getTransBase2hand(const tf2_ros::Buffer& tfBuffer_, Eigen::Matrix3d& trans_r, Eigen::Vector3d& trans_t);

// -------------------------------------------------主程序入口-------------------------------------------------
int main(int argc, char **argv)
{
	/*************************************/
	/********初始化设置*******************/
	/*************************************/
  ros::init(argc, argv, "UR_real");
	ros::NodeHandle node_handle;  
	ros::AsyncSpinner spinner(3);
	spinner.start();

  client = new Finger_actionlibClient(Finger_action_address, true);

	//发布消息和订阅消息
  ros::Publisher display_publisher = node_handle.advertise<moveit_msgs::DisplayTrajectory>("/move_group/display_planned_path", 1, true);
  moveit_msgs::DisplayTrajectory display_trajectory;
	ros::Publisher detectTarget_pub = node_handle.advertise<std_msgs::Int8>("detect_target", 10);  //让visual_detect节点检测目标
	ros::Publisher grab_result_pub = node_handle.advertise<rviz_teleop_commander::grab_result>("grab_result", 1);  //发布抓取状态
	ros::Subscriber detectResult_sub = node_handle.subscribe("detect_result", 1, detectResultCB);    //接收visual_detect检测结果
	ros::Subscriber tags_sub = node_handle.subscribe("targets_tag", 1, tagsCB);				//接收要抓取的目标 qcrong
  //robotiq手爪话题名在kinetic下不知是否有变更-2019zp
  ros::Publisher gripperPub = node_handle.advertise<robotiq_2f_gripper_control::Robotiq2FGripper_robot_output>("/Robotiq2FGripper_robot_output", 20);
  //获取工具坐标系
  tf2_ros::Buffer tfBuffer;
  tf2_ros::TransformListener tfListener(tfBuffer);  //获取机械臂末端在基坐标系下的位姿

	int n=0;		//记录对同一目标抓取的次数
	std_msgs::Int8 detectTarget;

	//手眼关系赋值
  hand2eye_r<<0.02775470241621737, -0.9983886629987773, 0.04949491417900936,
              -0.9984233782515894, -0.03010426443992026, -0.04737458839529671,
              0.04878826198507039, -0.04810198253680015, -0.9976501245313218;
  hand2eye_t<<-0.4961986013429963,-0.6043712289857819,0.7137581412140669;
  hand2eye_q=hand2eye_r;

  setPlacePose();
  goPlacePose(placePose);
	
	/*************************************/
	/********目标输入*********************/
	/*************************************/
	int cur_target=0;					//当前抓取目标的序号
  ROS_INFO("waiting for tags of targets input in GUI");
  while(getTargetsTag!=1)				//等待抓取目标输入
	{
		ros::Duration(0.5).sleep();
		if(!ros::ok())
		{
			detectTarget.data=2;		//让visual_detect节点退出
			detectTarget_pub.publish(detectTarget);
			ros::Duration(1.0).sleep();
			return 1;
		}
	}
	getTargetsTag=0;	//等待完毕，getTargetsTag置0
	detectTarget.data=1;		//让visual_detect节点检测目标
	detectTarget_pub.publish(detectTarget);
	ros::Duration(1.0).sleep();


	/*************************************/
	/********目标抓取*********************/
	/*************************************/

  ROS_INFO("All ready, waiting for goal.");

  gripperPubPtr = &gripperPub;
  initializeGripperMsg();
  rviz_teleop_commander::grab_result grabResultMsg;

  //等待目标传入并执行
	while(ros::ok())
	{
		if(getTargets==1)	//收到视觉检测结果
		{
			//判断当前抓取目标是否存在
      kinova_arm_moveit_demo::targetState curTargetPoint;
			//循环抓取
			int goalState=0;
      haveGoal(targetsTag,cur_target,curTargetPoint,n,goalState,tfBuffer);
			if(goalState==1)  //有要抓取的目标
			{
				//发布抓取状态
				grabResultMsg.now_target=targetsTag[cur_target];
				grabResultMsg.grab_times=n;
				grab_result_pub.publish(grabResultMsg);

				//进行抓取放置，要求抓取放置后返回初始位置
        //机械臂运动控制---执行抓取－放置－过程
        pickAndPlace(curTargetPoint);
				
				getTargets=0;		//执行完抓取置0，等待下一次视觉检测结果
				//让visual_detect节点进行检测
				detectTarget.data=1;		//让visual_detect节点进行视觉检测
				detectTarget_pub.publish(detectTarget);
			}
			else if(goalState==2)
			{
				//让visual_detect节点进行检测
				detectTarget.data=1;		//让visual_detect节点进行视觉检测
				detectTarget_pub.publish(detectTarget);
			}
			else if(goalState==3)			//所有目标抓取完成
			{
        ROS_INFO("There is no goal left.");
				break;
			}
			cur_target++;
			
		}
		ros::Duration(0.2).sleep();
	}
	
	//退出程序
	detectTarget.data=2;		//让visual_detect节点退出
	detectTarget_pub.publish(detectTarget);
	//发布抓取状态
	grabResultMsg.now_target=-1;
	grabResultMsg.grab_times=-1;
	grab_result_pub.publish(grabResultMsg);
	ros::Duration(1.0).sleep();
	return 0;
}

/*************************************/
/********函数定义*********************/
/*************************************/

//接收到detect_result消息的回调函数，将消息内容赋值到全局变量targets里面
void detectResultCB(const kinova_arm_moveit_demo::targetsVector &msg)
{
	int num = msg.targets.size();
  targets.clear();
	targets.resize(num);
	for(int i=0; i<num; i++)
	{
		targets[i]=msg.targets[i];
		//ROS_INFO("%d",msg.targets[i].tag);
		//ROS_INFO("%f %f %f",msg.targets[i].x,msg.targets[i].y,msg.targets[i].z);
	}
	getTargets=1;	//接收到视觉定位结果getTargets置1
}
//接收targets_tag消息的回调函数，将接收到的消息更新到targetsTag里面
void tagsCB(const rviz_teleop_commander::targets_tag &msg)
{
	int num=msg.targetsTag.size();
  targetsTag.clear();
  targetsTag.resize(num);
  ROS_INFO("Amount of targets = %d",num);
	for(int i=0; i<num; i++)
	{
		targetsTag[i]=msg.targetsTag[i];
		ROS_INFO(" [%d]", targetsTag[i]);
	}
	getTargetsTag=1;	//接收到需要抓取的目标物的标签
}

//循环检测当前视觉识别中是否还有要抓取的目标
void haveGoal(const vector<int>& targetsTag, int& cur_target, kinova_arm_moveit_demo::targetState& curTargetPoint, int& n, int& goalState, const tf2_ros::Buffer& tfBuffer_)
{
	int n_targetsTag=targetsTag.size();	//目标标签个数
	int n_targets=targets.size();		//检测到的物品的个数
	cur_target=cur_target%n_targetsTag;
	for(int i=0;i<n_targets;i++)
	{
		if(targetsTag[cur_target]==targets[i].tag)
		{
      //目标物在相机坐标系下的坐标转换到机器人工具坐标系下
			Eigen::Vector3d cam_center3d, base_center3d;

      cam_center3d(0)=targets[i].x;
      cam_center3d(1)=targets[i].y;
      cam_center3d(2)=targets[i].z;

      base_center3d=hand2eye_r*cam_center3d+hand2eye_t;
			Eigen::Quaterniond quater(targets[i].qw,targets[i].qx,targets[i].qy,targets[i].qz);
      quater=hand2eye_q*quater;
			//Eigen::Matrix3d tempm=quater.matrix();
			//ROS_INFO("quater: %f %f %f",curTargetPoint.x,curTargetPoint.y,curTargetPoint.z);
			//cout<<"tempm:"<<endl<<tempm<<endl;
	

      //目标物从工具坐标系转到基坐标系下
      Eigen::Matrix3d base2hand_r;
      Eigen::Vector3d base2hand_t;
      Eigen::Quaterniond base2hand_q;

      // 用tf给转换赋值
      getTransBase2hand(tfBuffer_,base2hand_r,base2hand_t);
      base2hand_q=base2hand_r;

      base_center3d=base2hand_r*base_center3d+base2hand_t;
      quater=base2hand_q*quater;

			//获取当前抓取物品的位置
			curTargetPoint.x=base_center3d(0);
			curTargetPoint.y=base_center3d(1)+0.04;
			curTargetPoint.z=base_center3d(2);
			ROS_INFO("curTargetPoint: %f %f %f",curTargetPoint.x,curTargetPoint.y,curTargetPoint.z);
			//curTargetPoint.x=-0.27;
			//curTargetPoint.y=0.5;

			curTargetPoint.qx=quater.x();	
			curTargetPoint.qy=quater.y();
			curTargetPoint.qz=quater.z();
			curTargetPoint.qw=quater.w();


			ROS_INFO("have goal 1");
			ROS_INFO("%d",targets[i].tag);
			//ROS_INFO("%f %f %f %f",curTargetPoint.qx,curTargetPoint.qy,curTargetPoint.qz,curTargetPoint.qw);
			//手抓闭合程度，抓取高度
      openVal_real=openVals_real[targetsTag[cur_target]-1];

			n++;
			goalState=1;  	//找到目标
			return;
		}
	}
	
	n++;
	if(n>N_MAX)
	{
		goalState=3;		//没有目标，退出
		return;
	}
	else
	goalState==2;			//当前帧没有要抓取的目标继续检测
	return;
}

//手抓控制函数，输入0-1之间的控制量，控制手抓开合程度，0完全张开，1完全闭合 added by yang 20180418
bool fingerControl(double finger_turn)
{
    if (finger_turn < 0)
    {
        finger_turn = 0.0;
    }
    else
    {
        finger_turn = std::min(finger_turn, 1.0);
    }
    kinova_msgs::SetFingersPositionGoal goal;
    goal.fingers.finger1 = finger_turn * FINGER_MAX;
    goal.fingers.finger2 = goal.fingers.finger1;
    goal.fingers.finger3 = goal.fingers.finger1;
    client->sendGoal(goal);
    if (client->waitForResult(ros::Duration(5.0)))
    {
      client->getResult();
      return true;
    }
    else
    {
      client->cancelAllGoals();
      ROS_WARN_STREAM("The gripper action timed-out");
      return false;
    }
}

void pickAndPlace(kinova_arm_moveit_demo::targetState curTargetPoint)
{
//流程介绍
//1--获得目标点并对路径进行插值
//2--执行插值后的路径
//3--到达目标点抓取物体
//4--从目标点到放置点进行插值
//5--执行插值后的路径
//6--放置物体
//7--等待下一个目标点
  moveit::planning_interface::MoveGroupInterface arm_group("manipulator");	//manipulator

  geometry_msgs::Pose targetPose;	//定义抓取位姿
  geometry_msgs::Point point;
  geometry_msgs::Quaternion orientation;
 
	sendGripperMsg(0);//open                

  point.x = curTargetPoint.x;//获取抓取位姿
  point.y = curTargetPoint.y;
  point.z =0.07;

  moveit::planning_interface::MoveGroupInterface::Plan pick_plan;
  moveit::planning_interface::MoveGroupInterface::Plan place_plan;

  orientation.x = curTargetPoint.qx;//方向由视觉节点给定
  orientation.y = curTargetPoint.qy;
  orientation.z = curTargetPoint.qz;
  orientation.w = curTargetPoint.qw;

  targetPose.position = point;// 设置好目标位姿为可用的格式
  targetPose.orientation = orientation;

  // if we use ur for experiment
  geometry_msgs::Pose pose;
  pose = targetPose;
  targetPose = changePoseForUR(pose);

  //抓取插值
  std::vector<geometry_msgs::Pose> pickWayPoints;
  pickWayPoints = pickInterpolate(placePose, targetPose);

  //前往抓取点
  moveit_msgs::RobotTrajectory trajectory1;
  arm_group.computeCartesianPath(pickWayPoints,
                                 0.02,  // eef_step
                                 0.0,   // jump_threshold
                                 trajectory1);

  pick_plan.trajectory_ = trajectory1;
  arm_group.execute(pick_plan);

  double tPlan1 = arm_group.getPlanningTime();
  ROS_INFO("Planning time is [%lf]s.", tPlan1);
  ROS_INFO("Go to the goal and prepare for picking .");

	sendGripperMsg(155);//close

	ros::Duration(0.5).sleep();

  //放置插值
  std::vector<geometry_msgs::Pose> placeWayPoints;
  placeWayPoints = placeInterpolate(targetPose, placePose);

  //前往放置点
  moveit_msgs::RobotTrajectory trajectory2;
  arm_group.computeCartesianPath(placeWayPoints,
                                 0.02,  // eef_step
                                 0.0,   // jump_threshold
                                 trajectory2);
  place_plan.trajectory_ = trajectory2;
  arm_group.execute(place_plan);

  double tPlan2 = arm_group.getPlanningTime();
  ROS_INFO("Planning time is [%lf]s.", tPlan2);
  ROS_INFO("Go to the goal and prepare for placing . ");

	sendGripperMsg(0);//open

  ROS_INFO("Waiting for the next goal.");
}
//抓取插值函数
std::vector<geometry_msgs::Pose> pickInterpolate(geometry_msgs::Pose startPose,geometry_msgs::Pose targetPose)
{
  //从放置位置前往抓取位置
  //插值后路径为＂----|＂形（先平移，后下落）
  std::vector<geometry_msgs::Pose> pickWayPoints;

  geometry_msgs::Pose midPose4;

  geometry_msgs::Point startPoint;
  geometry_msgs::Point targetPoint;
  geometry_msgs::Point midPoint;

  startPoint = startPose.position;
  targetPoint = targetPose.position;

  // midPose4
  midPoint.x = targetPoint.x;
  midPoint.y = targetPoint.y;
  midPoint.z = startPoint.z;

  midPose4.position = midPoint;
  midPose4.orientation = targetPose.orientation;

  pickWayPoints.push_back(midPose4);

  // Give targetPose
  pickWayPoints.push_back(targetPose);

  return pickWayPoints;
}

//放置插值函数
std::vector<geometry_msgs::Pose> placeInterpolate(geometry_msgs::Pose startPose,geometry_msgs::Pose targetPose)
{
  //从放置位置前往抓取位置
  //插值后路径为＂|----＂形（先抬升，后平移）
  std::vector<geometry_msgs::Pose> placeWayPoints;
  geometry_msgs::Pose midPose1;

  geometry_msgs::Point startPoint;
  geometry_msgs::Point targetPoint;
  geometry_msgs::Point midPoint;

  startPoint = startPose.position;
  targetPoint = targetPose.position;

  // midPose1
  midPoint.x = startPoint.x;
  midPoint.y = startPoint.y;
  midPoint.z = targetPoint.z;

  midPose1.position = midPoint;
  midPose1.orientation = targetPose.orientation;

  placeWayPoints.push_back(midPose1);

  // Give targetPose
  placeWayPoints.push_back(targetPose);

  return placeWayPoints;
}

void setPlacePose()
{
	placePose.position.x = -0.56;  //-0.56;
  placePose.position.y = -0.52;   //-0.52;
  placePose.position.z = 0.3;   //0.3;
  placePose.orientation.x = 1;
  placePose.orientation.y = 0;
  placePose.orientation.z = 0;
  placePose.orientation.w = 0;

  geometry_msgs::Pose pose;
  pose = placePose;
  placePose = changePoseForUR(pose);

}

//前往放置位置
void goPlacePose(geometry_msgs::Pose placePose)
{
  moveit::planning_interface::MoveGroupInterface arm_group("manipulator");	//manipulator
  std::vector< double > jointValues;
  jointValues.push_back(0.6053);
  jointValues.push_back(-0.8751);
  jointValues.push_back(0.9604);
  jointValues.push_back(-1.6561);
  jointValues.push_back(-1.5708);
  jointValues.push_back(2.1761);
  arm_group.setJointValueTarget(jointValues);

  arm_group.move();
}

// 把位姿态转换一下，给UR使用
geometry_msgs::Pose changePoseForUR(geometry_msgs::Pose pose)
{
  double x_init,y_init,z_init,w_init;
  double x_mid,y_mid,z_mid,w_mid;
  double x_trans,y_trans,z_trans,w_trans;
  double x_x180,y_x180,z_x180,w_x180;
  double x_y270,y_y270,z_y270,w_y270;

  pose.position.x = -pose.position.x;
  pose.position.y = -pose.position.y;
  pose.position.z = pose.position.z;

  x_init = pose.orientation.x;
  y_init = pose.orientation.y;
  z_init = pose.orientation.z;
  w_init = pose.orientation.w;

  Eigen::Quaterniond q_init(pose.orientation.w,pose.orientation.x,pose.orientation.y,pose.orientation.z);
  Eigen::Quaterniond q_x180(0,1,0,0);
  Eigen::Quaterniond q_y270(0.707,0,-0.707,0);
  Eigen::Quaterniond q_z270(0.707,0,0,-0.707);
  Eigen::Quaterniond q_trans=q_z270*q_x180*q_y270*q_init;

  x_x180 = 1;
  y_x180 = 0;
  z_x180 = 0;
  w_x180 = 0;


  x_y270 = 0;
  y_y270 = -0.707;
  z_y270 = 0;
  w_y270 = 0.707;


  // init and y270
  w_mid = z_init*z_y270 - x_init*x_y270 - y_init*y_y270 - z_init*z_y270;
  x_mid = w_init*x_y270 + x_init*w_y270 + z_init*y_y270 - y_init*z_y270;
  y_mid = w_init*y_y270 + y_init*w_y270 + x_init*z_y270 - z_init*x_y270;
  z_mid = w_init*z_y270 + z_init*w_y270 + y_init*x_y270 - x_init*y_y270;

  // mid and x180
  w_trans = z_mid*z_x180 - x_mid*x_x180 - y_mid*y_x180 - z_mid*z_x180;
  x_trans = w_mid*x_x180 + x_mid*w_x180 + z_mid*y_x180 - y_mid*z_x180;
  y_trans = w_mid*y_x180 + y_mid*w_x180 + x_mid*z_x180 - z_mid*x_x180;
  z_trans = w_mid*z_x180 + z_mid*w_x180 + y_mid*x_x180 - x_mid*y_x180;

  pose.orientation.x = q_trans.x();
  pose.orientation.y = q_trans.y();
  pose.orientation.z = q_trans.z();
  pose.orientation.w = q_trans.w();

  return pose;
}

int getTransBase2hand(const tf2_ros::Buffer& tfBuffer_, Eigen::Matrix3d& trans_r, Eigen::Vector3d& trans_t)
{
  geometry_msgs::TransformStamped transformStamped;
  try
  {
    transformStamped = tfBuffer_.lookupTransform( base_frame, tool_frame, ros::Time(0),ros::Duration(0.5));
  }
  catch (tf2::TransformException &ex)
  {
    ROS_WARN("%s",ex.what());
    return -1;
  }

  Quaterniond rotQ(transformStamped.transform.rotation.w,
                  transformStamped.transform.rotation.x,
                  transformStamped.transform.rotation.y,
                  transformStamped.transform.rotation.z);

  trans_r=rotQ.matrix();

  trans_t<<transformStamped.transform.translation.x,
            transformStamped.transform.translation.y,
            transformStamped.transform.translation.z;

//  //检查结果
//  Vector3d rotRPY=trans_r.eulerAngles(2,1,0); //ZYX欧拉角

//  //检查RPY是否与示教器上的RPY一致
//  cout<<"translation && RPY"<<endl
//     <<transformStamped.transform.translation.x<<endl
//     <<transformStamped.transform.translation.y<<endl
//     <<transformStamped.transform.translation.z<<endl
//     <<rotRPY.z()<<endl
//     <<rotRPY.y()<<endl
//     <<rotRPY.x()<<endl;

  return 0;

}
