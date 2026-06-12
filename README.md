# Stats

![Alt](https://repobeats.axiom.co/api/embed/ecdd86aa536b716f088339a0c5ee734558f78c28.svg "Repobeats analytics image")

# IU2VTM firmware port for the UV-K1 and UV-K5 V3 using the PY32F071 MCU

This repository is a fork of the [F4HWN custom firmware](https://github.com/armel/uv-k5-firmware-custom), who was a fork of [Egzumer custom firmware](https://github.com/egzumer/uv-k5-firmware-custom). It extends the work done for the UV-K5 V1, based on the DP32G030 MCU, and adapts it to the newer UV-K1 and UV-K5 V3 built around the PY32F071 MCU. It is the result of the joint work of [@muzkr](https://github.com/muzkr) and [@armel](https://github.com/armel).

This firmware adds some functions to the excellent work of Armel F4HWN:
Messenger fsk ported from Nunu kamilsss655 https://github.com/kamilsss655
Additional spectrum analyzer from Nunu kamilsss655 https://github.com/kamilsss655, you can start it with Fn+7


# A note for developers who intend to fork this project

This firmware is distributed under the Apache 2.0 License, carrying forward the original copyright of DualTachyon, whose work laid the foundation for the UV-K5 open-source ecosystem.
If you create a fork or a derived version, **we strongly encourage you to keep your work open source**.

Keeping your fork open:

- aligns with the intent and spirit of the Apache 2.0 License
- supports the amateur-radio and embedded-development community
- avoids unnecessary fragmentation
- allows others to study, audit and improve the firmware

It is also very much in line with the **ham spirit**: sharing knowledge, experimenting together and helping each other, rather than closing things off or claiming them as your own.

Maintaining an open-source fork is the best way to help build a healthy and sustainable ecosystem for everyone.

> [!WARNING]
> EN -  Use this firmware at your own risk. There is absolutely no guarantee that it will work in any way shape or form on your radio(s), it may even brick your radio(s), in which case, you'd need to buy another radio.
Anyway, have fun.

> [!NOTE]
> EN - About CHIRP, as with many other firmwares, you need to use a dedicated driver. The matching CHIRP driver is now bundled with each release of this repository, so you can download the firmware and its driver together from the [Releases page](https://github.com/armel/uv-k1-k5v3-firmware-custom/releases).
>

> [!CAUTION]
> EN - I recommend to backup your calibration data with [uvtools2](https://armel.github.io/uvtools2/) just after flashing this firmware. It's a good reflex to have. 


## License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
