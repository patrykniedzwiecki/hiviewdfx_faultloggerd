/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dwarf_unwinder.h"

#include <libunwind.h>
#include <libunwind_i-ohos.h>
#include "dfx_define.h"

namespace OHOS {
namespace HiviewDFX {
namespace {
#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0xD002D11
#define LOG_TAG "DfxDwarfUnwinder"
constexpr int32_t MIN_VALID_FRAME_COUNT = 3;
}

DwarfUnwinder::DwarfUnwinder()
{
    frames_.clear();
}

DwarfUnwinder::~DwarfUnwinder()
{
    frames_.clear();
}

const std::vector<NativeFrame>& DwarfUnwinder::GetFrames() const
{
    return frames_;
}

void DwarfUnwinder::UpdateFrameFuncName(unw_addr_space_t as,
    std::shared_ptr<DfxSymbolsCache> cache, NativeFrame& frame)
{
    if (cache != nullptr) {
        cache->GetNameAndOffsetByPc(as, frame.pc, frame.funcName, frame.funcOffset);
    }
}

bool DwarfUnwinder::UnwindWithContext(unw_addr_space_t as, unw_context_t& context,
    std::shared_ptr<DfxSymbolsCache> cache, size_t skipFrameNum)
{
    if (as == nullptr) {
        return false;
    }

    unw_cursor_t cursor;
    unw_init_local_with_as(as, &cursor, &context);
    size_t index = 0;
    size_t curIndex = 0;
    unw_word_t prevPc = 0;
    do {
        // skip 0 stack, as this is dump catcher. Caller don't need it.
        if (index < skipFrameNum) {
            index++;
            continue;
        }
        curIndex = index - skipFrameNum;

        NativeFrame frame;
        frame.index = curIndex;
        if (unw_get_reg(&cursor, UNW_REG_IP, (unw_word_t*)(&(frame.pc)))) {
            break;
        }

        if (unw_get_reg(&cursor, UNW_REG_SP, (unw_word_t*)(&(frame.sp)))) {
            break;
        }

        if (frame.index > 1 && prevPc == frame.pc) {
            break;
        }
        prevPc = frame.pc;

        frame.relativePc = unw_get_rel_pc(&cursor);
        unw_word_t sz = unw_get_previous_instr_sz(&cursor);
        if ((frame.index > 0) && (frame.relativePc > sz)) {
            frame.relativePc -= sz;
            frame.pc -= sz;
#if defined(__arm__)
            unw_set_adjust_pc(&cursor, frame.pc);
#endif
        }

        struct map_info* map = unw_get_map(&cursor);
        bool isValidFrame = true;
        if ((map != nullptr) && (strlen(map->path) < SYMBOL_BUF_SIZE - 1)) {
            frame.binaryName = std::string(map->path);
            UpdateFrameFuncName(as, cache, frame);
        } else {
            isValidFrame = false;
        }

        if (isValidFrame && (frame.relativePc == 0) && (frame.binaryName.find("Ark") == std::string::npos)) {
            isValidFrame = false;
        }

        if (frame.index < MIN_VALID_FRAME_COUNT || isValidFrame) {
            frames_.emplace_back(frame);
        } else {
            break;
        }

        index++;
    } while ((unw_step(&cursor) > 0) && (curIndex < BACK_STACK_MAX_STEPS));
    return (frames_.size() > 0);
}
} // namespace HiviewDFX
} // namespace OHOS
