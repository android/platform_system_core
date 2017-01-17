/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/stringprintf.h>

#include "Elf.h"
#include "Maps.h"
#include "Memory.h"

Memory* MapInfo::CreateMemory(pid_t pid) {
  std::unique_ptr<MemoryFileAtOffset> file_memory(new MemoryFileAtOffset);
  if (file_memory->Init(name, offset)) {
    return file_memory.release();
  }
  return new MemoryByPidRange(pid, start, end);
}

MapInfo* Maps::Find(uint64_t pc) {
  if (maps_.empty()) {
    return nullptr;
  }
  size_t first = 0;
  size_t last = maps_.size() - 1;
  while (first <= last) {
    size_t index = (first + last) / 2;
    MapInfo* cur = &maps_[index];
    if (pc >= cur->start && pc < cur->end) {
      return cur;
    } else if (pc < cur->start) {
      if (index == 0) {
        break;
      }
      last = index - 1;
    } else {
      first = index + 1;
    }
  }
  return nullptr;
}

bool Maps::ParseLine(const char* line, MapInfo* map_info) {
  char permissions[5];
  int name_pos;
  // Linux /proc/<pid>/maps lines:
  // 6f000000-6f01e000 rwxp 00000000 00:0c 16389419   /system/lib/libcomposer.so
  if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4s %" SCNx64 " %*x:%*x %*d %n",
             &map_info->start, &map_info->end, permissions, &map_info->offset, &name_pos) != 4) {
    return false;
  }
  map_info->flags = PROT_NONE;
  if (permissions[0] == 'r') {
    map_info->flags |= PROT_READ;
  }
  if (permissions[1] == 'w') {
    map_info->flags |= PROT_WRITE;
  }
  if (permissions[2] == 'x') {
    map_info->flags |= PROT_EXEC;
  }

  map_info->name = &line[name_pos];
  size_t length = map_info->name.length() - 1;
  if (map_info->name[length] == '\n') {
    map_info->name.erase(length);
  }
  return true;
}

bool Maps::Parse() {
  std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(GetMapsFile().c_str(), "re"), fclose);
  if (fp.get() == nullptr) {
    return false;
  }

  std::vector<char> line(8192);
  while (fgets(line.data(), line.size(), fp.get())) {
    MapInfo map_info;
    if (!ParseLine(line.data(), &map_info)) {
      return false;
    }

    maps_.push_back(map_info);
  }

  return true;
}

Maps::~Maps() {
  ClearCache();
}

void Maps::ClearCache() {
  for (auto& map : maps_) {
    delete map.elf;
    map.elf = nullptr;
  }
}

bool MapsBuffer::Parse() {
  const char* start_of_line = buffer_;
  do {
    std::string line;
    const char* end_of_line = strchr(start_of_line, '\n');
    if (end_of_line == nullptr) {
      line = start_of_line;
    } else {
      end_of_line++;
      line = std::string(start_of_line, end_of_line - start_of_line);
    }

    MapInfo map_info;
    if (!ParseLine(line.c_str(), &map_info)) {
      return false;
    }
    maps_.push_back(map_info);

    start_of_line = end_of_line;
  } while (start_of_line != nullptr && *start_of_line != '\0');
  return true;
}

const std::string MapsRemote::GetMapsFile() {
  return "/proc/" + std::to_string(pid_) + "/maps";
}

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/unique_fd.h>

bool MapsOffline::Parse() {
  // Format of maps information:
  //   <uint64_t> StartOffset
  //   <uint64_t> EndOffset
  //   <uint64_t> offset
  //   <uint16_t> flags
  //   <uint16_t> MapNameLength
  //   <VariableLengthValue> MapName
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(file_.c_str(), O_RDONLY)));
  if (fd == -1) {
    return false;
  }

  std::vector<char> name;
  while (true) {
    MapInfo map_info;
    ssize_t bytes = TEMP_FAILURE_RETRY(read(fd, &map_info.start, sizeof(map_info.start)));
    if (bytes == 0) {
      break;
    }
    if (bytes == -1 || bytes != sizeof(map_info.start)) {
      return false;
    }
    bytes = TEMP_FAILURE_RETRY(read(fd, &map_info.end, sizeof(map_info.end)));
    if (bytes == -1 || bytes != sizeof(map_info.end)) {
      return false;
    }
    bytes = TEMP_FAILURE_RETRY(read(fd, &map_info.offset, sizeof(map_info.offset)));
    if (bytes == -1 || bytes != sizeof(map_info.offset)) {
      return false;
    }
    bytes = TEMP_FAILURE_RETRY(read(fd, &map_info.flags, sizeof(map_info.flags)));
    if (bytes == -1 || bytes != sizeof(map_info.flags)) {
      return false;
    }
    uint16_t len;
    bytes = TEMP_FAILURE_RETRY(read(fd, &len, sizeof(len)));
    if (bytes == -1 || bytes != sizeof(len)) {
      return false;
    }
    if (len > 0) {
      name.resize(len);
      bytes = TEMP_FAILURE_RETRY(read(fd, name.data(), len));
      if (bytes == -1 || bytes != len) {
        return false;
      }
      map_info.name = std::string(name.data(), len);
    }
    maps_.push_back(map_info);
  }
  return true;
}
