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

#include <cstdio>
#include <cstring>
#include <expat.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include "partition.h"

using std::ifstream;
using std::string;
using std::runtime_error;

#define BUF_SIZE            4096
#define FB_PARTITION_MAGIC  0x54504246 //"FBPT" (FastBoot Partition Table)

#define GPT_ATTR_SYSTEM     1
#define GPT_ATTR_BOOTABLE   (1ULL << 2)
#define GPT_ATTR_RO         (1ULL << 60)
#define GPT_ATTR_HIDDEN     (1ULL << 62)


int partition_table::current_lun = 0;

static int depth;
static int parse_error;

static void start_element(void *data, const char *element, const char **attr)
{
  static int storage;
  storage_info *info = static_cast<storage_info *>(data);

  depth++;

  // if there is already an error no point continuing
  if (parse_error)
    return;

  if (!strcmp(element, "storage")) {
    if (depth != 1 || storage) {
      parse_error = true;
      return;
    }
    storage = true;
  } else if (!strcmp(element, "volume")) {
    if (depth != 2) {
      parse_error = true;
      return;
    }
    info->add_partition_table(partition_table(attr));
  } else if (!strcmp(element, "partition")) {
    if (depth != 3) {
      parse_error = true;
      return;
    }
    partition_table& table = info->get_last_partition_table();
    switch ((int)table.get_type()) {
      case PARTITION_TYPE_NONE:
        // if type was not specified default to GPT
      case PARTITION_TYPE_GPT:
        table.add_partition(partition(attr));
        break;
      default:
        fprintf(stderr, "partition type not supported\n");
        parse_error = true;
    }
  }
}

partition::partition(const char **attr)
{
  //init elements
  size = 0;
  this->attr = 0;
  extend = 0;
  name[0] = '\0';
  type[0] = '\0';

  for (int i = 0; attr[i]; i += 2 ) {
    if (!strcmp(attr[i], "label")) {
      strncpy(name, attr[i + 1], sizeof name);
    } else if (!strcmp(attr[i], "type")) {
      strncpy(type, attr[i + 1], sizeof type);
    } else if (!strcmp(attr[i], "guid")) {
      strncpy(guid, attr[i + 1], sizeof guid);
    } else if (!strcmp(attr[i], "size_in_kb")) {
      size = std::strtoul(attr[i + 1], 0, 0) * 1024;
    } else if (!strcmp(attr[i], "bootable") &&
               !strcmp(attr[i + 1], "true")) {
      this->attr |= GPT_ATTR_BOOTABLE;
    } else if (!strcmp(attr[i], "readonly") &&
               !strcmp(attr[i + 1], "true")) {
      this->attr |= GPT_ATTR_RO;
    } else if (!strcmp(attr[i], "extend")) {
      extend = !strcmp(attr[i + 1], "true");
    }
  }

  // validate attributes
  if (name[0] == '\0') throw runtime_error("missing label attr\n");

  if (type[0] == '\0')
    throw runtime_error("missing type attr\n");
}

std::vector<uint8_t> partition::serialize() const
{
  std::vector<uint8_t> data(sizeof(*this));
  partition *tmp = reinterpret_cast<partition *>(&data[0]);

  /* send all data as little endian */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  tmp->size = __builtin_bswap64(size);
  tmp->attr = __builtin_bswap64(attr)
  tmp->extend = __builtin_bswap32(extend);
#else
  tmp->size = size;
  tmp->attr = attr;
  tmp->extend = extend;
#endif

  std::memcpy(&tmp->name, name, sizeof name);
  std::memcpy(&tmp->type, type, sizeof type);
  std::memcpy(&tmp->guid, guid, sizeof guid);

  return data;
}

void partition_table::add_partition(partition p)
{
  partitions.push_back(p);
  num++;
}

partition_table::partition_table(const char **attr)
{
  // init elements
  num = 0;
  magic = FB_PARTITION_MAGIC;
  lun = current_lun++;
  type = PARTITION_TYPE_NONE;

  for (int i = 0; attr[i]; i += 2 ) {
    if (!strcmp(attr[i], "lun")) {
      lun = std::strtoul(attr[i + 1], nullptr, 0);
      current_lun = lun + 1;
    } else if (!strcmp(attr[i], "type")) {
      if (!strcmp(attr[i + 1], "gpt")) {
        type = PARTITION_TYPE_GPT;
      } else {
        type = PARTITION_TYPE_UNKNOWN;
        throw runtime_error("partition table type no supported");
      }
    } else if (!strcmp(attr[i], "disk_guid")) {
      strncpy(disk_guid, attr[i + 1], sizeof(disk_guid));
    }
  }
}

void storage_info::add_partition_table(partition_table table)
{
  tables.push_back(table);
}

std::vector<uint8_t> partition_table::serialize() const {
  const int table_data_size = offsetof(partition_table, partitions);
  std::vector<uint8_t> data(partitions.size() * sizeof(partition) +
                            table_data_size, 0);

  partition_table *tmp = reinterpret_cast<partition_table *>(&data[0]);
  /* send all data as little endian */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  tmp->magic = __builtin_bswap32(magic);
  tmp->lun = __builtin_bswap32(lun);
  tmp->type = __builtin_bswap32(type);
  tmp->num = __builtin_bswap32(num);
#else
  tmp->magic = magic;
  tmp->lun = lun;
  tmp->type = type;
  tmp->num = num;
#endif
  std::memcpy(&tmp->disk_guid, disk_guid, sizeof disk_guid);

  /* serialize all partitions */
  partition *pi = reinterpret_cast<partition *>(&data[0] + table_data_size);
  for (auto& p : partitions)
    std::memcpy(pi++, p.serialize().data(), sizeof *pi);

  return data;
};

storage_info::storage_info(const std::string fname)
{
  XML_Parser parser = XML_ParserCreate(nullptr);
  ifstream f(fname);

  if (!f.is_open())
    throw std::runtime_error(string("cannot open file ") + fname + '\n');

  // end_element function only decrease depth, so make it an anonymous func
  XML_SetElementHandler(parser, start_element,
                        [](void *, const char *) { depth--;});
  XML_SetUserData(parser, this);

  while (!parse_error) {
    char *buf = static_cast<char *>(XML_GetBuffer(parser, BUF_SIZE));
    f.read((char *)buf, BUF_SIZE);
    XML_ParseBuffer(parser, f.gcount(), f.eof());
    if (f.eof()) break;
  }

  XML_ParserFree(parser);
  f.close();

  if (parse_error) throw runtime_error("error parsing file\n");
}
