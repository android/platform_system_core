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

#include "DwarfStructs.h"
#include "Machine.h"
#include "Regs.h"

// Arm
Regs* Arm::CreateRegs(std::vector<uint64_t>& reg_mem) {
  reg_mem.resize((ARM_REG_LAST * sizeof(uint32_t)) / 2 + 1);
  return CreateRegs(reg_mem.data());
}

Regs* Arm::CreateRegs(void* data) {
  return new Regs32(ARM_REG_PC, ARM_REG_SP, ARM_REG_LAST, data);
}

// Arm64
Regs* Arm64::CreateRegs(std::vector<uint64_t>& reg_mem) {
  reg_mem.resize(ARM64_REG_LAST * sizeof(uint64_t));
  return CreateRegs(reg_mem.data());
}

Regs* Arm64::CreateRegs(void* data) {
  return new Regs64(ARM64_REG_PC, ARM64_REG_SP, ARM64_REG_LAST, data);
}

// X86
Regs* X86::CreateRegs(std::vector<uint64_t>& reg_mem) {
  reg_mem.resize((X86_REG_LAST * sizeof(uint32_t)) / 2 + 1);
  return CreateRegs(reg_mem.data());
}

Regs* X86::CreateRegs(void* data) {
  return new Regs32(X86_REG_PC, X86_REG_SP, X86_REG_LAST, data);
}

// X86_64
Regs* X86_64::CreateRegs(std::vector<uint64_t>& reg_mem) {
  reg_mem.resize(X86_64_REG_LAST * sizeof(uint64_t));
  return CreateRegs(reg_mem.data());
}

Regs* X86_64::CreateRegs(void* data) {
  return new Regs64(X86_64_REG_PC, X86_64_REG_SP, X86_64_REG_LAST, data);
}
