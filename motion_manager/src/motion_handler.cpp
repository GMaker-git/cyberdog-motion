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
#include <memory>
#include <vector>
#include "motion_manager/motion_handler.hpp"

namespace cyberdog
{
namespace motion
{

MotionHandler::MotionHandler()
{}

MotionHandler::~MotionHandler()
{}

bool MotionHandler::Init()
{
  action_ptr_ = std::make_shared<MotionAction>();
  if (!action_ptr_->Init()) {
    ERROR("Fail to initialize MotionAction");
    return false;
  }
  servo_check_click_ = std::make_shared<ServoClick>();
  servo_data_check_thread_ = std::thread(std::bind(&MotionHandler::ServoDataCheck, this));
  action_ptr_->RegisterFeedback(
    std::bind(&MotionHandler::UpdateMotionStatus, this, std::placeholders::_1));
  motion_status_ptr_.reset(new MotionStatusMsg);
  motion_status_ptr_->motor_error.resize(12);
  motion_id_map_ = action_ptr_->GetMotionIdMap();
  return true;
}

void MotionHandler::HandleServoStartFrame(const MotionServoCmdMsg::SharedPtr msg)
{
  action_ptr_->Execute(msg);
  TickServoCmd();
  SetServoNeedCheck(true);
}

void MotionHandler::HandleServoDataFrame(
  const MotionServoCmdMsg::SharedPtr msg,
  MotionServoResponseMsg & res)
{
  if (!AllowServoCmd(msg->motion_id)) {
    if (retry_ < max_retry_) {
      MotionResultSrv::Request::SharedPtr request(new MotionResultSrv::Request);
      MotionResultSrv::Response::SharedPtr response(new MotionResultSrv::Response);
      request->motion_id = MotionIDMsg::RECOVERYSTAND;
      INFO("Trying to be ready for ServoCmd");
      HandleResultCmd(request, response);
      if (!response->result) {
        retry_++;
      } else {
        retry_ = 0;
      }
    } else {
      res.result = false;
      res.code = MotionCodeMsg::MOTION_SWITCH_ERROR;
    }
    return;
  }

  action_ptr_->Execute(msg);
  TickServoCmd();
  SetServoNeedCheck(true);
}

void MotionHandler::HandleServoEndFrame(const MotionServoCmdMsg::SharedPtr msg)
{
  // action_ptr_->Execute(msg);
  (void) msg;
  PoseControlDefinitively();
  SetServoNeedCheck(false);
}

void MotionHandler::HandleServoCmd(
  const MotionServoCmdMsg::SharedPtr msg,
  MotionServoResponseMsg & res)
{
  if(GetWorkStatus() == HandlerStatus::kExecutingResultCmd){
    res.result = false;
    res.code = MotionCodeMsg::TASK_STATE_ERROR;
    return;
  }
  SetWorkStatus(HandlerStatus::kExecutingServoCmd);
  if (msg->cmd_type != MotionServoCmdMsg::SERVO_END) {
    if (!AllowServoCmd(msg->motion_id)) {
      SetServoNeedCheck(false);
      MotionResultSrv::Request::SharedPtr request(new MotionResultSrv::Request);
      MotionResultSrv::Response::SharedPtr response(new MotionResultSrv::Response);
      request->motion_id = MotionIDMsg::RECOVERYSTAND;
      INFO("Trying to be ready for ServoCmd");
      ExecuteResultCmd(request, response);
      if(!response->result) {
        SetWorkStatus(HandlerStatus::kIdle);
        return;
      }
      pre_motion_checked_ = true;
    }
    action_ptr_->Execute(msg);
    TickServoCmd();
    SetServoNeedCheck(true);
  } else {
    SetServoNeedCheck(false);
    PoseControlDefinitively();
    SetWorkStatus(HandlerStatus::kIdle);
    pre_motion_checked_ = false;
  }
}


/**
 * @brief 检测伺服指令的下发间隔是否符合要求
 *        1. 运行在死循环线程中，通过waitServoNeedCheck进行线程挂起与唤醒操作
 *
 */
void MotionHandler::ServoDataCheck()
{
  while (true) {
    WaitServoNeedCheck();
    if (!TockServoCmd()) {
      server_check_error_counter_++;
    } else {
      server_check_error_counter_ = 0;
    }
    if (server_check_error_counter_ >= kServoDataLostTimesThreshold) {
      WARN("Servo data lost time with %d times", kServoDataLostTimesThreshold);
      // StopServoResponse();
      // SetServoDataLost(); TODO(harvey): 是否通知Decision？
      SetServoNeedCheck(false);
      PoseControlDefinitively();
      // TODO(harvey): 当信号丢失的时候，是否需要将Decision中的状态复位？
      SetWorkStatus(HandlerStatus::kIdle);
      pre_motion_checked_ = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void MotionHandler::PoseControlDefinitively()
{
  MotionResultSrv::Request::SharedPtr request(new MotionResultSrv::Request);
  MotionResultSrv::Response::SharedPtr response(new MotionResultSrv::Response);
  request->motion_id = MotionIDMsg::POSECONTROL_DEFINITIVELY;
  request->pos_des = std::vector<float>{0.0, 0.0, 0.2};
  // action_ptr_->Execute(request);
  ExecuteResultCmd(request, response);
}

bool MotionHandler::CheckMotionResult()
{
  bool result = true;
  for (auto e : motion_status_ptr_->motor_error) {
    result = (e == 0 || e == kMotorNormal);
  }
  return motion_status_ptr_->ori_error == 0 &&
         // TODO(harvey): footpos_error需要等到运控组确定策略后再加进来
         //  motion_status_ptr_->footpos_error == 0 &&
         motion_status_ptr_->switch_status == MotionStatusMsg::NORMAL &&
         result;
}

bool MotionHandler::FeedbackTimeout()
{
  std::unique_lock<std::mutex> feedback_lk(feedback_mutex_);
  return feedback_cv_.wait_for(feedback_lk, std::chrono::milliseconds(kAcitonLcmReadTimeout)) ==
         std::cv_status::timeout;
}

void MotionHandler::ExecuteResultCmd(  
  const MotionResultSrv::Request::SharedPtr request,
  MotionResultSrv::Response::SharedPtr response)
{
  if (!CheckPreMotion(request->motion_id)) {
    MotionResultSrv::Request::SharedPtr req(new MotionResultSrv::Request);
    MotionResultSrv::Response::SharedPtr res(new MotionResultSrv::Response);
    req->motion_id = MotionIDMsg::RECOVERYSTAND;
    INFO("Trying to be ready for ResultCmd");
    ExecuteResultCmd(req, res);
    if (!res->result) {
      response->code = res->code;
      response->result = false;
      response->motion_id = motion_status_ptr_->motion_id;
      return;
    }
  }
  action_ptr_->Execute(request);
  if (FeedbackTimeout()) {
    response->code = MotionCodeMsg::COM_LCM_TIMEOUT;
    response->result = false;
    response->motion_id = motion_status_ptr_->motion_id;
    return;
  }
  std::unique_lock<std::mutex> check_lk(execute_mutex_);
  wait_id_ = request->motion_id;
  // TODO(harvey):
  // INFO("sws:%d", motion_status_ptr_->switch_status);
  if (motion_status_ptr_->switch_status == MotionStatusMsg::BAN_TRANS ||
    motion_status_ptr_->switch_status == MotionStatusMsg::EDAMP ||
    motion_status_ptr_->switch_status == MotionStatusMsg::ESTOP ||
    motion_status_ptr_->switch_status == MotionStatusMsg::LIFTED)
  {
    response->code = MotionCodeMsg::MOTION_SWITCH_ERROR;
    response->result = false;
    response->motion_id = motion_status_ptr_->motion_id;
    return;
  }
  if (motion_status_ptr_->switch_status == MotionStatusMsg::TRANSITIONING) {
    is_transitioning_wait_ = true;
    if (transitioning_cv_.wait_for(check_lk, std::chrono::milliseconds(kTransitioningTimeout)) ==
      std::cv_status::timeout)
    {
      response->code = MotionCodeMsg::MOTION_TRANSITION_TIMEOUT;
      response->result = false;
      response->motion_id = motion_status_ptr_->motion_id;
      WARN("Transitioning Timeout");
      return;
    }
  }
  if (is_transitioning_wait_) {
    INFO("Try to relock execute_mutex_");
    check_lk.lock();
    INFO("Relock execute_mutex_");
    }
  is_execute_wait_ = true;

  auto wait_timeout = 0;
  auto min_exec_time = motion_id_map_[request->motion_id].min_exec_time;
  // 站立、趴下、作揖、空翻、绝对力控这些动作运控内部设定了固定时间，duration必须为0
  if (min_exec_time > 0) {
    wait_timeout = min_exec_time;
  } else if (min_exec_time < 0) {  // 增量力控、增量位控、绝对位控、行走duration必须大于0
    wait_timeout = request->duration;
  } else {
  }

  if (execute_cv_.wait_for(
      check_lk,
      std::chrono::milliseconds(wait_timeout)) == std::cv_status::timeout)
  {
    response->code = MotionCodeMsg::MOTION_EXECUTE_TIMEOUT;
    response->result = false;
    response->motion_id = motion_status_ptr_->motion_id;
    return;
  }
  if (!CheckMotionResult()) {
    response->code = MotionCodeMsg::MOTION_EXECUTE_ERROR;
    response->result = false;
    response->motion_id = motion_status_ptr_->motion_id;
    return;
  }
  response->code = MotionCodeMsg::OK;
  response->result = true;
  response->motion_id = motion_status_ptr_->motion_id;
}

/**
 * @brief 执行结果指令
 *
 * @param request
 * @param response
 */
void MotionHandler::HandleResultCmd(
  const MotionResultSrv::Request::SharedPtr request,
  MotionResultSrv::Response::SharedPtr response)
{
  if(GetWorkStatus() != HandlerStatus::kIdle) {
    response->result = false;
    response->code = MotionCodeMsg::TASK_STATE_ERROR;
    return;
  }
  SetWorkStatus(HandlerStatus::kExecutingResultCmd);
  if (!isCommandValid(request)) {
    response->code = MotionCodeMsg::COMMAND_INVALID;
    response->result = false;
    response->motion_id = motion_status_ptr_->motion_id;
    SetWorkStatus(HandlerStatus::kIdle);
    return;
  }
  ExecuteResultCmd(request, response);
  SetWorkStatus(HandlerStatus::kIdle);
}

/**
 * @brief Inelegant code
 *
 * @param motion_status_ptr
 */
void MotionHandler::UpdateMotionStatus(MotionStatusMsg::SharedPtr motion_status_ptr)
{
  feedback_cv_.notify_one();
  std::unique_lock<std::mutex> lk(execute_mutex_);
  motion_status_ptr_->motion_id = motion_status_ptr->motion_id;
  motion_status_ptr_->contact = motion_status_ptr->contact;
  motion_status_ptr_->order_process_bar = motion_status_ptr->order_process_bar;
  motion_status_ptr_->switch_status = motion_status_ptr->switch_status;
  motion_status_ptr_->ori_error = motion_status_ptr->ori_error;
  motion_status_ptr_->footpos_error = motion_status_ptr->footpos_error;
  for (size_t i = 0; i < motion_status_ptr->motor_error.size(); ++i) {
    motion_status_ptr_->motor_error[i] = motion_status_ptr->motor_error[i];
  }
  if (is_transitioning_wait_ &&
    motion_status_ptr_->motion_id == wait_id_ &&
    motion_status_ptr_->switch_status == MotionStatusMsg::NORMAL)
  {
    transitioning_cv_.notify_one();
    is_transitioning_wait_ = false;
  }
  if (is_execute_wait_ &&
    motion_status_ptr_->motion_id == wait_id_ &&
    motion_status_ptr_->order_process_bar == 100)
  {
    execute_cv_.notify_one();
    is_execute_wait_ = false;
  }
}

MotionStatusMsg::SharedPtr MotionHandler::GetMotionStatus()
{
  return motion_status_ptr_;
}

bool MotionHandler::CheckPreMotion(int32_t motion_id)
{
  if (motion_id == MotionIDMsg::RECOVERYSTAND || motion_id == MotionIDMsg::ESTOP) {
    return true;
  }
  std::vector<int32_t> pre_motion = motion_id_map_.find(motion_id)->second.pre_motion;
  return std::find(
    pre_motion.begin(), pre_motion.end(),
    motion_status_ptr_->motion_id) != pre_motion.end();
}

bool MotionHandler::AllowServoCmd(int32_t motion_id)
{
  // TODO(harvey): 判断当前状态是否能够行走
  if(pre_motion_checked_) return true;
  return CheckPreMotion(motion_id);
}

bool MotionHandler::isCommandValid(const MotionResultSrv::Request::SharedPtr request)
{
  if (motion_id_map_.find(request->motion_id) == motion_id_map_.end()) {
    return false;
  }
  bool result = true;
  auto min_exec_time = motion_id_map_[request->motion_id].min_exec_time;
  if (min_exec_time > 0) {
    result = request->duration == 0;
  } else if (min_exec_time < 0) {
    result = request->duration > 0;
  } else {}
  if (!result) {
    return false;
  }
  return true;
}

}  // namespace motion
}  // namespace cyberdog
