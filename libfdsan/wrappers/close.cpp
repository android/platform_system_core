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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <async_safe/log.h>

#include "fdsan.h"
#include "fdsan_wrappers.h"

extern "C" int close(int fd) {
  return fdsan_close(fd, nullptr);
}

extern "C" int __libc_close_with_tag(int fd, void* tag) {
  return fdsan_close(fd, tag);
}
