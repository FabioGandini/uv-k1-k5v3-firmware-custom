/* Original work Copyright 2023 fagci
 * https://github.com/fagci
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef SPECTRUM_K5_H
#define SPECTRUM_K5_H

// kamilsss655 spectrum analyzer ported from the UV-K5 firmware; coexists
// with the F4HWN bandscope (app/spectrum.c), so everything except the
// entry point is kept private to spectrum_k5.c
void APP_RunSpectrumK5(void);

#endif
