/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

/* This files is writer log to file module on process dump module. */

#include "dfx_dump_writer.h"
#include "dfx_process.h"  // for DfxProcess
#include "memory"         // for shared_ptr

namespace OHOS {
namespace HiviewDFX {
DfxDumpWriter::DfxDumpWriter(std::shared_ptr<DfxProcess> process, int32_t fromSignalHandler)
{
    process_ = process;
    fromSignalHandler_ = fromSignalHandler;
}
} // namespace HiviewDFX
} // namespace OHOS
