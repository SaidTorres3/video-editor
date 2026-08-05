# MemoryModule

VideoEditor vendors `MemoryModule.c` and `MemoryModule.h` from the
[`exceptions-64bit`](https://github.com/fancycode/MemoryModule/tree/exceptions-64bit)
branch at commit `1adaf0ef668f60e9757768b458d727a738932fea`.

That branch registers a mapped x64 image's exception table with
`RtlAddFunctionTable`, which is required for correct stack unwinding through
the embedded 64-bit TEN VAD runtime. The vendored files are unmodified and are
licensed under the Mozilla Public License 2.0; see `LICENSE.txt`.

MemoryModule is a manual PE mapper, not the native Windows loader. It does not
register the image in the Windows loader's module list or reproduce all load
configuration processing, including full Control Flow Guard metadata handling.
The preferred long-term replacement is a genuine static TEN VAD library if TEN
publishes one.
