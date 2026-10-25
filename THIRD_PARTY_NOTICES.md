# Third-party notices

BeatSaberQuestAP is released under the MIT License (see `LICENSE`). It includes or is based on the following work.

## BeatSaberAP

This mod is a Quest port of [BeatSaberAP](https://github.com/N00byKing/BeatSaberAP) by N00byKing and CurrentlyPending. No code from the PC mod is included; this port talks to the same Archipelago server and apworld.

## Beat Saber apworld playlist cover

`include/Content/PlaylistIcon.hpp` holds the playlist cover image from the Beat Saber apworld (`worlds/beat_saber/PlaylistOutput.py` in [N00byKing/Archipelago](https://github.com/N00byKing/Archipelago)), which is distributed under that repository's MIT License:

```
MIT License

Copyright (c) 2017 LLCoolDave
Copyright (c) 2025 Berserker66
Copyright (c) 2022 CaitSith2
Copyright (c) 2021 LegendaryLinux

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## IXWebSocket

The mod binary statically links [IXWebSocket](https://github.com/machinezone/IXWebSocket) v11.4.6:

```
Copyright (c) 2018 Machine Zone, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the
   distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Mbed TLS

The mod binary statically links [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) v3.6.2, used under the Apache License 2.0 (one of its dual licenses). Copyright The Mbed TLS Contributors. The license text is in `licenses/Apache-2.0.txt`; the library is used unmodified.

## Quest modding libraries

Like every Quest mod, the binary compiles in header code from [beatsaber-hook](https://github.com/QuestPackageManager/beatsaber-hook) and [bs-cordl](https://github.com/QuestPackageManager/bs-cordl), plus beatsaber-hook's copy of [And64InlineHook](https://github.com/Rprop/And64InlineHook). beatsaber-hook and And64InlineHook are under the MIT License, with the same permission notice as reproduced above:

- beatsaber-hook: Copyright (c) 2020 Adam ?
- And64InlineHook: Copyright (c) 2018 Rprop (r_prop@outlook.com)

custom-types, paper, BSML, SongCore, BeatSaverPlusPlus and web-utils are loaded as separate libraries under their own licenses and are not included in this mod's binary.
