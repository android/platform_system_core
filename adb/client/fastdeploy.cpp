/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <androidfw/ResourceTypes.h>
#include <androidfw/ZipFileRO.h>
#include <libgen.h>
#include <algorithm>

#include "commandline.h"
#include "fastdeploy.h"
#include "fastdeploycallbacks.h"
#include "file_sync_service.h"
#include "utils/String16.h"

const long kRequiredAgentVersion = 0x00000001;

const char* kDeviceAgentPath = "/data/local/tmp/";

long get_agent_version() {
    const std::string kAgentVersionCommand = "/data/local/tmp/deployagent version";

    std::vector<unsigned char> versionOutputBuffer;
    std::vector<unsigned char> versionErrorBuffer;
    int statusCode;
    DeployAgentBufferCallback cb(&versionOutputBuffer, &versionErrorBuffer, &statusCode);
    int ret = send_shell_command(kAgentVersionCommand, false, &cb);
    long version = -1;

    if (ret == 0 && statusCode == 0 && versionErrorBuffer.size() > 0) {
        version = strtol((char*)versionErrorBuffer.data(), NULL, 16);
    }

    return version;
}

// output_path is required to point to a valid output string (non-null)
static bool get_agent_component_host_path(bool use_localagent, const char* adb_path,
                                          const char* local_path, const char* sdk_path,
                                          std::string* output_path) {
#ifdef _WIN32
    if (use_localagent) {
        // Local builds are not supported on Windows
        return false;
    } else {
        const char* adb_dir = dirname(const_cast<char*>(adb_path));
        if (adb_dir == nullptr) {
            return false;
        }
        *output_path = android::base::StringPrintf("%s%s", adb_dir, sdk_path);
        std::replace(output_path->begin(), output_path->end(), '/', '\\');
        return true;
    }
    return false;
#else
    if (use_localagent) {
        const char* product_out = getenv("ANDROID_PRODUCT_OUT");
        if (product_out == nullptr) {
            return false;
        }
        *output_path = android::base::StringPrintf("%s%s", product_out, local_path);
        return true;
    } else {
        const char* adb_dir = dirname(const_cast<char*>(adb_path));
        if (adb_dir == nullptr) {
            return false;
        }
        *output_path = android::base::StringPrintf("%s%s", adb_dir, sdk_path);
        return true;
    }
    return false;
#endif
}

static bool deploy_agent(bool checkTimeStamps, bool use_localagent, const char* adb_path) {
    std::vector<const char*> srcs;

    std::string agent_jar_path;
    if (get_agent_component_host_path(use_localagent, adb_path, "/system/framework/deployagent.jar",
                                      "/deployagent.jar", &agent_jar_path)) {
        srcs.push_back(agent_jar_path.c_str());
    } else {
        return false;
    }

    std::string agent_sh_path;
    if (get_agent_component_host_path(use_localagent, adb_path, "/system/bin/deployagent.sh",
                                      "/deployagent.sh", &agent_sh_path)) {
        srcs.push_back(agent_sh_path.c_str());
    } else {
        return false;
    }

    return do_sync_push(srcs, kDeviceAgentPath, checkTimeStamps);
}

bool update_agent(FastDeploy_AgentUpdateStrategy agentUpdateStrategy, bool use_localagent,
                  const char* adb_path) {
    long agent_version = get_agent_version();
    switch (agentUpdateStrategy) {
        case FastDeploy_AgentUpdateAlways:
            if (deploy_agent(false, use_localagent, adb_path) == false) {
                return false;
            }
            break;
        case FastDeploy_AgentUpdateNewerTimeStamp:
            if (deploy_agent(true, use_localagent, adb_path) == false) {
                return false;
            }
            break;
        case FastDeploy_AgentUpdateDifferentVersion:
            if (agent_version != kRequiredAgentVersion) {
                if (agent_version < 0) {
                    printf("Could not detect agent on device, deploying\n");
                } else {
                    printf("Device agent version is (%ld), (%ld) is required, re-deploying\n",
                           agent_version, kRequiredAgentVersion);
                }
                if (deploy_agent(false, use_localagent, adb_path) == false) {
                    return false;
                }
            }
            break;
    }

    agent_version = get_agent_version();
    return (agent_version == kRequiredAgentVersion);
}

static std::string get_string_from_utf16(const char16_t* input, int input_len) {
    ssize_t utf8_length = utf16_to_utf8_length(input, input_len);
    if (utf8_length <= 0) {
        return {};
    }

    std::string utf8;
    utf8.resize(utf8_length);
    utf16_to_utf8(input, input_len, &*utf8.begin(), utf8_length + 1);
    return utf8;
}

// output is required to point to a valid output string (non-null)
static bool get_packagename_from_apk(const char* apkPath, std::string* output) {
    using namespace android;

    ZipFileRO* zipFile = ZipFileRO::open(apkPath);
    if (zipFile == nullptr) {
        return false;
    }

    ZipEntryRO entry = zipFile->findEntryByName("AndroidManifest.xml");
    if (entry == nullptr) {
        return false;
    }

    uint32_t manifest_len = 0;
    if (!zipFile->getEntryInfo(entry, NULL, &manifest_len, NULL, NULL, NULL, NULL)) {
        return false;
    }

    void* manifest_data = malloc(manifest_len);
    if (!zipFile->uncompressEntry(entry, manifest_data, manifest_len)) {
        free(manifest_data);
        return false;
    }

    ResXMLTree tree;
    status_t setto_status = tree.setTo(manifest_data, manifest_len, true);
    free(manifest_data);
    if (setto_status != NO_ERROR) {
        return false;
    }

    ResXMLParser::event_code_t code;
    while ((code = tree.next()) != ResXMLParser::BAD_DOCUMENT &&
           code != ResXMLParser::END_DOCUMENT) {
        switch (code) {
            case ResXMLParser::START_TAG: {
                size_t element_name_length;
                const char16_t* element_name = tree.getElementName(&element_name_length);
                if (element_name == nullptr) {
                    continue;
                }

                String16 element_name_string(element_name, element_name_length);
                if (element_name_string == u"manifest") {
                    for (int i = 0; i < (int)tree.getAttributeCount(); i++) {
                        size_t attribute_name_length;
                        const char16_t* attribute_name_text =
                                tree.getAttributeName(i, &attribute_name_length);
                        if (attribute_name_text == nullptr) {
                            continue;
                        }
                        String16 attribute_name_string(attribute_name_text, attribute_name_length);

                        if (attribute_name_string == u"package") {
                            size_t attribute_value_length;
                            const char16_t* attribute_value_text =
                                    tree.getAttributeStringValue(i, &attribute_value_length);
                            if (attribute_value_text == nullptr) {
                                continue;
                            }
                            *output = get_string_from_utf16(attribute_value_text,
                                                            attribute_value_length);
                            return true;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    return false;
}

int extract_metadata(const char* apkPath, FILE* outputFp) {
    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }

    const std::string kAgentExtractCommandPattern = "/data/local/tmp/deployagent.sh extract %s";
    std::string extractCommand =
            android::base::StringPrintf(kAgentExtractCommandPattern.c_str(), packageName.c_str());

    std::vector<unsigned char> extractErrorBuffer;
    int statusCode;
    DeployAgentFileCallback cb(outputFp, &extractErrorBuffer, &statusCode);
    int ret = send_shell_command(extractCommand.c_str(), false, &cb);

    if (ret == 0) {
        return cb.getBytesWritten();
    }

    return ret;
}

// output is required to point to a valid output string (non-null)
static bool patch_generator_command(bool use_localagent, const char* adb_path, std::string* output) {
    if (use_localagent) {
        const char* kGeneratorCommandPattern = "java -jar %s/framework/deploypatchgenerator.jar";
        const char* host_out = getenv("ANDROID_HOST_OUT");
        if (host_out == nullptr) {
            return false;
        }
        *output = android::base::StringPrintf(kGeneratorCommandPattern, host_out, host_out);
        return true;
    } else {
        const char* kGeneratorCommandPattern = "java -jar %s/deploypatchgenerator.jar";
        const char* adb_dir = dirname(const_cast<char*>(adb_path));
        if (adb_dir == nullptr) {
            return false;
        }
        *output = android::base::StringPrintf(kGeneratorCommandPattern, adb_dir, adb_dir);
        return true;
    }
    return false;
}

int create_patch(const char* apkPath, const char* metadataPath, const char* patchPath,
                 bool use_localagent, const char* adb_path) {
    const char* kGeneratePatchCommandPattern = "%s %s %s > %s";

    std::string patch_generator_command_string;
    if (patch_generator_command(use_localagent, adb_path, &patch_generator_command_string) == false) {
        return 1;
    }
    std::string generatePatchCommand = android::base::StringPrintf(
            kGeneratePatchCommandPattern, patch_generator_command_string.c_str(), apkPath,
            metadataPath, patchPath);
    return system(generatePatchCommand.c_str());
}

std::string get_patch_path(const char* apkPath) {
    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return "";
    }
    std::string patchDevicePath =
            android::base::StringPrintf("%s%s.patch", kDeviceAgentPath, packageName.c_str());
    return patchDevicePath;
}

int apply_patch_on_device(const char* apkPath, const char* patchPath, const char* outputPath) {
    const std::string kAgentApplyCommandPattern = "/data/local/tmp/deployagent.sh apply %s %s %s";

    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }
    std::string patchDevicePath = get_patch_path(apkPath);

    std::vector<const char*> srcs = {patchPath};
    bool push_ok = do_sync_push(srcs, patchDevicePath.c_str(), false);

    if (push_ok == false) {
        return -1;
    }

    std::vector<unsigned char> applyOutputBuffer;
    std::vector<unsigned char> applyErrorBuffer;
    int statusCode;

    std::string applyPatchCommand =
            android::base::StringPrintf(kAgentApplyCommandPattern.c_str(), packageName.c_str(),
                                        patchDevicePath.c_str(), outputPath);
    DeployAgentBufferCallback cb(&applyOutputBuffer, &applyErrorBuffer, &statusCode);
    int ret = send_shell_command(applyPatchCommand, false, &cb);

    if (applyErrorBuffer.size() > 0) {
        printf("%.*s\n", (int)applyErrorBuffer.size(), (char*)applyErrorBuffer.data());
    }

    if (ret != 0) {
        return ret;
    }

    return statusCode;
}

int install_patch(const char* apkPath, const char* patchPath, int argc, const char** argv) {
    const std::string kAgentApplyCommandPattern =
            "/data/local/tmp/deployagent.sh apply %s %s pm %s";

    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }

    // TODO: make this entirely streaming
    std::vector<const char*> srcs;
    std::string patchDevicePath =
            android::base::StringPrintf("%s%s.patch", kDeviceAgentPath, packageName.c_str());
    srcs.push_back(patchPath);
    bool push_ok = do_sync_push(srcs, patchDevicePath.c_str(), false);

    if (push_ok == false) {
        return -1;
    }

    std::vector<unsigned char> applyOutputBuffer;
    std::vector<unsigned char> applyErrorBuffer;
    int statusCode;

    std::string argsString;

    for (int i = 0; i < argc; i++) {
        argsString.append(argv[i]);
        argsString.append(" ");
    }

    std::string applyPatchCommand =
            android::base::StringPrintf(kAgentApplyCommandPattern.c_str(), packageName.c_str(),
                                        patchDevicePath.c_str(), argsString.c_str());
    DeployAgentBufferCallback cb(&applyOutputBuffer, &applyErrorBuffer, &statusCode);

    printf("%s\n", applyPatchCommand.c_str());
    int ret = send_shell_command(applyPatchCommand, false, &cb);

    if (applyErrorBuffer.size() > 0) {
        printf("%.*s\n", (int)applyErrorBuffer.size(), (char*)applyErrorBuffer.data());
    }

    if (ret != 0) {
        return ret;
    }

    return statusCode;
}
