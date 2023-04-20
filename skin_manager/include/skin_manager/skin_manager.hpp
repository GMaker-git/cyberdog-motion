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
#ifndef SKIN_MANAGER__SKIN_MANAGER_HPP_
#define SKIN_MANAGER__SKIN_MANAGER_HPP_
#include <unistd.h>
#include <string>
#include <thread>
#include <memory>
#include <map>
#include <vector>
#include <unordered_map>
#include "rclcpp/rclcpp.hpp"
#include "protocol/srv/elec_skin.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "elec_skin/elec_skin_base.hpp"
namespace cyberdog
{
namespace motion
{
class SkinManagerNode : public rclcpp::Node
{
public:
  explicit SkinManagerNode(std::string name);

  void ShowMoveElecSkin(std::vector<float> & msg)
  {
    if (!enable_) {
      return;
    }
    if (!align_contact_) {
      return;
    }
    static auto last_contact = std::vector<uint8_t>(4, 0);
    auto contact = std::vector<uint8_t>(4, 0);
    for (uint8_t i = 0; i < 4; ++i) {
      contact[i] = msg[i] > 0 ? 1 : 0;
      if (contact[i] == last_contact[i]) {
        continue;
      }
      last_contact[i] = contact[i];
      if (contact[i] == 1) {
        WARN("Leg %d liftdown", i);
        for (auto p : leg_map[i]) {
          // INFO("%d, %d, %d, %d", p, change_dir_.front(), start_dir_, gradual_duration_);
          elec_skin_->PositionContril(
            p,
            // change_dir_.front(),
            liftdown_color_,
            start_dir_,
            gradual_duration_);
        }
      } else {
        WARN("Leg %d liftup", i);
        for (auto p : leg_map[i]) {
          // INFO("%d, %d, %d, %d", p, change_dir_.back(), start_dir_, gradual_duration_);
          elec_skin_->PositionContril(
            p,
            liftup_color_,
            // change_dir_.back(),
            start_dir_,
            gradual_duration_);
        }
      }
    }
  }

  void ShowBlackElecSkin(int32_t & duration)
  {
    // align_contact_ = false;
    // grandual_duration_ = request->wave_cycle_time;
    // INFO("ShowBlackElecSkin");
    for (uint8_t i = 0; i < 4; ++i) {
      for (auto p : leg_map[i]) {
        // INFO("%d, %d, %d, %d", p, change_dir_.back(), start_dir_, duration);
        elec_skin_->PositionContril(
          p,
          change_dir_.back(),
          start_dir_,
          duration);
      }
    }
  }

  void ShowWhiteElecSkin(int32_t & duration)
  {
    // align_contact_ = false;
    // grandual_duration_ = request->wave_cycle_time;
    // INFO("ShowWhiteElecSkin");
    for (uint8_t i = 0; i < 4; i++) {
      for (auto p : leg_map[i]) {
        INFO("%d, %d, %d, %d", p, change_dir_.front(), start_dir_, duration);
        elec_skin_->PositionContril(
          p,
          change_dir_.front(),
          start_dir_,
          duration);
      }
    }
  }

  void ShowFrontElecSkin(int32_t & duration)
  {
    elec_skin_->ModelControl(ModelSwitch::MS_WAVEF, duration);
  }

  void ShowBackElecSkin(int32_t & duration)
  {
    elec_skin_->ModelControl(ModelSwitch::MS_WAVEB, duration);
  }

  void ShowFlashElecSkin(int32_t & duration)
  {
    elec_skin_->ModelControl(ModelSwitch::MS_FLASH, duration);
  }

  void ShowRandomElecSkin(int32_t & duration)
  {
    elec_skin_->ModelControl(ModelSwitch::MS_RANDOM, duration);
  }

  void SetAlignContact(bool align_contact)
  {
    align_contact_ = align_contact;
  }

  void ShowDefaultSkin()
  {
    INFO("ShowDefaultSkin");
    elec_skin_->ModelControl(ModelSwitch::MS_WAVEF, defaul_duration);
  }

private:
  void StartSkinCallback(
    const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
    std::shared_ptr<std_srvs::srv::SetBool::Response> response);
  void SetModeCallback(
    const std::shared_ptr<protocol::srv::ElecSkin::Request> request,
    const std::shared_ptr<protocol::srv::ElecSkin::Response> response);

  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr launch_service_{nullptr};
  rclcpp::Service<protocol::srv::ElecSkin>::SharedPtr skin_mode_service_{nullptr};
  std::shared_ptr<ElecSkinBase> elec_skin_{nullptr};
  rclcpp::executors::MultiThreadedExecutor::SharedPtr executor_;
  std::unordered_map<uint8_t, std::vector<PositionSkin>> leg_map;
  std::vector<PositionColorChangeDirection> change_dir_;
  PositionColorStartDirection start_dir_;
  PositionColorChangeDirection liftdown_color_;
  PositionColorChangeDirection liftup_color_;
  int32_t gradual_duration_{0};
  int32_t defaul_duration{0};
  int8_t default_color{0};
  int8_t start_direction{0};
  // int32_t stand_gradual_duration_{0};
  // int32_t twink_gradual_duration_{0};
  // int32_t random_gradual_duration_{0};
  bool enable_{false};
  bool align_contact_{false};
  // bool move_skin_{false};
};
}  //  namespace motion
}  //  namespace cyberdog

#endif  // SKIN_MANAGER__SKIN_MANAGER_HPP_
