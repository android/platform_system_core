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

#include "sysdeps.h"

#include <dns_sd.h>
#include <endian.h>
#include <mutex>
#include <unistd.h>

#include <android-base/logging.h>

#include "cutils/properties.h"

#define MDNS_SERVICE_NAME "mdnsd"
#define MDNS_SERVICE_STATUS "init.svc.mdnsd"

static std::mutex& mdns_lock = *new std::mutex();
static int port;
static DNSServiceRef mdns_ref;
static bool mdns_registered = false;

static void start_mdns() {
    char value[PROPERTY_VALUE_MAX];
    property_get(MDNS_SERVICE_STATUS, value, "");
    if (strcmp("running", value) == 0) {
        return;
    }

    property_set("ctl.start", MDNS_SERVICE_NAME);

    for (int sleeps = 0; sleeps < 5; ++sleeps) {
        property_get(MDNS_SERVICE_STATUS, value, "");

        if (strcmp("running", value) == 0) {
            return;
        }

        sleep(1);
    }

    LOG(ERROR) << "Could not start mdnsd.";
}

static void mdns_callback(DNSServiceRef /*ref*/,
                          DNSServiceFlags /*flags*/,
                          DNSServiceErrorType errorCode,
                          const char* /*name*/,
                          const char* /*regtype*/,
                          const char* /*domain*/,
                          void* /*context*/) {
    if (errorCode != kDNSServiceErr_NoError) {
        LOG(ERROR) << "Encountered mDNS registration error ("
            << errorCode << ").";
    }
}

static void setup_mdns_thread(void* /* unused */) {
    start_mdns();
    std::lock_guard<std::mutex> lock(mdns_lock);

    char hostname[PROPERTY_VALUE_MAX + 4] = "adb-";

    property_get("ro.serialno", hostname + 4, "unidentified");

    auto error = DNSServiceRegister(&mdns_ref, 0, 0, hostname, "_adb._tcp",
                                    nullptr, nullptr, htobe16((uint16_t)port),
                                    0, nullptr, mdns_callback, nullptr);

    if (error != kDNSServiceErr_NoError) {
        LOG(ERROR) << "Could not register mDNS service (" << error << ").";
        mdns_registered = false;
    }

    mdns_registered = true;
}

static void teardown_mdns() {
    std::lock_guard<std::mutex> lock(mdns_lock);

    if (mdns_registered) {
        DNSServiceRefDeallocate(mdns_ref);
    }
}

void setup_mdns(int port_in) {
    port = port_in;
    adb_thread_create(setup_mdns_thread, nullptr, nullptr);

    // TODO: Make this more robust against a hard kill.
    atexit(teardown_mdns);
}
