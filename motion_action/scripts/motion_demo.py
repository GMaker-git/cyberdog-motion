#!/usr/bin/env python3
# Copyright (c) 2021 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import motion_units
import time
loop = 1
count = 0
while count < loop:
  # motion_units.standup()
  # motion_units.roll(3.14*2/24)
  # time.sleep(2)
  # motion_units.roll(-3.14*2/24)
  # motion_units.pitch(3.14*2/24)
  # motion_units.pitch(-3.14*2/24)
  motion_units.yaw(3.14*2/6)
  # motion_units.yaw(-3.14*2/6)
  # motion_units.walk(0.1)
  # motion_units.walk(-0.1)
  # motion_units.getdown()
  count = count + 1



