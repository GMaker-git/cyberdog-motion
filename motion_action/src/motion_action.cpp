// Copyright (c) 2021 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "motion_action/motion_action.hpp"
#include <string>
#include <memory>
#include <map>
#include <vector>

cyberdog::motion::MotionAction::MotionAction()
: lcm_publish_channel_("robot_control_cmd"),
  lcm_subscribe_channel_("robot_control_response"),
  lcm_cmd_init_(false),
  ins_init_(false)
{}

cyberdog::motion::MotionAction::~MotionAction() {}

void cyberdog::motion::MotionAction::Execute(const MotionServoCmdMsg::SharedPtr msg)
{
  // Checkout mode global, send msg continuously
  if (!ins_init_) {
    ERROR("MotionAction has not been initialized when execute ServoCmd");
    return;
  }
  if (motion_id_map_.empty()) {
    return;
  }
  lcm_cmd_.mode = motion_id_map_.at(msg->motion_id).front();
  lcm_cmd_.gait_id = motion_id_map_.at(msg->motion_id).back();
  lcm_cmd_.contact = 0;
  lcm_cmd_.life_count++;
  lcm_cmd_.value = 0;
  lcm_cmd_.duration = 0;
  GET_VALUE(msg->step_height, lcm_cmd_.step_height, 2, "step_height");
  GET_VALUE(msg->vel_des, lcm_cmd_.vel_des, 3, "vel_des");
  GET_VALUE(msg->rpy_des, lcm_cmd_.rpy_des, 3, "rpy_des");
  GET_VALUE(msg->pos_des, lcm_cmd_.pos_des, 3, "pos_des");
  GET_VALUE(msg->ctrl_point, lcm_cmd_.ctrl_point, 3, "ctrl_point");
  GET_VALUE(msg->acc_des, lcm_cmd_.acc_des, 6, "acc_des");
  GET_VALUE(msg->foot_pose, lcm_cmd_.foot_pose, 6, "foot_pose");
  lcm_cmd_init_ = true;
}

void cyberdog::motion::MotionAction::Execute(const MotionResultSrv::Request::SharedPtr request)
{
  if (!ins_init_) {
    ERROR("MotionAction has not been initialized when execute ResultSrv");
    return;
  }
  if (motion_id_map_.empty()) {
    return;
  }
  lcm_cmd_.mode = motion_id_map_.at(request->motion_id).front();
  lcm_cmd_.gait_id = motion_id_map_.at(request->motion_id).back();
  lcm_cmd_.contact = 0;
  lcm_cmd_.life_count++;
  lcm_cmd_.value = 0;
  lcm_cmd_.duration = 0;
  GET_VALUE(request->step_height, lcm_cmd_.step_height, 2, "step_height");
  GET_VALUE(request->vel_des, lcm_cmd_.vel_des, 3, "vel_des");
  GET_VALUE(request->rpy_des, lcm_cmd_.rpy_des, 3, "rpy_des");
  GET_VALUE(request->pos_des, lcm_cmd_.pos_des, 3, "pos_des");
  GET_VALUE(request->ctrl_point, lcm_cmd_.ctrl_point, 3, "ctrl_point");
  GET_VALUE(request->acc_des, lcm_cmd_.acc_des, 6, "acc_des");
  GET_VALUE(request->foot_pose, lcm_cmd_.foot_pose, 6, "foot_pose");
  lcm_cmd_init_ = true;
}

bool cyberdog::motion::MotionAction::ParseMotionId()
{
  std::string motion_id_map_config = ament_index_cpp::get_package_share_directory("motion_action") +
    "/preset/" + "motion_id_map.toml";
  toml::value value;
  if (!cyberdog::common::CyberdogToml::ParseFile(motion_id_map_config, value)) {
    FATAL("Cannot parse %s", motion_id_map_config.c_str());
    return false;
  }
  for (auto p : value.as_table()) {
    motion_id_map_.emplace(
      int32_t(std::stoi(p.first)),
      std::vector<int8_t>{int8_t(p.second.as_array().front().as_integer()),
        int8_t(p.second.as_array().back().as_integer())});
  }
  return true;
}

bool cyberdog::motion::MotionAction::Init(
  const std::string & publish_url, const std::string & subscribe_url)
{
  lcm_publish_duration_ = 1 / static_cast<float>(LCM_PUBLISH_FREQUENCY_) * 1000;
  lcm_publish_instance_ = std::make_shared<lcm::LCM>(publish_url);
  lcm_subscribe_instance_ = std::make_shared<lcm::LCM>(subscribe_url);
  ParseMotionId();
  lcm_subscribe_instance_->subscribe(lcm_subscribe_channel_, &MotionAction::ReadLcm, this);
  control_thread_ = std::thread(&MotionAction::WriteLcm, this);
  control_thread_.detach();
  response_thread_ =
    std::thread([this]() {while (0 == this->lcm_subscribe_instance_->handle()) {}});
  response_thread_.detach();
  ins_init_ = true;
  return true;
}

bool cyberdog::motion::MotionAction::SelfCheck()
{
  return true;
}

void cyberdog::motion::MotionAction::RegisterFeedback(
  std::function<void(MotionStatusMsg::SharedPtr)> feedback)
{
  feedback_func_ = feedback;
}

void cyberdog::motion::MotionAction::ReadLcm(
  const lcm::ReceiveBuffer *, const std::string &,
  const robot_control_response_lcmt * msg)
{
  if (msg->mode != last_res_mode_ || msg->gait_id != last_res_gait_id_) {
    for (auto m = motion_id_map_.begin(); ; m++) {
      if (m == motion_id_map_.end()) {
        FATAL("Get unkown response about motion_id!");
        return;
      }
      if (m->second.front() == msg->mode && m->second.back() == msg->gait_id) {
        lcm_res_->motion_id = m->first;
        break;
      }
    }
  }
  lcm_res_->contact = msg->contact;
  lcm_res_->order_process_bar = msg->order_process_bar;
  lcm_res_->switch_status = msg->switch_status;
  lcm_res_->ori_error = msg->ori_error;
  lcm_res_->footpos_error = msg->footpos_error;
  for (uint8_t i = 0; i < 12; ++i) {
    lcm_res_->motor_error[i] = msg->motor_error[i];
  }
  if (feedback_func_) {
    feedback_func_(lcm_res_);
  }
}

void cyberdog::motion::MotionAction::WriteLcm()
{
  while (lcm_publish_instance_->good()) {
    if (lcm_cmd_init_) {
      lcm_publish_instance_->publish(lcm_publish_channel_, &lcm_cmd_);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(lcm_publish_duration_));
  }
}
