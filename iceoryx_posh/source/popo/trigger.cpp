// Copyright (c) 2020, 2021 by Apex.AI Inc. All rights reserved.
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

#include "iceoryx_posh/popo/trigger.hpp"

namespace iox
{
namespace popo
{
constexpr uint64_t Trigger::INVALID_TRIGGER_ID;

Trigger::~Trigger()
{
    reset();
}

bool Trigger::hasTriggered() const noexcept
{
    return (isValid()) ? m_hasTriggeredCallback().value() : false;
}

void Trigger::reset() noexcept
{
    if (!isValid())
    {
        return;
    }

    // the constructor made sure that m_resetCallback is always set
    IOX_DISCARD_RESULT(m_resetCallback(m_uniqueId));

    invalidate();
}

const EventInfo& Trigger::getEventInfo() const noexcept
{
    return m_eventInfo;
}

void Trigger::invalidate() noexcept
{
    m_hasTriggeredCallback = cxx::ConstMethodCallback<bool>();
    m_resetCallback = cxx::MethodCallback<void, uint64_t>();
    m_uniqueId = INVALID_TRIGGER_ID;
}

Trigger::operator bool() const noexcept
{
    return isValid();
}

bool Trigger::isValid() const noexcept
{
    return static_cast<bool>(m_hasTriggeredCallback);
}

bool Trigger::isLogicalEqualTo(const void* const eventOrigin,
                               const cxx::ConstMethodCallback<bool>& hasTriggeredCallback) const noexcept
{
    return isValid() && m_eventInfo.m_eventOrigin == eventOrigin && m_hasTriggeredCallback == hasTriggeredCallback;
}

Trigger::Trigger(Trigger&& rhs) noexcept
{
    *this = std::move(rhs);
}

Trigger& Trigger::operator=(Trigger&& rhs) noexcept
{
    if (this != &rhs)
    {
        reset();

        // EventInfo
        m_eventInfo = std::move(rhs.m_eventInfo);

        // Trigger
        m_resetCallback = std::move(rhs.m_resetCallback);
        m_hasTriggeredCallback = std::move(rhs.m_hasTriggeredCallback);
        m_uniqueId = std::move(rhs.m_uniqueId);

        rhs.invalidate();
    }
    return *this;
}

uint64_t Trigger::getUniqueId() const noexcept
{
    return m_uniqueId;
}


} // namespace popo
} // namespace iox
