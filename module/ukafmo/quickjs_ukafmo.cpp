/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <string>
#include <vector>

extern "C"
{
#include "quickjs.h"
}


/**
 * Mutex
 * 
 * ベースウェアが起動中であることを示すため、SSPは"ssp"、MateriaとCROWは"sakura"という名前のMutexを保持しています。
 * 
 * この名前つきMutexの存在を確認することで、起動中かどうかをローコストに判定できます。
 * 
 * Mutex自体の状態は決められておらず、シグナル状態か否かの確認は不要です。
 * @return true if either "ssp" or "sakura" mutex exists, false otherwise
 */
bool isUkagakaRunning(){
    HANDLE hmutexSSP = OpenMutex(MUTEX_ALL_ACCESS, FALSE, L"ssp");
    HANDLE hmutexSakura = OpenMutex(MUTEX_ALL_ACCESS, FALSE, L"sakura");
    if (hmutexSSP == NULL || hmutexSakura == NULL) {
        if (hmutexSSP) CloseHandle(hmutexSSP);
        if (hmutexSakura) CloseHandle(hmutexSakura);
        return false;
    }
    if (hmutexSSP) CloseHandle(hmutexSSP);
    if (hmutexSakura) CloseHandle(hmutexSakura);
    return true;
}