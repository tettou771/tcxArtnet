# tcxArtnet Licenses

## tcxArtnet

MIT License

Copyright (c) 2026 tettou771

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

<!--
  tcxArtnet bundles no third-party *code*: the Art-Net packet format is built by
  hand on top of TrussC's core UdpSocket. If you add a library later, append a
  `---`-separated section here stating its source and its own license.
-->

---

## Art-Net

Art-Net™ Designed by and Copyright Artistic Licence Engineering Ltd.

The Art-Net protocol is published royalty-free, subject to its conditions: the
above trademark credit must appear in product documentation, and any shipped
product that implements Art-Net requires an OEM Code from Artistic Licence
(https://art-net.org.uk/oem-code-zone). tcxArtnet implements the protocol
independently and defaults its OEM code to `OemUnknown` (0x00FF).

The Art-Net logo bundled at `docs/art-net-logo.png` is from the official master
logo pack and remains the property of Artistic Licence Engineering Ltd; it is
used in accordance with their logo policy (https://art-net.org.uk/).
