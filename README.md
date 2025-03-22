# nopus
A simple tool for decoding Nintendo Opus (NOT Ogg Opus) audio files.

These files are simply Opus data in a proprietary container by Nintendo. They are very common in Nintendo Switch games as the hardware has Opus decoding functionality in the DSP (it's not used for everything though, since there's a limited number of decoder instances).

A Makefile is included; simply run `make` to build (please note that libopus is required).

This program is licensed under the MIT license.

conhlee 2025