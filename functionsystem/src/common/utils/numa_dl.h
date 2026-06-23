/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
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

#ifndef FUNCTIONSYSTEM_NUMA_DL_H
#define FUNCTIONSYSTEM_NUMA_DL_H

struct Bitmask {
    unsigned long size;
    unsigned long *maskp;
};

namespace functionsystem::utils {

int FsNumaAvailable(void);
int FsNumaMaxNode(void);
int FsNumaNumConfiguredCpus(void);
Bitmask* FsNumaAllocateCpumask(void);
void FsNumaFreeCpumask(Bitmask*);
Bitmask* FsNumaAllocateNodemask(void);
void FsNumaBitmaskFree(Bitmask*);
Bitmask* FsNumaBitmaskSetbit(Bitmask*, unsigned int);
int FsNumaBitmaskIsbitset(const Bitmask*, unsigned int);
int FsNumaNodeToCpus(int, Bitmask*);
int FsNumaRunOnNode(int);
int FsNumaRunOnNodeMask(Bitmask*);
void FsNumaSetMembind(Bitmask*);
Bitmask* FsNumaGetMembind(void);
Bitmask* FsNumaGetRunNodeMask(void);

} // namespace functionsystem::utils

#endif // FUNCTIONSYSTEM_NUMA_DL_H