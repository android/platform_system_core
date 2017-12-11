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

#include "property_schema.h"

#include <android-base/parseint.h>

#include "space_tokenizer.h"

using android::base::ParseInt;
using android::base::ParseUint;

namespace android {
namespace init {

template <typename T>
static bool ParseNumber(const std::string& value, T* out) {
    if constexpr (std::is_signed_v<T>) {
        return ParseInt(value, out);
    } else {
        if (value.front() == '-') {
            // Apparently ParseUint gives a valid result in this case?
            return false;
        }
        return ParseUint(value, out);
    }
}

template <typename T>
static bool CheckNumber(SpaceTokenizer& schema_tokens, const std::string& value) {
    T value_num;
    if (!ParseNumber(value, &value_num)) {
        return false;
    }
    auto min = schema_tokens.GetNext();
    auto max = schema_tokens.GetNext();
    if (min.empty() || max.empty()) {
        // min and max are optional, so we return true here if they're not present since we've
        // already checked that value is able to be parsed as an int.
        return true;
    }
    T min_num;
    T max_num;
    if (!ParseNumber(min, &min_num) || !ParseNumber(max, &max_num)) {
        return false;
    }
    return value_num >= min_num && value_num <= max_num;
}

bool CheckSchema(const std::string& schema, const std::string& value) {
    auto schema_tokens = SpaceTokenizer(schema);
    auto schema_type = schema_tokens.GetNext();

    if (schema_type == "any") {
        return true;
    }
    if (schema_type == "bool") {
        return value == "true" || value == "false" || value == "1" || value == "0";
    }
    if (schema_type == "int") {
        return CheckNumber<int64_t>(schema_tokens, value);
    }
    if (schema_type == "uint") {
        return CheckNumber<uint64_t>(schema_tokens, value);
    }
    if (schema_type == "enum") {
        auto token = schema_tokens.GetNext();
        while (!token.empty()) {
            if (token == value) {
                return true;
            }
            token = schema_tokens.GetNext();
        }
    }
    return false;
}

template <typename T>
static bool NumberSchemaValid(SpaceTokenizer& schema_tokens) {
    auto min = schema_tokens.GetNext();
    auto max = schema_tokens.GetNext();

    if (min.empty() && max.empty()) {
        return true;
    }
    if (max.empty()) {
        return false;
    }

    T min_num;
    T max_num;
    if (!ParseNumber<T>(min, &min_num) || !ParseNumber<T>(max, &max_num)) {
        return false;
    }
    return max_num >= min_num;
}

bool IsSchemaValid(const std::string& schema) {
    auto schema_tokens = SpaceTokenizer(schema);
    auto schema_type = schema_tokens.GetNext();

    // There should not be any string following 'any' or 'bool'
    if (schema_type == "any" || schema_type == "bool") {
        return schema_tokens.GetNext().empty();
    }
    if (schema_type == "int") {
        return NumberSchemaValid<int64_t>(schema_tokens);
    }
    if (schema_type == "uint") {
        return NumberSchemaValid<uint64_t>(schema_tokens);
    }
    // There must be at least one string following 'enum'
    if (schema_type == "enum") {
        return !schema_tokens.GetNext().empty();
    }
    return false;
}

}  // namespace init
}  // namespace android
