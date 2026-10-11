#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::misc {
	namespace {

		[[nodiscard]] std::string controller_name( std::uintptr_t controller )
		{
			if ( !controller )
			{
				return {};
			}

			const auto name_ptr = memory::safe_read<std::uintptr_t>(
				controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) ).value_or( 0 );
			return memory::read_string( name_ptr, 127 );
		}

		void submit_name_change( const std::string& display_name )
		{
			if ( display_name.empty( ) )
			{
				return;
			}

			other::s_display_name = display_name;
			other::s_name_change_pending = true;
			memory::call<void>( PATTERN( patterns::engine_client_cmd ), addresses::globals::source2engine_to_client, 0, xs( "setinfo name x" ), 0x7ffef001 );
			other::s_name_change_pending = false;
		}

	} // namespace

	void other::on_round_start( )
	{
		this->do_autobuy( );
		
		// Reset session stats on round start
		m_session_stats.kills = 0;
		m_session_stats.deaths = 0;
		m_session_stats.headshots = 0;
	}

	#include <chrono>

	// Shared cooldown + send helper for the chat spammer (both the timer and
	// the kill/death triggers go through these so they never double-send).
	static std::chrono::steady_clock::time_point g_chat_spam_last{};

	static bool chat_spammer_can_send( float delay_seconds )
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto elapsed = std::chrono::duration<float>( now - g_chat_spam_last ).count( );

		if ( elapsed < std::max( delay_seconds, 1.0f ) )
		{
			return false;
		}

		g_chat_spam_last = now;
		return true;
	}

	static void send_chat_message( const std::string& message )
	{
		if ( message.empty( ) )
		{
			return;
		}

		const auto cmd = "say " + message;
		memory::call<void>( PATTERN( patterns::engine_client_cmd ), addresses::globals::source2engine_to_client, 0, cmd.c_str( ), 0x7ffef001 );
	}

	void other::on_player_death( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			return;
		}

		// Get event data
		const auto attacker_key = cstypes::event_hash{ 0, "attacker" };
		const auto victim_key = cstypes::event_hash{ 0, "userid" };
		const auto headshot_key = cstypes::event_hash{ 0, "headshot" };

		const auto attacker = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &attacker_key );
		const auto victim = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &victim_key );
		const auto is_headshot = memory::call<bool>( PATTERN( patterns::game_event_get_int ), event, &headshot_key ) != 0;

		// Track stats for local player
		if ( attacker == local.controller )
		{
			m_session_stats.kills++;
			if ( is_headshot )
			{
				m_session_stats.headshots++;
			}
		}

		if ( victim == local.controller )
		{
			m_session_stats.deaths++;
		}

		// Chat spammer: optional burst on kill / death.
		const auto& spam = settings::g_misc.m_chat_spammer;
		if ( spam.enabled.value )
		{
			const auto want_kill = attacker == local.controller && spam.on_kill.value;
			const auto want_death = victim == local.controller && spam.on_death.value;

			if ( ( want_kill || want_death ) && chat_spammer_can_send( spam.delay.value ) )
			{
				send_chat_message( spam.message.snapshot( ) );
			}
		}
	}

	void other::on_frame_stage_notify( )
	{
		this->do_player_alpha_changing( );
		this->do_reveal_radar( );
		this->do_name_changing( );
		this->do_viewmodel_adjust( );
		this->do_chat_spam( );
	}

	void other::do_chat_spam( )
	{
		const auto& spam = settings::g_misc.m_chat_spammer;

		if ( !spam.enabled.value || !spam.on_timer.value )
		{
			return;
		}

		if ( !chat_spammer_can_send( spam.delay.value ) )
		{
			return;
		}

		send_chat_message( spam.message.snapshot( ) );
	}

	void other::do_reveal_radar( ) const
	{
		if ( !settings::g_misc.reveal_radar.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			return;
		}

		const auto spotted_state_offset = SCHEMA( "C_CSPlayerPawn", "m_entitySpottedState"_hash );
		const auto spotted_offset = SCHEMA( "EntitySpottedState_t", "m_bSpotted"_hash );

		for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			const auto controller = player.ptr;
			if ( !controller || !memory::read<bool>( controller + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );
			if ( !pawn || pawn == local.view_pawn( ) )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			memory::write<bool>( pawn + spotted_state_offset + spotted_offset, true );
		}
	}

	void other::do_autobuy( ) const
	{
		if ( !settings::g_misc.m_autobuy.enabled )
		{
			return;
		}

		std::string cmd {};

		switch ( settings::g_misc.m_autobuy.primary_weapon )
		{
		case 1: cmd += xs( "buy ak47; buy m4a1; " ); break;
		case 2: cmd += xs( "buy sg556; buy aug; " ); break;
		case 3: cmd += xs( "buy ssg08; " ); break;
		case 4: cmd += xs( "buy awp; " ); break;
		case 5: cmd += xs( "buy g3sg1; buy scar20; " ); break;
		}

		if ( settings::g_misc.m_autobuy.armor )
		{
			cmd += xs( "buy vesthelm; buy vest; " );
		}

		if ( settings::g_misc.m_autobuy.taser )
		{
			cmd += xs( "buy taser; " );
		}

		if ( settings::g_misc.m_autobuy.defuser )
		{
			cmd += xs( "buy defuser; " );
		}

		switch ( settings::g_misc.m_autobuy.secondary_weapon )
		{
		case 1: cmd += xs( "buy elite; " ); break;
		case 2: cmd += xs( "buy fiveseven; buy tec9; " ); break;
		case 3: cmd += xs( "buy deagle; " ); break;
		case 4: cmd += xs( "buy revolver; " ); break;
		}

		for ( auto i = 0; i < 5; ++i )
		{
			if ( !settings::g_misc.m_autobuy.grenades[ i ] )
			{
				continue;
			}

			switch ( i )
			{
			case 0: cmd += xs( "buy molotov; buy incgrenade; " ); break;
			case 1: cmd += xs( "buy hegrenade; " ); break;
			case 2: cmd += xs( "buy smokegrenade; " ); break;
			case 3: cmd += xs( "buy flashbang; " ); break;
			case 4: cmd += xs( "buy decoy; " ); break;
			}
		}

		if ( !cmd.empty( ) )
		{
			memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str( ), 0x7ffef001 );
		}
	}

	void other::do_player_alpha_changing( )
	{
		const auto local = systems::g_local.get( );

		if ( !local.pawn || !local.is_alive )
		{
			if ( this->m_is_alpha_changed && local.pawn )
			{
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}

			this->m_is_alpha_changed = false;
			return;
		}

		if ( !settings::g_esp.m_local_alpha.enabled.value )
		{
			if ( this->m_is_alpha_changed )
			{
				this->m_is_alpha_changed = false;
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}

			return;
		}

		const auto is_scoped = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto should_apply = !settings::g_esp.m_local_alpha.only_scoped.value || is_scoped;

		if ( should_apply )
		{
			this->m_is_alpha_changed = true;
			const auto alpha = static_cast< std::uint8_t >( settings::g_esp.m_local_alpha.opacity.value * 255.0f );
			memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, alpha );
		}
		else
		{
			if ( this->m_is_alpha_changed )
			{
				this->m_is_alpha_changed = false;
				memory::call<void>( PATTERN (patterns::game_event_get_string), local.pawn, 255 );
			}
		}
	}

	void other::do_name_changing( )
	{
		const auto local = systems::g_local.get( );
		const auto& cfg = settings::g_misc.m_name_changer;
		const auto enabled = cfg.clantag.value || cfg.override_name.value;

		if ( !enabled )
		{
			if ( this->m_name_changer_active && local.controller && !this->m_original_name.empty( ) )
			{
				submit_name_change( this->m_original_name );
			}

			this->m_name_changer_active = false;
			this->m_name_changer_controller = 0;
			this->m_original_name.clear( );
			this->m_last_sent_name.clear( );
			other::s_display_name.clear( );
			return;
		}

		if ( !local.controller )
		{
			return;
		}

		if ( !this->m_name_changer_active )
		{
			this->m_original_name = controller_name( local.controller );
			if ( this->m_original_name.empty( ) )
			{
				this->m_original_name = xs( "Player" );
			}

			this->m_name_changer_active = true;
			this->m_name_changer_controller = local.controller;
			this->m_last_sent_name.clear( );
		}
		else if ( this->m_name_changer_controller != local.controller )
		{
			// Keep the captured real name across map loads, where the controller may be recreated.
			this->m_name_changer_controller = local.controller;
			this->m_last_sent_name.clear( );
		}

		// Snapshot instead of binding a reference: the render thread can reassign the
		// setting at any time (menu edit, config load), and a live reference into a
		// std::string that is being reallocated is a use-after-free.
		const auto configured_name = cfg.name.snapshot( );
		const auto& base_name = ( cfg.override_name.value && !configured_name.empty( ) )
			? configured_name
			: this->m_original_name;

		std::string display_name = base_name;
		if ( cfg.clantag.value )
		{
			constexpr std::string_view tag{ "Kitty.wtf" };
			constexpr auto ticks_per_step{ 16 }; // 0.25 seconds at CS2's 64-tick interval.
			constexpr auto phase_count{ static_cast<int>( tag.size( ) * 2 ) };

			const auto global_vars = memory::safe_read<std::uintptr_t>( addresses::globals::global_vars ).value_or( 0 );
			const auto current_tick = global_vars
				? memory::safe_read<int>( global_vars + 0x44 ).value_or( 0 )
				: 0;
			auto phase = current_tick / ticks_per_step % phase_count;
			if ( phase < 0 )
			{
				phase += phase_count;
			}

			const auto reveal_index = phase <= static_cast<int>( tag.size( ) )
				? phase
				: phase_count - phase;
			const auto visible_tag = tag.substr( 0, static_cast<std::size_t>( reveal_index ) );
			if ( !visible_tag.empty( ) )
			{
				display_name.reserve( base_name.size( ) + visible_tag.size( ) + 3 );
				display_name = "[";
				display_name += visible_tag;
				display_name += "] ";
				display_name += base_name;
			}
		}
		if ( display_name == this->m_last_sent_name )
		{
			return;
		}

		submit_name_change( display_name );
		this->m_last_sent_name = std::move( display_name );
	}

	void other::do_kill_feed_preservation( )
	{
		const auto local = systems::g_local.get ();

		if (!local.pawn || !local.is_alive) {
			return;
		}

		const auto hud_element = memory::call<std::uintptr_t>(PATTERN (patterns::find_hud_element), xs( "CCSGO_HudDeathNotice" ) );
		if ( !hud_element )
		{
			return;
		}

		memory::write<float> (hud_element + 0x58, settings::g_misc.preserve_killfeed ? 1000.0f : 1.5f);

		float spawntime = memory::read<float> (local.pawn + SCHEMA ("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
		if ( m_last_spawntime != spawntime )
		{
			const auto clear_death_notices = PATTERN (patterns::hud_death_notice_clear);
			if ( clear_death_notices )
			{
				memory::call<void>( clear_death_notices, hud_element - 0x20 );
			}

			m_last_spawntime = spawntime;
		}
	}

	void other::do_viewmodel_adjust( )
	{
		const auto& cfg = settings::g_misc.m_viewmodel_adjust;

		struct entry
		{
			std::uint32_t hash;
			float value;
		};

		const entry entries[ k_vm_cvar_count ]
		{
			{ "viewmodel_offset_x"_hash, cfg.offset_x.value },
			{ "viewmodel_offset_y"_hash, cfg.offset_y.value },
			{ "viewmodel_offset_z"_hash, cfg.offset_z.value },
			{ "viewmodel_fov"_hash, cfg.fov.value },
			{ "cl_wpn_sway_scale"_hash, cfg.sway.value },
			{ "cl_bob_lower_amt"_hash, cfg.bob_lower.value },
			{ "cl_bobamt_lat"_hash, cfg.bob_lat.value },
			{ "cl_bobamt_vert"_hash, cfg.bob_vert.value },
			{ "cl_bobcycle"_hash, cfg.bob_cycle.value },
			{ "cl_viewmodel_shift"_hash, cfg.shift.value },
			{ "cl_recoil_scale"_hash, cfg.recoil_scale.value },
		};

		const auto cvar_system = addresses::globals::cvar;
		if ( !cvar_system )
		{
			return;
		}

		for ( auto i = 0u; i < k_vm_cvar_count; ++i )
		{
			auto& cache = this->m_vm_cache[ i ];

			// Convars that do not exist on this game build are skipped safely --
			// the matching slider is simply inert.
			const auto cvar = cvar_system->find( entries[ i ].hash );
			if ( !cvar )
			{
				continue;
			}

			if ( !cfg.enabled.value )
			{
				if ( cache.has_original )
				{
					cvar->m_value.fl = cache.original;
					cache.has_original = false;
				}

				cache.current = std::numeric_limits<float>::quiet_NaN( );
				continue;
			}

			if ( !cache.has_original )
			{
				cache.original = cvar->m_value.fl;
				cache.has_original = true;
			}

			if ( cache.current != entries[ i ].value )
			{
				cvar->m_value.fl = entries[ i ].value;
				cache.current = entries[ i ].value;
			}
		}
	}

	void other::on_bullet_impact_for_autofake( std::uintptr_t event, const systems::local::snapshot& local )
	{
		// Auto pitch removed - function disabled
		return;
		{
			return;
		}

		if ( !event || !local.pawn || !local.is_alive )
		{
			return;
		}

		// Get bullet impact position
		const auto x = memory::call<float>( PATTERN( patterns::game_event_get_float ), event, "x", 0.0f );
		const auto y = memory::call<float>( PATTERN( patterns::game_event_get_float ), event, "y", 0.0f );
		const auto z = memory::call<float>( PATTERN( patterns::game_event_get_float ), event, "z", 0.0f );
		
		const auto userid_key = cstypes::event_hash{ 0, "userid" };
		const auto attacker_controller = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &userid_key );

		if ( !attacker_controller || attacker_controller == local.controller )
		{
			return;
		}

		// Get attacker pawn
		const auto attacker_pawn_handle = memory::read<std::uint32_t>( attacker_controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
		if ( !attacker_pawn_handle )
		{
			return;
		}

		const auto attacker_pawn = systems::g_entities.lookup( attacker_pawn_handle );
		if ( !attacker_pawn )
		{
			return;
		}

		const math::vector3 impact_pos{ x, y, z };

		// Forward to anti-aim for processing
		features::combat::g_misc.antiaim( ).on_bullet_impact( impact_pos, attacker_pawn, local );
	}

	void other::add_evasion_log( const std::string& attacker_name )
	{
		evasion_log log;
		log.attacker_name = attacker_name;
		
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		log.time = memory::read<float>( global_vars + 0x10 );
		
		// Shorter duration for cleaner disappearance
		log.duration = 2.5f;
		
		log.offset = animation::spring{ };
		log.offset.set_target( 0.0f );
		
		log.alpha = animation::fade{ };
		log.alpha.fade_in( 0.3f );
		log.fading_out = false;

		m_evasion_logs.push_back( log );

		// Keep only last 5 logs
		if ( m_evasion_logs.size( ) > 5 )
		{
			m_evasion_logs.erase( m_evasion_logs.begin( ) );
		}
	}

	void other::render_evasion_logs( xdraw::draw_list& draw_list, float time )
	{
		if ( m_evasion_logs.empty( ) )
		{
			return;
		}

		// Get current time
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x10 );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		constexpr auto base_x_offset{ 15.0f };
		constexpr auto base_y_offset{ 200.0f }; // Stack above hit logs
		constexpr auto entry_spacing{ 4.0f };
		constexpr auto h{ 26.0f };
		constexpr auto r{ 14.0f };
		constexpr auto text_pad_x{ 12.0f };
		constexpr auto text_nudge{ 0.5f };

		// Watermark colors - glassy pink (exact same as hit logs)
		static const auto watermark_bg = xdraw::color{ 255, 182, 193, 120 };
		static const auto whitish_text = xdraw::color{ 245, 245, 245, 255 };

		auto y_offset{ 0.0f };

		for ( auto it = m_evasion_logs.begin( ); it != m_evasion_logs.end( ); )
		{
			auto& log = *it;
			const auto elapsed = current_time - log.time;

			// Smooth fade-out in the last 0.8 seconds
			constexpr auto fade_duration{ 0.8f };
			const auto fade_start = log.duration - fade_duration;
			
			// Start fading early for smoother transition
			if ( elapsed > fade_start && !log.fading_out )
			{
				log.alpha.fade_out( fade_duration );
				log.fading_out = true;
			}

			log.offset.update( );
			log.alpha.update( );

			if ( !log.snapped && log.offset.settled( ) )
			{
				log.offset.snap( 0.0f );
				log.snapped = true;
			}

			const auto alpha = log.alpha.alpha( );
			
			// Remove when fully faded
			if ( elapsed >= log.duration || alpha < 0.01f )
			{
				it = m_evasion_logs.erase( it );
				continue;
			}

			const auto slide_x = log.snapped ? 0.0f : log.offset.value( );

			if ( alpha > 0.01f )
			{
				const auto scale_alpha = [ alpha ]( xdraw::color c ) -> xdraw::color { 
					return c.alpha( static_cast<std::uint8_t>( ( c.a / 255.0f ) * alpha * 255.0f ) ); 
				};

				const auto text = std::string( "Successfully Evaded " ) + log.attacker_name + "'s Bullet";
				const auto [text_w, text_h] = xdraw::measure_text( text );
				const auto total_w = text_w + text_pad_x * 2.0f;

				// Position from bottom left
				const auto x = base_x_offset + slide_x;
				const auto y = screen_h - base_y_offset - y_offset - h;

				// Glassy background with blur - exactly like hit logs
				if ( elapsed <= fade_start )
				{
					draw_list.rect_filled_blurred( x, y, total_w, h, xdraw::corner_radius{ r } );
				}

				// Main glassy pill - pink watermark color
				draw_list.rect_filled( x, y, total_w, h, scale_alpha( watermark_bg ), xdraw::corner_radius{ r } );

				// Whitish text
				const auto tx = x + text_pad_x;
				const auto ty = y + ( h - text_h ) * 0.5f + text_nudge;
				draw_list.text( tx, ty, text, scale_alpha( whitish_text ) );

				y_offset += h + entry_spacing;
			}

			++it;
		}
	}

} // namespace features::misc
