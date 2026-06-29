/*
 * Copyright 2016 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// sysio-pp: WIRE-specific WASM post-pass. Reads the linked contract wasm,
// strips data segments that are only zero-initialized (bss), re-fractures the
// remaining initialized memory into compact segments, and appends the heap
// pointer data segment. This is WIRE source built against libwabt; it is NOT a
// stock WABT tool. Ported from the 2018-era vendored WABT API to the
// vcpkg-provided WABT 1.0.41 public API (`wabt/` include prefix, `Errors`
// vector error handling, `Const::I32` factory, named `DataSegment` ctor).

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "wabt/binary-reader-ir.h"
#include "wabt/binary-reader.h"
#include "wabt/binary-writer.h"
#include "wabt/common.h"
#include "wabt/error.h"
#include "wabt/feature.h"
#include "wabt/ir.h"
#include "wabt/leb128.h"
#include "wabt/option-parser.h"
#include "wabt/stream.h"

using namespace wabt;

static int s_verbose;
static std::string s_infile;
static std::string s_outfile;
static Features s_features;
static WriteBinaryOptions s_write_binary_options;

static void InitFeatures() {
   s_features.enable_sign_extension();
   s_features.enable_mutable_globals();
}

static std::unique_ptr<FileStream> s_log_stream;

static const char s_description[] =
R"(  Read a file in the WebAssembly binary format, strip bss or any data segment that is only initialized to zeros, and other post processing.

  $ sysio-pp test.wasm -o test.stripped.wasm

  # or original replacement
  $ wasm2wat test.wasm
)";

static void ParseOptions(int argc, char** argv) {
  OptionParser parser("postprocess", s_description);

  parser.AddOption('v', "verbose", "Use multiple times for more info", []() {
    s_verbose++;
    s_log_stream = FileStream::CreateStdout();
  });
  parser.AddOption(
      'o', "output", "FILENAME",
      "Output file for the generated wast file, by default use stdout",
      [](const char* argument) {
        s_outfile = argument;
        ConvertBackslashToSlash(&s_outfile);
      });
  parser.AddArgument("filename", OptionParser::ArgumentCount::One,
                     [](const char* argument) {
                       s_infile = argument;
                       ConvertBackslashToSlash(&s_infile);
                     });
  parser.Parse(argc, argv);
}

bool GetHeapPtr( Module& mod, const std::vector<uint8_t>& buff, uint32_t& heap_ptr ) {
   auto* global = mod.GetGlobal(Var(Index(1), Location()));
   if (!global || global->init_expr.empty())
      return false;
   size_t offset = global->init_expr.begin()->loc.offset;
   ReadS32Leb128(buff.data()+offset+4, buff.data()+offset+9, &heap_ptr);
   return true;
}

bool GetStackPtr( Module& mod, const std::vector<uint8_t>& buff, uint32_t& stack_ptr ) {
   auto* global = mod.GetGlobal(Var(Index(0), Location()));
   if (!global || global->init_expr.empty())
      return false;
   size_t offset = global->init_expr.begin()->loc.offset;
   ReadS32Leb128(buff.data()+offset+4, buff.data()+offset+9, &stack_ptr);
   return true;
}

inline bool IsZeroed(const DataSegment* ds) {
   for ( auto d : ds->data ) {
      if (d != 0)
         return false;
   }
   return true;
}

std::vector<DataSegment*> StripZeroedData( std::vector<DataSegment*>&& ds, size_t& fix_bytes ) {
   for ( auto itr=ds.begin(); itr != ds.end();) {
      if (IsZeroed(*itr)) {
         fix_bytes += (*itr)->data.size();
	 itr = ds.erase(itr);
      } else {
         ++itr;
      }
   }
   return ds;
}

inline std::vector<uint8_t> FillFromSegments(const std::vector<DataSegment*>& segments) {
  // Size the reconstructed memory image by the maximum (offset + size) across
  // ALL segments, not just the last one. The original sized it from the last
  // segment, which assumes the segments are ordered by ascending offset; when
  // they are not (e.g. an already-post-passed / re-fractured module) a segment
  // with a higher offset than the last overflowed the buffer in the write loop
  // below. This was the WSA-020 out-of-bounds write. Using the max keeps the
  // result byte-identical whenever the last segment already had the highest end
  // (every normal single-pass contract link), and makes the pathological
  // orderings well-defined instead of undefined behaviour.
  std::size_t mem_size = 0;
  for (auto ds : segments) {
    auto offset = reinterpret_cast<ConstExpr*>(&(ds->offset.front()))->const_.u32();
    mem_size = std::max<std::size_t>(mem_size,
                                     static_cast<std::size_t>(offset) + ds->data.size());
  }
  std::vector<uint8_t> memory(mem_size, 0);

  for (auto ds : segments) {
    auto offset = reinterpret_cast<ConstExpr*>(&(ds->offset.front()))->const_.u32();
    for (std::size_t i=0; i < ds->data.size(); ++i) {
      memory[offset+i] = ds->data[i];
    }
  }

  return memory;
}

inline std::unique_ptr<DataSegment> CreateSegment(uint32_t offset, uint8_t* start, std::size_t size) {
   auto segment = std::make_unique<DataSegment>(std::string_view{});
   Const c = Const::I32(offset);
   std::unique_ptr<Expr> ce(new ConstExpr(c));
   segment->memory_var = Var(Index(0), Location());
   segment->offset = ExprList{std::move(ce)};
   segment->data.resize(size);
   std::memcpy(segment->data.data(), start, size);
   return segment;
}

inline std::vector<std::unique_ptr<DataSegment>> CreateSegments(std::vector<uint8_t> memory) {
   std::vector<std::unique_ptr<DataSegment>> segments;

   std::size_t f=0;
   // look for the first non-zero entry
   for (; f < memory.size(); ++f) {
      if (memory[f] != 0)
         break;
   }

   uint32_t zero_span = 0;
   uint32_t last_offset = f;

   for (std::size_t i=f; i < memory.size(); ++i) {
      zero_span = memory[i] == 0 ? zero_span + 1 : 0;
      std::size_t size = i - last_offset;
      if (zero_span > 8 || (size > 1024)) {
         segments.push_back(CreateSegment(last_offset, &memory[last_offset], size));
         last_offset = i;
         zero_span = 0;
         i += size;
         continue;
      }
   }
   segments.push_back(CreateSegment(last_offset, &memory[last_offset], memory.size()-last_offset));

   return segments;
}

bool AddHeapPointerData( Module& mod, size_t fixup, const std::vector<uint8_t>& buff, DataSegment& ds ) {
   uint32_t heap_ptr_val;
   if (!GetHeapPtr(mod, buff, heap_ptr_val))
      return false;
   heap_ptr_val = (heap_ptr_val + 7) & ~7; // align to 8 bytes
   Const c = Const::I32(0);
   std::unique_ptr<Expr> ce(new ConstExpr(c));
   ds.memory_var = Var(Index(0), Location());
   ds.offset = ExprList{std::move(ce)};
   uint8_t* dat = reinterpret_cast<uint8_t*>(&heap_ptr_val);
   ds.data = std::vector<uint8_t>{dat[0],
                                  dat[1],
                                  dat[2],
                                  dat[3]};
   mod.data_segments.push_back(&ds);
   return true;
}

void WriteBufferToFile(std::string_view filename,
                       const OutputBuffer& buffer) {
  buffer.WriteToFile(filename);
}

int ProgramMain(int argc, char** argv) {
  Result result;

  InitStdio();
  InitFeatures();
  ParseOptions(argc, argv);

  std::vector<uint8_t> file_data;
  result = ReadFile(s_infile.c_str(), &file_data);
  DataSegment _hds(std::string_view{});
  // Owns the DataSegments produced by CreateSegments for the lifetime of the
  // write below. The original code pushed `new DataSegment()` pointers into
  // Module::data_segments (a non-owning vector) and never freed them — that was
  // the LeakSanitizer finding in WSA-020. Owning them here keeps them alive
  // until after WriteBinaryModule and frees them on return.
  std::vector<std::unique_ptr<DataSegment>> owned_segments;
  if (Succeeded(result)) {
    Errors errors;
    Module module;
    const bool kStopOnFirstError = true;
    ReadBinaryOptions options(s_features, s_log_stream.get(),
                              /*read_debug_names=*/false, kStopOnFirstError,
                              /*fail_on_custom_section_error=*/false);
    result = ReadBinaryIr(s_infile.c_str(), file_data.data(),
                          file_data.size(), options, &errors, &module);

    if (Succeeded(result)) {
      // Drop custom sections. The previous vendored (2018-era) WABT writer did
      // not round-trip custom sections, so the toolchain's post-passed contracts
      // never carried them (e.g. clang's "llvm.func_attr.annotate.sysio_action").
      // WABT 1.0.41 preserves them, which would change every contract's bytes
      // (and code hash) for purely-metadata sections the runtime ignores. Clear
      // them to keep post-pass output byte-identical to the vendored toolchain.
      module.customs.clear();
      size_t fixup = 0;
      if (!module.data_segments.empty()) {
        auto pre_memory = FillFromSegments(module.data_segments);
        owned_segments  = CreateSegments(pre_memory);
        std::vector<DataSegment*> seg_view;
        seg_view.reserve(owned_segments.size());
        for (auto& s : owned_segments)
          seg_view.push_back(s.get());
        if (pre_memory != FillFromSegments(seg_view)) {
          std::cerr << "Fractured Memory Failed, not applying optimizations" << std::endl;
          module.data_segments = StripZeroedData(std::move(module.data_segments), fixup);
        } else {
          module.data_segments = StripZeroedData(std::move(seg_view), fixup);
        }
      }
      if (!AddHeapPointerData(module, fixup, file_data, _hds)) {
        if (s_verbose)
          std::cerr << "Warning: could not find heap pointer global, skipping heap fixup" << std::endl;
      }
     if (Succeeded(result)) {
      MemoryStream stream(s_log_stream.get());
      result =
          WriteBinaryModule(&stream, &module, s_write_binary_options);

      if (Succeeded(result)) {
        if (s_outfile.empty()) {
          s_outfile = s_infile;
        }
        WriteBufferToFile(s_outfile, stream.output_buffer());
      }
    }
   }

  }
  return result != Result::Ok;
}

int main(int argc, char** argv) {
  WABT_TRY
  return ProgramMain(argc, argv);
  WABT_CATCH_BAD_ALLOC_AND_EXIT
}
