/*
** Copyright 2018, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <processgroup/sched_policy_ctrl.h>
#define LOG_TAG "SchedPolicy"

#include <errno.h>
#include <utils.h>
#include <cgroup_map.h>
#include <unistd.h>
#include <processgroup/processgroup.h>
#include <android-base/logging.h>

/* Re-map SP_DEFAULT to the system default policy, and leave other values unchanged.
 * Call this any place a SchedPolicy is used as an input parameter.
 * Returns the possibly re-mapped policy.
 */
static inline SchedPolicy _policy(SchedPolicy p)
{
   return p == SP_DEFAULT ? SP_SYSTEM_DEFAULT : p;
}

int set_cpuset_policy(int tid, SchedPolicy policy) {
    if (tid == 0) {
        tid = GetTid();
    }
    policy = _policy(policy);

    switch (policy) {
    case SP_BACKGROUND:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_HighEnergySaving, TP_ProcessCapacityLow });
    case SP_FOREGROUND:
    case SP_AUDIO_APP:
    case SP_AUDIO_SYS:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_HighPerformance, TP_ProcessCapacityHigh });
    case SP_TOP_APP :
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_MaxPerformance, TP_ProcessCapacityMax });
    case SP_SYSTEM:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_ServiceCapacityLow });
    case SP_RESTRICTED:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_ServiceCapacityRestricted });
    default:
        break;
    }

    return 0;
}

int set_sched_policy(int tid, SchedPolicy policy) {
    if (tid == 0) {
        tid = GetTid();
    }
    policy = _policy(policy);

    switch (policy) {
    case SP_BACKGROUND:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_HighEnergySaving, TP_TimerSlackHigh });
    case SP_FOREGROUND:
    case SP_AUDIO_APP:
    case SP_AUDIO_SYS:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_HighPerformance, TP_TimerSlackNormal });
    case SP_TOP_APP:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_MaxPerformance, TP_TimerSlackNormal });
    case SP_RT_APP:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_RealtimePerformance, TP_TimerSlackNormal });
    default:
        return SetTaskProfiles(tid, std::vector<std::string>{
            TP_TimerSlackNormal });
    }

    return 0;
}

bool cpusets_enabled() {
    static bool enabled =
        (CgroupMap::GetInstance().FindController("cpuset") != nullptr);
    return enabled;
}

bool schedboost_enabled() {
    static bool enabled =
        (CgroupMap::GetInstance().FindController("schedtune") != nullptr);
    return enabled;
}

static int getCGroupSubsys(int tid, const char* subsys, std::string &subgroup)
{
    const struct cgroup_controller *controller =
        CgroupMap::GetInstance().FindController(subsys);

    if (!controller)
        return -1;

    if (CgroupMap::GetProcessGroup(controller, tid, subgroup) != 0) {
        PLOG(ERROR) << "Failed to find cgroup for tid " << tid;
        return -1;
    }
    return 0;
}

int get_sched_policy(int tid, SchedPolicy *policy)
{
    if (tid == 0) {
        tid = GetTid();
    }

    std::string group;
    if (schedboost_enabled()) {
        if (getCGroupSubsys(tid, "schedtune", group) < 0) return -1;
    }
    if (group.empty() && cpusets_enabled()) {
        if (getCGroupSubsys(tid, "cpuset", group) < 0) return -1;
    }

    // TODO: replace hardcoded directories
    if (group.empty()) {
        *policy = SP_FOREGROUND;
    } else if (group == "foreground") {
        *policy = SP_FOREGROUND;
    } else if (group == "system-background") {
        *policy = SP_SYSTEM;
    } else if (group == "background") {
        *policy = SP_BACKGROUND;
    } else if (group == "top-app") {
        *policy = SP_TOP_APP;
    } else if (group == "restricted") {
        *policy = SP_RESTRICTED;
    } else {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

const char* get_sched_policy_name(SchedPolicy policy) {
    policy = _policy(policy);
    static const char* const kSchedPolicyNames[] = {
            [SP_BACKGROUND] = "bg", [SP_FOREGROUND] = "fg", [SP_SYSTEM] = "  ",
            [SP_AUDIO_APP] = "aa",  [SP_AUDIO_SYS] = "as",  [SP_TOP_APP] = "ta",
            [SP_RT_APP] = "rt",     [SP_RESTRICTED] = "rs",
    };
    static_assert(arraysize(kSchedPolicyNames) == SP_CNT, "missing name");
    if (policy < SP_BACKGROUND || policy >= SP_CNT) {
        return "error";
    }
    return kSchedPolicyNames[policy];
}
