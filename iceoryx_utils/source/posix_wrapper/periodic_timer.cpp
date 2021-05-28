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

#include "iceoryx_utils/posix_wrapper/periodic_timer.hpp"

using namespace iox::units::duration_literals;

namespace iox
{
namespace posix
{
PeriodicTimer::PeriodicTimer(const iox::units::Duration interval) noexcept
    : m_interval(interval)
{
    auto timerSemaphore = posix::Semaphore::create(posix::CreateUnnamedSharedMemorySemaphore, 0U);
    cxx::Ensures(!timerSemaphore.has_error() && "Could not create Semaphore for PeriodicTimer!");
    m_waitSemaphore = std::move(timerSemaphore.value());
    start();
}

void PeriodicTimer::start() noexcept
{
    stop();
    auto waitResult = m_waitSemaphore.timedWait(m_interval, true);
    cxx::Ensures(!waitResult.has_error());
    m_timeForNextActivation = now().value() + m_interval;
}

void PeriodicTimer::start(const iox::units::Duration interval) noexcept
{
    m_interval = interval;
    start();
}

void PeriodicTimer::stop() noexcept
{
    if (*(m_waitSemaphore.getValue()) == static_cast<int>(posix::SemaphoreWaitState::TIMEOUT))
    {
        auto stopResult = m_waitSemaphore.post();
        cxx::Ensures(!stopResult.has_error());
    }
}

cxx::expected<units::Duration, TimerErrorCause> PeriodicTimer::now() noexcept
{
    struct timespec ts;
    auto result =
        cxx::makeSmartC(clock_gettime, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, CLOCK_REALTIME, &ts);

    if (result.hasErrors())
    {
        return createErrorCodeFromErrNo(result.getErrNum());
    }

    return cxx::success<units::Duration>(ts);
}

cxx::expected<iox::posix::WaitResult, TimerErrorCause> PeriodicTimer::wait(TimerCatchupPolicy policy) noexcept
{
    // To check if the TIMER is active (if the sempahore is acquired)
    if (*(m_waitSemaphore.getValue()) == static_cast<int>(posix::SemaphoreWaitState::TIMEOUT))
    {
        if (now().value() > m_timeForNextActivation)
        {
            if (policy == TimerCatchupPolicy::IMMEDIATE_TICK)
            {
                m_timeForNextActivation = now().value();
                m_waitResult.state = iox::posix::TimerState::TICK;
                return cxx::success<iox::posix::WaitResult>(m_waitResult);
            }
            else if (policy == TimerCatchupPolicy::SKIP_TO_NEXT_TICK)
            {
                auto delay = now().value() - m_timeForNextActivation; // calculate the time delay
                if (delay > m_interval)
                {
                    auto totalSlotToBeSkipped =
                        delay.toMilliseconds() / m_interval.toMilliseconds(); // calculate the total slots missed
                    m_timeForNextActivation =
                        m_timeForNextActivation + m_interval * totalSlotToBeSkipped; // Skip to the next activation
                }
                else
                {
                    m_timeForNextActivation = m_timeForNextActivation + m_interval; // Skip to the next activation
                }
                auto timeDiff = m_timeForNextActivation - now().value(); // calculate remaining time for activation
                auto waitResult = m_waitSemaphore.timedWait(timeDiff, true);
                if (waitResult.has_error())
                {
                    return cxx::error<TimerErrorCause>(TimerErrorCause::INTERNAL_LOGIC_ERROR);
                }
                else
                {
                    m_waitResult.state = iox::posix::TimerState::TICK;
                    return cxx::success<iox::posix::WaitResult>(m_waitResult);
                }
            }
            else
            {
                auto timeDiff = now().value() - m_timeForNextActivation; // Calculate the time delay
                m_waitResult.state = iox::posix::TimerState::DELAY;
                m_waitResult.timeDelay = timeDiff;
                return cxx::success<iox::posix::WaitResult>(m_waitResult);
            }
        }
        else
        {
            auto actualWaitDuration = m_timeForNextActivation - now().value();
            auto waitResult = m_waitSemaphore.timedWait(actualWaitDuration, true);
            if (waitResult.has_error())
            {
                return cxx::error<TimerErrorCause>(TimerErrorCause::INTERNAL_LOGIC_ERROR);
            }
            else
            {
                m_timeForNextActivation = m_timeForNextActivation + m_interval;
                m_waitResult.state = iox::posix::TimerState::TICK;
                return cxx::success<iox::posix::WaitResult>(m_waitResult);
            }
        }
    }
    m_waitResult.state = iox::posix::TimerState::STOP;
    return cxx::success<iox::posix::WaitResult>(m_waitResult);
}

cxx::error<TimerErrorCause> PeriodicTimer::createErrorCodeFromErrNo(const int32_t errnum) noexcept
{
    TimerErrorCause timerErrorCause = TimerErrorCause::INTERNAL_LOGIC_ERROR;
    switch (errnum)
    {
    case EINVAL:
    {
        std::cerr << "The argument provided is invalid" << std::endl;
        timerErrorCause = TimerErrorCause::INVALID_ARGUMENTS;
        break;
    }
    case EPERM:
    {
        std::cerr << "No permissions to set the clock" << std::endl;
        timerErrorCause = TimerErrorCause::NO_PERMISSION;
        break;
    }
    case EFAULT:
    {
        std::cerr << "An invalid pointer was provided" << std::endl;
        timerErrorCause = TimerErrorCause::INVALID_POINTER;
        break;
    }
    default:
    {
        std::cerr << "Internal logic error in posix::Timer occurred" << std::endl;
        break;
    }
    }
    return cxx::error<TimerErrorCause>(timerErrorCause);
}

} // namespace posix
} // namespace iox
