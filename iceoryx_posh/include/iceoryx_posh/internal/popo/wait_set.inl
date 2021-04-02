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

#ifndef IOX_POSH_POPO_WAIT_SET_INL
#define IOX_POSH_POPO_WAIT_SET_INL

namespace iox
{
namespace popo
{
template <uint64_t Capacity>
inline WaitSet<Capacity>::WaitSet() noexcept
    : WaitSet(*runtime::PoshRuntime::getInstance().getMiddlewareConditionVariable())
{
}

template <uint64_t Capacity>
inline WaitSet<Capacity>::WaitSet(ConditionVariableData& condVarData) noexcept
    : m_conditionVariableDataPtr(&condVarData)
    , m_conditionListener(condVarData)
{
    for (uint64_t i = 0U; i < Capacity; ++i)
    {
        m_indexRepository.push(i);
    }
}

template <uint64_t Capacity>
inline WaitSet<Capacity>::~WaitSet() noexcept
{
    removeAllTriggers();
    m_conditionVariableDataPtr->m_toBeDestroyed.store(true, std::memory_order_relaxed);
}

template <uint64_t Capacity>
template <typename T>
inline cxx::expected<uint64_t, WaitSetError>
WaitSet<Capacity>::attachEventImpl(T& eventOrigin,
                                   const WaitSetHasTriggeredCallback& hasTriggeredCallback,
                                   const uint64_t eventId,
                                   const EventInfo::Callback<T>& eventCallback) noexcept
{
    if (!hasTriggeredCallback)
    {
        return cxx::error<WaitSetError>(WaitSetError::PROVIDED_HAS_TRIGGERED_CALLBACK_IS_UNSET);
    }

    for (auto& currentTrigger : m_triggerArray)
    {
        if (currentTrigger && currentTrigger->isLogicalEqualTo(&eventOrigin, hasTriggeredCallback))
        {
            return cxx::error<WaitSetError>(WaitSetError::EVENT_ALREADY_ATTACHED);
        }
    }

    cxx::MethodCallback<void, uint64_t> invalidationCallback = EventAttorney::getInvalidateTriggerMethod(eventOrigin);
    auto index = m_indexRepository.pop();
    if (!index)
    {
        return cxx::error<WaitSetError>(WaitSetError::WAIT_SET_FULL);
    }


    m_triggerArray[*index].emplace(
        &eventOrigin, hasTriggeredCallback, invalidationCallback, eventId, eventCallback, *index);

    return cxx::success<uint64_t>(*index);
}

template <uint64_t Capacity>
template <typename T, typename EventType, typename>
inline cxx::expected<WaitSetError> WaitSet<Capacity>::attachEvent(T& eventOrigin,
                                                                  const EventType eventType,
                                                                  const uint64_t eventId,
                                                                  const EventInfo::Callback<T>& eventCallback) noexcept
{
    auto hasTriggeredCallback = EventAttorney::getHasTriggeredCallbackForEvent(eventOrigin, eventType);

    return attachEventImpl(eventOrigin, hasTriggeredCallback, eventId, eventCallback).and_then([&](auto& uniqueId) {
        EventAttorney::enableEvent(
            eventOrigin,
            TriggerHandle(*m_conditionVariableDataPtr, {*this, &WaitSet::removeTrigger}, uniqueId),
            eventType);
    });
}

template <uint64_t Capacity>
template <typename T, typename EventType, typename>
inline cxx::expected<WaitSetError> WaitSet<Capacity>::attachEvent(T& eventOrigin,
                                                                  const EventType eventType,
                                                                  const EventInfo::Callback<T>& eventCallback) noexcept
{
    return attachEvent(eventOrigin, eventType, EventInfo::INVALID_ID, eventCallback);
}

template <uint64_t Capacity>
template <typename T>
inline cxx::expected<WaitSetError> WaitSet<Capacity>::attachEvent(T& eventOrigin,
                                                                  const uint64_t eventId,
                                                                  const EventInfo::Callback<T>& eventCallback) noexcept
{
    auto hasTriggeredCallback = EventAttorney::getHasTriggeredCallbackForEvent(eventOrigin);

    return attachEventImpl(eventOrigin, hasTriggeredCallback, eventId, eventCallback).and_then([&](auto& uniqueId) {
        EventAttorney::enableEvent(
            eventOrigin, TriggerHandle(*m_conditionVariableDataPtr, {*this, &WaitSet::removeTrigger}, uniqueId));
    });
}

template <uint64_t Capacity>
template <typename T>
cxx::expected<WaitSetError> WaitSet<Capacity>::attachEvent(T& eventOrigin,
                                                           const EventInfo::Callback<T>& eventCallback) noexcept
{
    return attachEvent(eventOrigin, EventInfo::INVALID_ID, eventCallback);
}

template <uint64_t Capacity>
template <typename T, typename... Targs>
inline void WaitSet<Capacity>::detachEvent(T& eventOrigin, const Targs&... args) noexcept
{
    EventAttorney::disableEvent(eventOrigin, args...);
}

template <uint64_t Capacity>
inline void WaitSet<Capacity>::removeTrigger(const uint64_t uniqueTriggerId) noexcept
{
    for (auto& trigger : m_triggerArray)
    {
        if (trigger.has_value() && trigger->getUniqueId() == uniqueTriggerId)
        {
            trigger->invalidate();
            trigger.reset();
            cxx::Ensures(m_indexRepository.push(uniqueTriggerId));
            return;
        }
    }
}

template <uint64_t Capacity>
inline void WaitSet<Capacity>::removeAllTriggers() noexcept
{
    for (auto& trigger : m_triggerArray)
    {
        trigger.reset();
    }
}

template <uint64_t Capacity>
inline typename WaitSet<Capacity>::EventInfoVector WaitSet<Capacity>::timedWait(const units::Duration timeout) noexcept
{
    return waitAndReturnTriggeredTriggers([this, timeout] { return this->m_conditionListener.timedWait(timeout); });
}

template <uint64_t Capacity>
inline typename WaitSet<Capacity>::EventInfoVector WaitSet<Capacity>::wait() noexcept
{
    return waitAndReturnTriggeredTriggers([this] { return this->m_conditionListener.wait(); });
}

template <uint64_t Capacity>
inline typename WaitSet<Capacity>::EventInfoVector WaitSet<Capacity>::createVectorWithTriggeredTriggers() noexcept
{
    EventInfoVector triggers;
    if (!m_activeNotifications.empty())
    {
        for (uint64_t i = m_activeNotifications.size() - 1U;; --i)
        {
            auto index = m_activeNotifications[i];
            auto& trigger = m_triggerArray[index];

            if (trigger && trigger->hasTriggered())
            {
                cxx::Expects(triggers.push_back(&m_triggerArray[index]->getEventInfo()));
            }
            else
            {
                m_activeNotifications.erase(m_activeNotifications.begin() + i);
            }

            if (i == 0U)
            {
                break;
            }
        }
    }

    return triggers;
}

template <uint64_t Capacity>
inline void WaitSet<Capacity>::acquireNotifications(const WaitFunction& wait) noexcept
{
    auto notificationVector = wait();
    if (m_activeNotifications.empty())
    {
        m_activeNotifications = notificationVector;
    }
    else if (!notificationVector.empty())
    {
        m_activeNotifications = algorithm::uniqueMergeSortedContainers(notificationVector, m_activeNotifications);
    }
}

template <uint64_t Capacity>
inline typename WaitSet<Capacity>::EventInfoVector
WaitSet<Capacity>::waitAndReturnTriggeredTriggers(const WaitFunction& wait) noexcept
{
    if (m_conditionListener.wasNotified())
    {
        this->acquireNotifications(wait);
    }

    EventInfoVector triggers = createVectorWithTriggeredTriggers();

    if (!triggers.empty())
    {
        return triggers;
    }

    acquireNotifications(wait);
    return createVectorWithTriggeredTriggers();
}

template <uint64_t Capacity>
inline uint64_t WaitSet<Capacity>::size() const noexcept
{
    return Capacity - m_indexRepository.size();
}

template <uint64_t Capacity>
inline constexpr uint64_t WaitSet<Capacity>::capacity() noexcept
{
    return Capacity;
}


} // namespace popo
} // namespace iox

#endif
