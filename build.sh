#!/bin/bash

## Without starting from scratch, the relevant XCFLAGS arguments (disabling/enabling embedded fonts) are ignored
## A better solution may be possible
rm -rf ./build/wasm2

echo Building generated files:
make -j4 -C generate

## There are 2 versions of this build--
## 		One build with all major features enabled (only features disabled are 3rd-party dependencies [e.g. OCR] and misc fonts [e.g. emojis])
## 		One build with virtually all features disabled (basically only good for extracting text content from PDFs)
## The former build is ~5MB while the latter build is ~2.8MB.

## Note: The vast majority of these options do not have a meaningful impact on file size.
## Disabling fonts decreases the final .wasm significantly--from 5.0MB to 2.9MB. 
## All of the other options combined decrease the final .wasm file from 2.9MB to 2.8MB.

## Disable non-PDF formats
## FZ_ENABLE_XPS=0
## FZ_ENABLE_SVG=0
## FZ_ENABLE_CBZ=0
## FZ_ENABLE_IMG=0
## FZ_ENABLE_HTML=0
## FZ_ENABLE_EPUB=0

## Disable document writers
## FZ_ENABLE_OCR_OUTPUT=0
## FZ_ENABLE_DOCX_OUTPUT=0
## FZ_ENABLE_ODT_OUTPUT=0

## Disable ICC color profiles
## FZ_ENABLE_ICC=0

## Disable JavaScript
## FZ_ENABLE_JS=0

## Disable fonts
## TOFU
## TOFU_CJK

## The option NO_CJK reduces size considerably, but does so by eliminating cmap values, so may have unintended consequences.  This option is not currently included. 


echo Building library:
make -j4 -C . \
	OS=wasm2 build=release \
	XCFLAGS="-DTOFU -DTOFU_CJK -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_HTML=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_ICC=0 -DFZ_ENABLE_XPS=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -Wno-error=incompatible-pointer-types" \
	libs

## TODO: I believe the following is no longer true as a bug was fixed.  We can consider switching to "Os" optimization.
## Note: Compiling with "Os" optimization was found to introduce a significant bug, which was not present for "O3".
## Therefore, despite the larger file size, "O3" is used for the final build.
## Specifically, when "0s" was used, flags were being changed in a non-deterministic way, without any code to change them.
## When extracting stext from the PDF, `dev->flags & FZ_STEXT_DEHYPHENATE` would sometimes be true and sometimes false, even though the code to change this flag was not present.
## Adding print statement showed that `dev->flags` was switching between 0 and 1082479957.
echo
echo Linking WebAssembly:
emcc -Wall -O3 -g1 -o libmupdf.js \
	-s WASM=1 \
	-s VERBOSE=0 \
	-s ASSERTIONS=1 \
	-s ABORTING_MALLOC=0 \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s MAXIMUM_MEMORY=4GB \
	-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","getValue","UTF8ToString","HEAPU8"]' \
	-s EXPORTED_FUNCTIONS='["_malloc","_free"]' \
	-s FORCE_FILESYSTEM \
	-I ./include \
	--pre-js wrap.js \
	wrap.c \
	./build/wasm2/release/libmupdf.a \
	./build/wasm2/release/libmupdf-third.a

sed -i 's/var FS = {/export var FS = {/g' libmupdf.js
sed -i 's/var Module = typeof/export var Module = typeof/g' libmupdf.js

## Edit so that paths work with Webpack
## This requires setting the file path to an explicit URL (with the path hard-coded in the constructor).
sed -i 's/var wasmBinaryFile;/var wasmBinaryFile;\nconst wasmBinaryFileURL = new URL(".\/libmupdf.wasm", import.meta.url);\nif (typeof process === "undefined") wasmBinaryFile = wasmBinaryFileURL.href;\n/' libmupdf.js

echo Done.
