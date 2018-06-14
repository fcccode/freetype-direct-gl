#pragma once

#include "font.h"
#include <string>

namespace ftdgl {
namespace impl {
FontPtr CreateFontFromDesc(util::MemoryBufferPtr mem_buf, FT_Library & library, const std::string & desc);
} //namespace impl
} //namespace ftdgl
