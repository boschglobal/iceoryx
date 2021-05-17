// Copyright (c) 2021 by Robert Bosch GmbH. All rights reserved.
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
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ROUDIFUZZ_HPP
#define ROUDIFUZZ_HPP

#include "iceoryx_posh/internal/roudi/roudi.hpp"

/// @brief 	RouDiFuzz is a class which inherits from iox::roudi::RouDi to make some protected methods avaialble for
/// Fuzzing. This is necessary, to directly injects messages
///			in these messages to test the robustness of the interfaces

class RouDiFuzz : iox::roudi::RouDi
{
  public:
    RouDiFuzz(iox::roudi::RouDiMemoryInterface& roudiMemoryInterface,
              iox::roudi::PortManager& portManager,
              iox::roudi::RouDi::RoudiStartupParameters = {iox::roudi::MonitoringMode::OFF,
                                                           false,
                                                           iox::roudi::RouDi::RuntimeMessagesThreadStart::IMMEDIATE,
                                                           iox::version::CompatibilityCheckLevel::OFF});

    /// @brief [in] Send a message to the processMessage method of RouDi
    /// @param [in] Message which should be sent to the processMessage method of RouDi
    void processMessageFuzz(std::string aMessage);
};
#endif /*ROUDIFUZZ_HPP*/
