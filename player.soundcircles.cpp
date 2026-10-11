#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::player {

	void overlay::add_sound_circles( xdraw::draw_list& draw_list, const info& info, const settings::esp::player::overlay::sound_circles& cfg )
	{
		// Get player flags to detect jumps
		const auto flags = memory::read<std::uint32_t>( info.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
		const auto is_on_ground = ( flags & ( 1 << 0 ) ) != 0; // FL_ONGROUND

		auto& circle = m_sound_circles[ info.pawn ];

		// Detect landing (transition from air to ground) - this creates the sound circle
		const auto was_on_ground = ( circle.last_flags & ( 1 << 0 ) ) != 0;
		
		if ( !circle.active && circle.last_flags != 0 && !was_on_ground && is_on_ground )
		{
			// Player just landed (creates sound)
			circle.position = info.origin;
			circle.start_time = 0.0f; // Reset time counter
			circle.active = true;
		}

		circle.last_flags = flags;

		// Render active circle
		if ( circle.active )
		{
			const auto dt = xdraw::delta_time( );
			circle.start_time += dt; // Accumulate time
			const auto elapsed = circle.start_time;
			const auto duration = cfg.duration.value;

			if ( elapsed >= duration )
			{
				circle.active = false;
			}
			else
			{
				const auto progress = elapsed / duration;
				const auto base_size = cfg.size.value;

				// Project ground position
				auto ground_pos = circle.position;
				ground_pos.z -= 64.0f; // Lower to ground level
				const auto screen = systems::g_view.project( ground_pos );

				if ( systems::g_view.projection_valid( screen ) )
				{
					// Main circle with fade
					if ( cfg.fade.value )
					{
						const auto fade_progress = std::min( 1.0f, progress * 1.5f );
						const auto start_col = cfg.color.value;
						const auto end_col = cfg.fade_color.value;

						// Interpolate color
						const auto r = static_cast<std::uint8_t>( start_col.r + ( end_col.r - start_col.r ) * fade_progress );
						const auto g = static_cast<std::uint8_t>( start_col.g + ( end_col.g - start_col.g ) * fade_progress );
						const auto b = static_cast<std::uint8_t>( start_col.b + ( end_col.b - start_col.b ) * fade_progress );
						const auto a = static_cast<std::uint8_t>( start_col.a + ( end_col.a - start_col.a ) * fade_progress );

						draw_list.circle( screen.x, screen.y, base_size * ( 1.0f + progress * 0.5f ), xdraw::color{ r, g, b, a }, 32, 2.0f );
					}

					// Ripple effect
					if ( cfg.ripple.value )
					{
						const auto ripple_count = 3;
						for ( int i = 0; i < ripple_count; ++i )
						{
							const auto ripple_delay = static_cast<float>( i ) * 0.15f;
							const auto ripple_progress = std::max( 0.0f, std::min( 1.0f, ( progress - ripple_delay ) / ( 1.0f - ripple_delay ) ) );

							if ( ripple_progress > 0.0f )
							{
								const auto ripple_alpha = static_cast<std::uint8_t>( cfg.ripple_color.value.a * ( 1.0f - ripple_progress ) );
								const auto ripple_col = xdraw::color{ cfg.ripple_color.value.r, cfg.ripple_color.value.g, cfg.ripple_color.value.b, ripple_alpha };
								const auto ripple_radius = base_size * ( 0.5f + ripple_progress * 1.5f );

								draw_list.circle( screen.x, screen.y, ripple_radius, ripple_col, 32, 1.5f );
							}
						}
					}
				}
			}
		}
	}

} // namespace features::esp::player
