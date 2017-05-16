/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _INIT_COLDBOOT_H
#define _INIT_COLDBOOT_H

#include <dirent.h>

#include <functional>

#include "uevent.h"
#include "uevent_handler.h"
#include "uevent_listener.h"

extern const char* kColdBootPaths[3];

// Returns true if all necessary devices have been found and thus coldboot can stop.
using ColdBootCallback = std::function<bool()>;

bool ColdBootPath(const std::string& path, ColdBootCallback callback);
void ColdBoot(ColdBootCallback callback);

#endif
