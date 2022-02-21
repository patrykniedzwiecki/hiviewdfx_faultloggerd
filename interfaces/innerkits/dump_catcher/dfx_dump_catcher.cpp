/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This files contains faultlog sdk interface functions. */

#include "dfx_dump_catcher.h"

#include <algorithm>
#include <climits>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <directory_ex.h>
#include <file_ex.h>
#include <fcntl.h>
#include <strstream>
#include <pthread.h>
#include <securec.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <string>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ucontext.h>
#include <vector>

#include <hilog/log.h>
#include "../faultloggerd_client/include/faultloggerd_client.h"
#include "dfx_dump_catcher_local_dumper.h"

static const std::string DFXDUMPCATCHER_TAG = "DfxDumpCatcher";

const std::string LOG_FILE_PATH = "/data/log/faultlog/temp";

namespace OHOS {
namespace HiviewDFX {
static pthread_mutex_t g_dumpCatcherMutex = PTHREAD_MUTEX_INITIALIZER;

DfxDumpCatcher::DfxDumpCatcher()
{
    DfxLogDebug("%s :: construct.", DFXDUMPCATCHER_TAG.c_str());
}

DfxDumpCatcher::~DfxDumpCatcher()
{
    DfxLogDebug("%s :: destructor.", DFXDUMPCATCHER_TAG.c_str());
}

void DfxDumpCatcher::FreeStackInfo()
{
    free(DfxDumpCatcherLocalDumper::g_StackInfo_);
    DfxDumpCatcherLocalDumper::g_StackInfo_ = nullptr;
    DfxDumpCatcherLocalDumper::g_CurrentPosition = 0;
}

bool DfxDumpCatcher::DoDumpLocalPidTid(const int pid, const int tid)
{
    DfxLogDebug("%s :: DoDumpLocalPidTid :: pid(%d), tid(%d).", DFXDUMPCATCHER_TAG.c_str(), pid, tid);
    if (pid <= 0 || tid <= 0) {
        DfxLogError("%s :: DoDumpLocalPidTid :: return false as param error.", DFXDUMPCATCHER_TAG.c_str());
        return false;
    }

    do {
        if (RequestSdkDump(pid, tid) == true) {
            int time = 0;
            long long msgLength;
            do {
                msgLength = DfxDumpCatcherLocalDumper::g_CurrentPosition;
                DfxLogDebug("%s :: DoDumpLocalPidTid :: msgLength(%lld), time(%d).",
                    DFXDUMPCATCHER_TAG.c_str(), msgLength, time);

                sleep(1);
                time++;

                DfxLogDebug("%s :: DoDumpLocalPidTid :: g_CurrentPosition(%lld), time(%d).",
                    DFXDUMPCATCHER_TAG.c_str(), DfxDumpCatcherLocalDumper::g_CurrentPosition, time);
            } while (time < SLEEP_TIME_TWENTY_S && msgLength != DfxDumpCatcherLocalDumper::g_CurrentPosition);

            DfxLogDebug("%s :: DoDumpLocalPidTid :: return true.", DFXDUMPCATCHER_TAG.c_str());
            return true;
        } else {
            break;
        }
    } while (false);

    DfxLogError("%s :: DoDumpLocalPidTid :: return false.", DFXDUMPCATCHER_TAG.c_str());
    return false;
}

bool DfxDumpCatcher::DoDumpLocalPid(const int pid)
{
    DfxLogDebug("%s :: DoDumpLocalPid :: pid(%d).", DFXDUMPCATCHER_TAG.c_str(), pid);
    if (pid <= 0) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as param error.", DFXDUMPCATCHER_TAG.c_str());
        return false;
    }

    char path[NAME_LEN] = {0};
    if (snprintf_s(path, sizeof(path), sizeof(path) - 1, "/proc/%d/task", pid) <= 0) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as snprintf_s failed.", DFXDUMPCATCHER_TAG.c_str());
        return FALSE;
    }

    char realPath[PATH_MAX] = {'\0'};
    if (realpath(path, realPath) == NULL) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as realpath failed.", DFXDUMPCATCHER_TAG.c_str());
        return FALSE;
    }

    DIR *dir = opendir(realPath);
    if (dir == NULL) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as opendir failed.", DFXDUMPCATCHER_TAG.c_str());
        return FALSE;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) {
            continue;
        }

        if (strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        pid_t tid = atoi(ent->d_name);
        if (tid == 0) {
            continue;
        }

        int writeNumber = 0;
        char* current_pos = DfxDumpCatcherLocalDumper::g_StackInfo_ + DfxDumpCatcherLocalDumper::g_CurrentPosition;
        writeNumber = sprintf_s(current_pos, BACK_STACK_INFO_SIZE-DfxDumpCatcherLocalDumper::g_CurrentPosition, \
            "Tid: %d\n", tid);
        if (writeNumber >= 0) {
            DfxDumpCatcherLocalDumper::g_CurrentPosition = DfxDumpCatcherLocalDumper::g_CurrentPosition + writeNumber;
        }

        int currentTid = syscall(SYS_gettid);
        if (tid == currentTid) {
            DfxDumpCatcherLocalDumper::ExecLocalDump(pid, tid, NUMBER_TWO);
        } else {
            DoDumpLocalPidTid(pid, tid);
        }
    }
    DfxLogDebug("%s :: DoDumpLocalPid :: return true.", DFXDUMPCATCHER_TAG.c_str());
    return true;
}

bool DfxDumpCatcher::DoDumpRemote(const int pid, const int tid)
{
    DfxLogDebug("%s :: DoDumpRemote :: pid(%d), tid(%d).", DFXDUMPCATCHER_TAG.c_str(), pid, tid);
    if (pid <= 0 || tid < 0) {
        DfxLogError("%s :: DoDumpRemote :: param error.", DFXDUMPCATCHER_TAG.c_str());
        return false;
    }

    if (RequestSdkDump(pid, tid) == true) {
        int time = 0;
        // Get stack trace file
        long long stackFileLength = 0;
        std::string stackTraceFilePatten = LOG_FILE_PATH + "/stacktrace-" + std::to_string(pid);
        std::string stackTraceFileName = "";
        do {
            sleep(1);
            std::vector<std::string> files;
            OHOS::GetDirFiles(LOG_FILE_PATH, files);
            int i = 0;
            while (i < files.size() && files[i].find(stackTraceFilePatten) == std::string::npos) {
                i++;
            }
            if (i < files.size()) {
                stackTraceFileName = files[i];
            }
            time++;
        } while (stackTraceFileName == "" && time < SLEEP_TIME_TWENTY_S);
        DfxLogDebug("%s :: DoDumpRemote :: Got stack trace file %s.",
            DFXDUMPCATCHER_TAG.c_str(), stackTraceFileName.c_str());

        // Get stack trace file length
        time = 0;
        do {
            double filesize = -1;
            struct stat fileInfo;

            sleep(1);
            time++;

            if (stat(stackTraceFileName.c_str(), &fileInfo) < 0) {
                continue;
            } else if (stackFileLength == fileInfo.st_size) {
                break;
            } else {
                stackFileLength = fileInfo.st_size;
            }
        } while (time < SLEEP_TIME_TEN_S);
        DfxLogDebug("%s :: DoDumpRemote :: Got file length %lld.", DFXDUMPCATCHER_TAG.c_str(), stackFileLength);

        // Read stack trace file content.
        FILE *fp = nullptr;
        fp = fopen(stackTraceFileName.c_str(), "r");
        if (fp == nullptr) {
            DfxLogError("%s :: DoDumpRemote :: open %s NG.", DFXDUMPCATCHER_TAG.c_str(), stackTraceFileName.c_str());
            return false;
        }
        fread(DfxDumpCatcherLocalDumper::g_StackInfo_, 1, BACK_STACK_INFO_SIZE, fp);
        DfxLogDebug("%s :: DoDumpRemote :: Readed stack trace file content.", DFXDUMPCATCHER_TAG.c_str());
        fclose(fp);
        fp = nullptr;
        OHOS::RemoveFile(stackTraceFileName);

        // As tid is specified, we need del other thread info, just keep tid back trace stack info.
        if (tid != 0) {
            char *cutPosition = strstr(DfxDumpCatcherLocalDumper::g_StackInfo_, "Other thread info:");
            if (cutPosition != NULL) {
                *cutPosition = '\0';
            }
        }
        DfxLogDebug("%s :: DoDumpRemote :: return true.", DFXDUMPCATCHER_TAG.c_str());
        return true;
    }
    DfxLogError("%s :: DoDumpRemote :: return false.", DFXDUMPCATCHER_TAG.c_str());
    return false;
}

bool DfxDumpCatcher::DumpCatch(const int pid, const int tid, std::string& msg)
{
    pthread_mutex_lock(&g_dumpCatcherMutex);
    bool ret = false;
    int currentPid = getpid();
    int currentTid = syscall(SYS_gettid);
    DfxLogDebug("%s :: dump_catch :: cPid(%d), cTid(%d), pid(%d), tid(%d).",
        DFXDUMPCATCHER_TAG.c_str(), currentPid, currentTid, pid, tid);

    if (pid <= 0 || tid < 0) {
        DfxLogError("%s :: dump_catch :: param error.", DFXDUMPCATCHER_TAG.c_str());
        pthread_mutex_unlock(&g_dumpCatcherMutex);
        return false;
    }
    DfxDumpCatcherLocalDumper::g_StackInfo_ = (char *) calloc(BACK_STACK_INFO_SIZE + 1, 1);
    errno_t err = memset_s(DfxDumpCatcherLocalDumper::g_StackInfo_, BACK_STACK_INFO_SIZE + 1, \
        '\0', BACK_STACK_INFO_SIZE);
    if (err != EOK) {
        DfxLogError("%s :: dump_catch :: memset_s failed, err = %d\n", DFXDUMPCATCHER_TAG.c_str(), err);
        FreeStackInfo();
        pthread_mutex_unlock(&g_dumpCatcherMutex);
        return false;
    }
    DfxDumpCatcherLocalDumper::g_CurrentPosition = 0;

    if (pid == currentPid) {
        DfxDumpCatcherLocalDumper::DFX_InstallLocalDumper(SIGDUMP);

        if (tid == currentTid) {
            ret = DfxDumpCatcherLocalDumper::ExecLocalDump(pid, tid, NUMBER_ONE);
        } else if (tid == 0) {
            ret = DoDumpLocalPid(pid);
        } else {
            ret = DoDumpLocalPidTid(pid, tid);
        }

        DfxDumpCatcherLocalDumper::DFX_UninstallLocalDumper(SIGDUMP);
    } else {
        ret = DoDumpRemote(pid, tid);
    }

    if (ret == true) {
        msg = DfxDumpCatcherLocalDumper::g_StackInfo_;
    }

    FreeStackInfo();
    DfxLogDebug("%s :: dump_catch :: ret(%d), msg(%s).", DFXDUMPCATCHER_TAG.c_str(), ret, msg.c_str());
    pthread_mutex_unlock(&g_dumpCatcherMutex);
    return ret;
}
} // namespace HiviewDFX
} // namespace OHOS