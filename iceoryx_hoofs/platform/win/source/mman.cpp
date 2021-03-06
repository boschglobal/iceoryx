// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
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

#include "iceoryx_hoofs/platform/mman.hpp"
#include "iceoryx_hoofs/platform/platform_settings.hpp"
#include "iceoryx_hoofs/platform/win32_errorHandling.hpp"

#include <set>
#include <string>

static std::set<std::string> openedSharedMemorySegments;

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    DWORD desiredAccess = FILE_MAP_ALL_ACCESS;
    DWORD fileOffsetHigh = 0;
    DWORD fileOffsetLow = 0;
    DWORD numberOfBytesToMap = length;

    void* mappedObject = Win32Call(MapViewOfFile,
                                   HandleTranslator::getInstance().get(fd),
                                   desiredAccess,
                                   fileOffsetHigh,
                                   fileOffsetLow,
                                   numberOfBytesToMap)
                             .value;
    // windows only reserves memory but does not allocate it right away (see SEC_RESERVE in iox_shm_open)
    // this call actually allocates the right amount of bytes
    mappedObject = Win32Call(VirtualAlloc, mappedObject, numberOfBytesToMap, MEM_COMMIT, PAGE_READWRITE).value;

    return mappedObject;
}

int munmap(void* addr, size_t length)
{
    if (Win32Call(UnmapViewOfFile, addr).value)
    {
        return 0;
    }

    return -1;
}

int iox_shm_open(const char* name, int oflag, mode_t mode)
{
    HANDLE sharedMemoryHandle{nullptr};

    if (oflag & O_CREAT) // O_EXCL
    {
        // we do not yet support ACL and rights for data partitions in windows
        // DWORD access = (oflag & O_RDWR) ? PAGE_READWRITE : PAGE_READONLY;
        DWORD access = PAGE_READWRITE | SEC_RESERVE;
        DWORD MAXIMUM_SIZE_LOW = static_cast<DWORD>(iox::platform::win32::IOX_MAXIMUM_SUPPORTED_SHM_SIZE & 0xFFFFFFFF);
        DWORD MAXIMUM_SIZE_HIGH =
            static_cast<DWORD>((iox::platform::win32::IOX_MAXIMUM_SUPPORTED_SHM_SIZE >> 32) & 0xFFFFFFFF);

        auto result = Win32Call(CreateFileMapping,
                                static_cast<HANDLE>(INVALID_HANDLE_VALUE),
                                static_cast<LPSECURITY_ATTRIBUTES>(nullptr),
                                static_cast<DWORD>(access),
                                static_cast<DWORD>(MAXIMUM_SIZE_HIGH),
                                static_cast<DWORD>(MAXIMUM_SIZE_LOW),
                                static_cast<LPCSTR>(name));
        sharedMemoryHandle = result.value;

        if (oflag & O_EXCL && result.error == ERROR_ALREADY_EXISTS)
        {
            if (sharedMemoryHandle != nullptr)
            {
                Win32Call(CloseHandle, sharedMemoryHandle).value;
            }
            return -1;
        }
    }
    else
    {
        auto result = Win32Call(OpenFileMapping,
                                static_cast<DWORD>(FILE_MAP_ALL_ACCESS),
                                static_cast<BOOL>(false),
                                static_cast<LPCSTR>(name));

        sharedMemoryHandle = result.value;

        if (result.error != 0)
        {
            if (sharedMemoryHandle != nullptr)
            {
                Win32Call(CloseHandle, sharedMemoryHandle);
            }
            return -1;
        }
    }

    openedSharedMemorySegments.insert(name);
    return HandleTranslator::getInstance().add(sharedMemoryHandle);
}

int shm_unlink(const char* name)
{
    auto iter = openedSharedMemorySegments.find(name);
    if (iter != openedSharedMemorySegments.end())
    {
        openedSharedMemorySegments.erase(iter);
        return 0;
    }

    errno = ENOENT;
    return -1;
}
