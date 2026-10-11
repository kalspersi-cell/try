#include <pch/pch.hpp>
#include <chrono>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::misc {
	namespace {

		// The game raises this panorama event when the accept prompt is shown.
		constexpr auto k_accept_popup_event{ "popup_accept_match_found"_hash };

	} // namespace

	void auto_accept::accept( ) const
	{
		const auto set_player_ready = PATTERN( patterns::set_player_ready );
		if ( !set_player_ready )
		{
			logging::console::print( xs( "[auto_accept] set_player_ready pattern not found\n" ) );
			return;
		}

		// SetPlayerReady branches on whether szReadyType compares equal to
		// "deferred". That path walks an internal pointer that is usually null
		// while the prompt is up, so the accept silently does nothing. Any other
		// string takes the path that actually flags the player ready and sends
		// the accept to the game coordinator, and "accept" is what the game's
		// own accept button passes.
		memory::call<bool>( set_player_ready, static_cast< void* >( nullptr ), xs( "accept" ) );
	}

	void auto_accept::on_match_found( ) const
	{
		if ( !settings::g_misc.auto_accept.value )
		{
			return;
		}

		this->accept( );
	}

	void auto_accept::on_panorama_event( const char* event_name )
	{
		if ( !settings::g_misc.auto_accept.value || !event_name )
		{
			return;
		}

		if ( fnv1a::runtime_hash( event_name ) != k_accept_popup_event )
		{
			return;
		}

		m_pending = true;
		m_accept_at = std::chrono::steady_clock::now( ) + std::chrono::milliseconds( 1500 );
	}

	void auto_accept::on_frame_stage_notify( )
	{
		if ( !settings::g_misc.auto_accept.value || !m_pending )
		{
			m_pending = false;
			return;
		}

		if ( std::chrono::steady_clock::now( ) < m_accept_at )
		{
			return;
		}

		m_pending = false;
		this->accept( );
	}

} // namespace features::misc
