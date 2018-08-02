/*
 * Copyright (C) 2018 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>

#include "extensions.h"
#include "tinyxml2.h"

namespace fastboot {
namespace extension {

namespace {  // private to this file

const std::unordered_map<std::string, Configuration::CommandTest::Expect> CMD_EXPECTS = {
        {"okay", Configuration::CommandTest::OKAY},
        {"fail", Configuration::CommandTest::FAIL},
        {"data", Configuration::CommandTest::DATA},
};

bool XMLAssert(bool cond, const tinyxml2::XMLElement* elem, const char* msg) {
    if (!cond) {
        printf("%s (line %d)\n", msg, elem->GetLineNum());
    }
    return !cond;
}

const std::string XLMAttribute(const tinyxml2::XMLElement* elem, const std::string key) {
    if (!elem->Attribute(key.c_str())) {
        return "";
    }

    return elem->Attribute(key.c_str());
}

}  // namespace

bool ParseXml(const std::string& file, Configuration* config) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(file.c_str())) {
        printf("Failed to open/parse XML file '%s'\nXMLError: %s\n", file.c_str(), doc.ErrorStr());
        return false;
    }

    tinyxml2::XMLConstHandle handle(&doc);
    tinyxml2::XMLConstHandle root(handle.FirstChildElement("config"));
    // Extract getvars
    const tinyxml2::XMLElement* var =
            root.FirstChildElement("getvar").FirstChildElement("var").ToElement();
    while (var) {
        const std::string key = XLMAttribute(var, "key");
        const std::string reg = XLMAttribute(var, "assert");
        if (XMLAssert(key.size(), var, "The var key name is empty")) return false;
        // TODO, is there a way to make sure regex is valid without exceptions?
        if (XMLAssert(config->getvars.find(key) == config->getvars.end(), var,
                      "The same getvar variable name is listed twice"))
            return false;
        std::regex regex(reg, std::regex::extended);
        config->getvars[key] = std::move(regex);
        var = var->NextSiblingElement("var");
    }

    // Extract partitions
    const tinyxml2::XMLElement* part =
            root.FirstChildElement("partitions").FirstChildElement("part").ToElement();
    while (part) {
        const std::string name = XLMAttribute(part, "value");
        const std::string slots = XLMAttribute(part, "slots");
        const std::string test = XLMAttribute(part, "test");
        if (XMLAssert(name.size(), part, "The name of a partition can not be empty")) return false;
        if (XMLAssert(slots == "yes" || slots == "no", part,
                      "Slots attribute must be 'yes' or 'no'"))
            return false;
        bool allowed = test == "yes" || test == "no-writes" || test == "no";
        if (XMLAssert(allowed, part, "The test attribute must be 'yes' 'no-writes' or 'no'"))
            return false;
        if (XMLAssert(config->partitions.find(name) == config->partitions.end(), part,
                      "The same partition name is listed twice"))
            return false;
        Configuration::PartitionInfo part_info;
        part_info.test = (test == "yes")
                                 ? Configuration::PartitionInfo::YES
                                 : (test == "no-writes") ? Configuration::PartitionInfo::NO_WRITES
                                                         : Configuration::PartitionInfo::NO;
        part_info.slots = slots == "yes";
        config->partitions[name] = part_info;
        part = part->NextSiblingElement("part");
    }

    // Extract oem commands
    const tinyxml2::XMLElement* command =
            root.FirstChildElement("oem").FirstChildElement("command").ToElement();
    while (command) {
        const std::string cmd = XLMAttribute(command, "value");
        const std::string permissions = XLMAttribute(command, "permissions");
        if (XMLAssert(cmd.size(), command, "Empty command value")) return false;
        if (XMLAssert(permissions == "none" || permissions == "unlocked", command,
                      "Permissions attribute must be 'none' or 'unlocked'"))
            return false;

        // Each command has tests
        std::vector<Configuration::CommandTest> tests;
        const tinyxml2::XMLElement* test = command->FirstChildElement("test");
        while (test) {  // iterate through tests
            const std::string arg = XLMAttribute(test, "value");
            const std::string expect = XLMAttribute(test, "expect");
            const std::string reg = XLMAttribute(test, "assert");
            if (XMLAssert(arg.size(), test, "Empty test argument value")) return false;
            if (XMLAssert(CMD_EXPECTS.find(expect) != CMD_EXPECTS.end(), test,
                          "Expect attribute must be 'okay', 'fail', or 'data'"))
                return false;
            std::regex regex;
            if (expect == "okay" && reg.size()) {
                std::regex r(reg, std::regex::extended);
                regex = r;
            }
            Configuration::CommandTest cmd_test{arg, CMD_EXPECTS.at(expect), regex};
            tests.push_back(std::move(cmd_test));
            test = test->NextSiblingElement("test");
        }

        // Build the command struct
        const Configuration::OemCommand oem_cmd{permissions == "unlocked", std::move(tests)};
        config->oem[cmd] = std::move(oem_cmd);

        command = command->NextSiblingElement("command");
    }

    // Extract checksum
    const tinyxml2::XMLElement* checksum = root.FirstChildElement("oem").ToElement();
    std::string cmd(checksum && checksum->Attribute("value") ? checksum->Attribute("value") : "");
    config->checksum = cmd;

    return true;
}

}  // namespace extension
}  // namespace fastboot
