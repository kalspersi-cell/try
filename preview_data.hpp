// Preview art that is compiled into Kitty.dll.
//
// The pictures are what let the menu show a model tile to somebody who only has
// the dll, with no asset folder beside it. tools/generate_preview_data.ps1 writes
// the matching preview_data.cpp; this header is the fixed half of the pair.
#pragma once

#include <cstddef>
#include <string_view>

namespace preview_data
{
	struct blob
	{
		const char* name;            // asset-relative key, e.g. "custom_models/ghost_face.png"
		const unsigned char* data;   // png bytes, loaded through xdraw::load_texture
		std::size_t size;
	};

	// Returns null when no picture was compiled in under that name, which is the
	// case for any model whose folder ships without a preview.png.
	[[nodiscard]] const blob* find( std::string_view name );

} // namespace preview_data
