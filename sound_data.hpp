#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sound_data
{
	struct blob
	{
		const char* name;
		const unsigned char* data;
		std::size_t size;
	};

	extern const blob embedded[ 5 ];

	// Writes every embedded wav into <directory> when it is not already
	// present, so the sounds travel with the dll instead of living on one pc.
	void extract_all( const std::wstring& directory );
} // namespace sound_data
