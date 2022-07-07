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
#include "dfx_unwind_local.h"
#include "dfx_dump_res.h"

#include <algorithm>
#include <climits>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <strstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <file_ex.h>
#include <poll.h>
#include <pthread.h>
#include <securec.h>
#include <ucontext.h>
#include <unistd.h>

#include <sstream>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "directory_ex.h"
#include "../faultloggerd_client/include/faultloggerd_client.h"

static const int NUMBER_TEN = 10;
static const int MAX_TEMP_FILE_LENGTH = 256;
static const int DUMP_CATCHE_WORK_TIME_S = 60;
static const int NUMBER_TWO_KB = 2048;


namespace OHOS {
namespace HiviewDFX {
namespace {
static const std::string DFXDUMPCATCHER_TAG = "DfxDumpCatcher";
}

DfxDumpCatcher::DfxDumpCatcher()
{
}

DfxDumpCatcher::~DfxDumpCatcher()
{
}

bool DfxDumpCatcher::DoDumpLocalTid(int tid, std::string& msg)
{
    bool ret = false;
    if (tid <= 0) {
        DfxLogError("%s :: DoDumpLocalTid :: return false as param error.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }
    
    if (DfxUnwindLocal::GetInstance().SendLocalDumpRequest(tid) == true) {
        ret = DfxUnwindLocal::GetInstance().WaitLocalDumpRequest();
    }

    if (ret) {
        msg.append(DfxUnwindLocal::GetInstance().CollectUnwindResult(tid));
    } else {
        msg.append("Failed to dump thread:" + std::to_string(tid) + ".\n");
    }
    DfxLogDebug("%s :: DoDumpLocalTid :: return %d.", DFXDUMPCATCHER_TAG.c_str(), ret);
    return ret;
}

bool DfxDumpCatcher::DoDumpLocalPid(int pid, std::string& msg)
{
    bool ret = false;
    if (pid <= 0) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as param error.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }

    char realPath[PATH_MAX] = {'\0'};
    if (realpath("/proc/self/task", realPath) == nullptr) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as realpath failed.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }

    DIR *dir = opendir(realPath);
    if (dir == nullptr) {
        DfxLogError("%s :: DoDumpLocalPid :: return false as opendir failed.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        pid_t tid = atoi(ent->d_name);
        if (tid == 0) {
            continue;
        }

        int currentTid = syscall(SYS_gettid);
        if (tid == currentTid) {
            DfxUnwindLocal::GetInstance().ExecLocalDumpUnwind(tid, DUMP_CATCHER_NUMBER_THREE);
            msg.append(DfxUnwindLocal::GetInstance().CollectUnwindResult(tid));
        } else {
            ret = DoDumpLocalTid(tid, msg);
        }
    }

    if (closedir(dir) == -1) {
        DfxLogError("closedir failed.");
    }
    
    DfxLogDebug("%s :: DoDumpLocalPid :: return %d.", DFXDUMPCATCHER_TAG.c_str(), ret);
    return ret;
}

bool DfxDumpCatcher::DoDumpRemoteLocked(int pid, int tid, std::string& msg)
{
    return DoDumpCatchRemote(pid, tid, msg);
}

bool DfxDumpCatcher::DoDumpLocalLocked(int pid, int tid, std::string& msg)
{
    bool ret = DfxUnwindLocal::GetInstance().Init();
    if (!ret) {
        DfxLogError("%s :: DoDumpLocal :: Init error.", DFXDUMPCATCHER_TAG.c_str());
        DfxUnwindLocal::GetInstance().Destroy();
        return ret;
    }

    if (tid == syscall(SYS_gettid)) {
        ret = DfxUnwindLocal::GetInstance().ExecLocalDumpUnwind(tid, DUMP_CATCHER_NUMBER_TWO);
        msg.append(DfxUnwindLocal::GetInstance().CollectUnwindResult(tid));
    } else if (tid == 0) {
        ret = DoDumpLocalPid(pid, msg);
    } else {
        ret = DoDumpLocalTid(tid, msg);
    }

    DfxUnwindLocal::GetInstance().Destroy();
    DfxLogDebug("%s :: DoDumpLocal :: ret(%d).", DFXDUMPCATCHER_TAG.c_str(), ret);
    return ret;
}

bool DfxDumpCatcher::DumpCatch(int pid, int tid, std::string& msg)
{
    bool ret = false;
    if (pid <= 0 || tid < 0) {
        DfxLogError("%s :: dump_catch :: param error.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }

    std::unique_lock<std::mutex> lck(dumpCatcherMutex_);
    int currentPid = getpid();
    DfxLogDebug("%s :: dump_catch :: cPid(%d), pid(%d), tid(%d).",
        DFXDUMPCATCHER_TAG.c_str(), currentPid, pid, tid);

    if (pid == currentPid) {
        ret = DoDumpLocalLocked(pid, tid, msg);
    } else {
        ret = DoDumpRemoteLocked(pid, tid, msg);
    }
    DfxLogDebug("%s :: dump_catch :: ret(%d), msg(%s).", DFXDUMPCATCHER_TAG.c_str(), ret, msg.c_str());
    return ret;
}

bool DfxDumpCatcher::DumpCatchFd(int pid, int tid, std::string& msg, int fd)
{
    bool ret = false;
    ret = DumpCatch(pid, tid, msg);
    if (fd > 0) {
        ret = write(fd, msg.c_str(), msg.length());
    }
    return ret;
}

bool DfxDumpCatcher::DoDumpCatchRemote(int pid, int tid, std::string& msg)
{
    bool ret = false;
    if (pid <= 0 || tid < 0) {
        DfxLogError("%s :: DoDumpCatchRemote :: param error.", DFXDUMPCATCHER_TAG.c_str());
        return ret;
    }

    if (RequestSdkDump(pid, tid) == true) {
        int readBufFd = RequestPipeFd(pid, FaultLoggerPipeType::PIPE_FD_READ_BUF);
        DfxLogDebug("read buf fd: %d", readBufFd);

        int readResFd = RequestPipeFd(pid, FaultLoggerPipeType::PIPE_FD_READ_RES);
        DfxLogDebug("read res fd: %d", readResFd);

        fd_set readfds;
        struct timeval tv = {BACK_TRACE_DUMP_TIMEOUT_S, 0};
        std::string bufMsg, resMsg;
        char buffer[LOG_BUF_LEN];
        while (true) {
            FD_SET(readBufFd, &readfds);
            FD_SET(readResFd, &readfds);
            int selRet = select(readResFd + 1, &readfds, NULL, NULL, &tv);
            if (selRet <= 0) {
                DfxLogError("%s :: %s :: select error", DFXDUMPCATCHER_TAG.c_str(), __func__);
                break;
            }

            if(FD_ISSET(readBufFd, &readfds)){
                bzero(buffer, sizeof(buffer));
                ssize_t nread = read(readBufFd, buffer, sizeof(buffer) - 1);
                if (nread <= 0) {
                    DfxLogError("%s :: %s :: read error", DFXDUMPCATCHER_TAG.c_str(), __func__);
                    break;
                }
                bufMsg.append(buffer);
            }
            else if(FD_ISSET(readResFd, &readfds)){
                DumpResMsg dumpRes;
                read(readResFd, &dumpRes, sizeof(struct DumpResMsg));
                if (dumpRes.res == ProcessDumpRes::DUMP_ESUCCESS) {
                    ret = true;
                }
                DfxDumpRes::GetInstance().SetRes(dumpRes.res);
                resMsg.append("Result: " + DfxDumpRes::GetInstance().ToString() + "\n");
                DfxLogDebug("%s :: %s :: resMsg: %s", DFXDUMPCATCHER_TAG.c_str(), __func__, resMsg.c_str());
                break;
            }
        }
        msg = bufMsg;
    }
    RequestPipeFd(pid, FaultLoggerPipeType::PIPE_FD_DELETE);
    DfxLogDebug("%s :: %s :: ret: %d", DFXDUMPCATCHER_TAG.c_str(), __func__, ret);
    return ret;
}

bool DfxDumpCatcher::DumpCatchMultiPid(const std::vector<int> pidV, std::string& msg)
{
    bool ret = false;
    int pidSize = (int)pidV.size();
    if (pidSize <= 0) {
        DfxLogError("%s :: %s :: param error, pidSize(%d).", DFXDUMPCATCHER_TAG.c_str(), __func__, pidSize);
        return ret;
    }

    std::unique_lock<std::mutex> lck(dumpCatcherMutex_);
    int currentPid = getpid();
    int currentTid = syscall(SYS_gettid);
    DfxLogDebug("%s :: %s :: cPid(%d), cTid(%d), pidSize(%d).", DFXDUMPCATCHER_TAG.c_str(), \
        __func__, currentPid, currentTid, pidSize);

    time_t startTime = time(nullptr);
    if (startTime > 0) {
        DfxLogDebug("%s :: %s :: startTime(%ld).", DFXDUMPCATCHER_TAG.c_str(), __func__, startTime);
    }

    for (int i = 0; i < pidSize; i++) {
        int pid = pidV[i];
        std::string pidStr;
        if (DoDumpRemoteLocked(pid, 0, pidStr)) {
            msg.append(pidStr + "\n");
        } else {
            msg.append("Failed to dump process:" + std::to_string(pid));
        }

        time_t currentTime = time(nullptr);
        if (currentTime > 0) {
            DfxLogDebug("%s :: %s :: startTime(%ld), currentTime(%ld).", DFXDUMPCATCHER_TAG.c_str(), \
                __func__, startTime, currentTime);
            if (currentTime > startTime + DUMP_CATCHE_WORK_TIME_S) {
                break;
            }
        }
    }

    DfxLogDebug("%s :: %s :: msg(%s).", DFXDUMPCATCHER_TAG.c_str(), __func__, msg.c_str());
    if (msg.find("Tid:") != std::string::npos) {
        ret = true;
    }
    return ret;
}

bool DfxDumpCatcher::DumpCatchFrame(int pid, int tid, std::string& msg, \
    std::vector<std::shared_ptr<DfxFrame>>& frames)
{
    if (pid != getpid() || tid == 0) {
        DfxLogError("DumpCatchFrame :: only support localDump.");
        return false;
    }

    bool ret = DfxUnwindLocal::GetInstance().Init();
    if (!ret) {
        DfxLogError("DumpCatchFrame :: failed to init local dumper.");
        DfxUnwindLocal::GetInstance().Destroy();
        return ret;
    }

    std::unique_lock<std::mutex> lck(dumpCatcherMutex_);
    if (tid == syscall(SYS_gettid)) {
        ret = DfxUnwindLocal::GetInstance().ExecLocalDumpUnwind(tid, DUMP_CATCHER_NUMBER_ONE);
        msg = DfxUnwindLocal::GetInstance().CollectUnwindResult(tid);
    } else {
        ret = DoDumpLocalTid(tid, msg);
    }

    if (ret) {
        DfxUnwindLocal::GetInstance().CollectUnwindFrames(frames);
    }

    DfxUnwindLocal::GetInstance().Destroy();
    return ret;
}
} // namespace HiviewDFX
} // namespace OHOS
