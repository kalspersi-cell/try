#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	namespace
	{

		// How long a custom agent model may stay unresolved before the pawn is put
		// back on the model it already had. The engine either resolves the resource
		// within a frame or two or never will, so the window only exists to stop a
		// broken install from being re-written forever.
		constexpr auto k_agent_model_retry_window{ std::chrono::seconds( 5 ) };

		// A model path is requested a few times, a second apart, while the resource
		// streams in. Writing the same path on every frame instead races the resource
		// system: it never gets to finish the load, and the pawn is left showing the
		// engine's error model for as long as somebody keeps switching models.
		constexpr auto k_agent_model_request_interval{ std::chrono::seconds( 1 ) };
		constexpr auto k_agent_model_max_requests{ 3 };

	} // namespace

	void agents::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn )
		{
			return;
		}

		const auto team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto selected_def_index = ( team == 3 ) ? settings::g_changer.agents.ct_def : ( team == 2 ) ? settings::g_changer.agents.t_def : static_cast< std::int16_t >( 0 );

		const econ_item_system::item_def* selected{ nullptr };
		if ( selected_def_index != 0 )
		{
			selected = g_econ_item_system.find_def( selected_def_index );
		}

		// A custom agent (an installed third-party model) takes priority over the
		// schema agent for the pawn's team.
		std::string desired_model{};
		if ( team == 3 )
		{
			desired_model = settings::g_changer.agents.ct_model;
		}
		else if ( team == 2 )
		{
			desired_model = settings::g_changer.agents.t_model;
		}

		// A custom model is only ever written when the engine can resolve it. One that
		// is missing from the game files comes back as the engine's error model, and
		// one that never resolves inside the retry window is treated the same way: the
		// pawn keeps the agent it already had instead of being left as an error.
		const auto custom_selection = !desired_model.empty( );

		// Set while the engine is still busy with the last write, so the pawn is left
		// alone for this frame instead of being pointed at the same path again.
		auto custom_waiting = false;

		if ( custom_selection )
		{
			if ( g_custom_models.model_missing( desired_model ) )
			{
				this->m_failed_custom = desired_model;
			}

			// Picking a different model is a fresh attempt, so an earlier failure does
			// not stick to it.
			if ( !this->m_failed_custom.empty( ) && desired_model != this->m_failed_custom )
			{
				this->m_failed_custom.clear( );
				this->m_custom_pending = false;
			}

			if ( desired_model == this->m_failed_custom )
			{
				desired_model.clear( );
			}
			else if ( custom_models::model_matches( local.pawn, desired_model ) )
			{
				this->m_custom_pending = false;

				// The resource is live and the pawn is really wearing it, but its mesh
				// groups still belong to the model it had a moment ago until the game
				// gets around to rebuilding them -- and a model whose groups were never
				// rebuilt renders as the engine's error model. Asking for that rebuild
				// is the step the reference implementation does that this one did not:
				// it is what makes a custom agent survive the game's own cosmetic
				// refresh (and the switch back to a model used earlier). Once per
				// apply, after a request actually went out.
				if ( !this->m_custom_settled && this->m_custom_requests > 0 )
				{
					this->m_custom_settled = true;
					memory::write<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash ), true );
				}
			}
			else
			{
				const auto now = std::chrono::steady_clock::now( );

				if ( !this->m_custom_pending )
				{
					this->m_custom_pending = true;
					this->m_custom_started = now;
					this->m_custom_requests = 0;
					// A new model has to settle again even if the previous one did.
					this->m_custom_settled = false;
				}

				if ( now - this->m_custom_started > k_agent_model_retry_window )
				{
					this->m_failed_custom = desired_model;
					desired_model.clear( );
				}
				else if ( this->m_custom_requests >= k_agent_model_max_requests )
				{
					// Out of requests: let the window run out so the pawn falls back to the
					// agent it already had instead of being written again.
					custom_waiting = true;
				}
				else if ( this->m_custom_requests > 0
					&& now - this->m_custom_last_request < k_agent_model_request_interval
					&& !custom_models::model_ready( desired_model ) )
				{
					// The interval is the fallback spacing, not the rule. A path the resource
					// system already holds is written the moment it is live instead of a second
					// later, which is what keeps the engine from resolving the write to the
					// error model while the request it was registered under is still
					// streaming. A request that cannot be brought forward still goes out when
					// the interval expires, so a resource system that never answers for a path
					// only costs the original spacing.
					custom_waiting = true;
				}
				else
				{
					++this->m_custom_requests;
					this->m_custom_last_request = now;
				}
			}
		}
		else
		{
			this->m_failed_custom.clear( );
			this->m_custom_pending = false;
		}

		if ( desired_model.empty( ) && selected )
		{
			desired_model = selected->model_player;
		}

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;
			this->m_applied_handle = 0;
			this->m_applied_def = 0;
			this->m_applied_custom.clear( );
			this->m_failed_custom.clear( );
			this->m_custom_pending = false;
			this->m_custom_requests = 0;
			this->m_custom_last_request = {};
			this->m_custom_settled = false;
			this->m_tracked_team = 0;
			this->m_tracked_pawn = local.pawn;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );

		if ( desired_model.empty( ) )
		{
			if ( this->m_overridden && !this->m_original_model.empty( ) )
			{
				// The stock model is resident already, but registering it is what keeps a
				// restore from landing on the engine's error model if the catalog changed.
				custom_models::precache_model( this->m_original_model );

				memory::try_call<void>(PATTERN (patterns::set_player_model), local.pawn, this->m_original_model.c_str( ) );

				//const auto controller = memory::read<std::uintptr_t>( local.pawn + 0x3268 );
				//if ( controller )
				//{
				//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
				//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
				//}

				this->cycle_weapon_owners( local.pawn );

				this->m_applied_handle = 0;
				this->m_applied_def = 0;
				this->m_applied_custom.clear( );
				this->m_tracked_team = 0;
				this->m_overridden = false;
			}
			return;
		}

		if ( !this->m_overridden && this->m_original_model.empty( ) )
		{
			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		// The engine resolves a model path asynchronously, and pointing the pawn at a
		// path that is still streaming in is what produces the error model, so the
		// write happens once per request rather than once per frame.
		if ( custom_waiting )
		{
			return;
		}

		const auto applied_def = selected ? selected->def_index : static_cast< std::int16_t >( 0 );
		const auto current_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		const auto selection_matches = ( this->m_applied_def == applied_def && this->m_applied_custom == desired_model );
		const auto team_matches = ( this->m_tracked_team == team );
		const auto handle_matches = ( this->m_applied_handle != 0 && current_handle == this->m_applied_handle );

		// A pending custom model has to be re-applied, so it cannot take the
		// "already applied" exit even though the handle looks settled.
		if ( this->m_overridden && selection_matches && team_matches && handle_matches && !this->m_custom_pending )
		{
			return;
		}

		if ( this->m_overridden && !team_matches )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;
			this->m_applied_custom.clear( );

			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		// An agent model is resolved by path exactly like a weapon model, so an
		// unregistered path is what makes a custom agent show up as the error model.
		custom_models::precache_model( desired_model );

		memory::try_call<void>(PATTERN (patterns::set_player_model), local.pawn, desired_model.c_str( ) );

		//const auto controller = memory::read<std::uintptr_t>( local_pawn + 0x3268 );
		//if ( controller )
		//{
		//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
		//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
		//}

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

		this->cycle_weapon_owners( local.pawn );

		this->m_applied_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		this->m_applied_def = applied_def;
		this->m_applied_custom = desired_model;
		this->m_tracked_team = team;
		this->m_overridden = true;
	}

	void agents::cycle_weapon_owners( std::uintptr_t pawn )
	{
		const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto owner_off = SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash );
			const auto saved_owner = memory::read<std::uint32_t>( weapon + owner_off );

			memory::write<std::uint32_t>( weapon + owner_off, 0xffffffff );
			memory::write<std::uint32_t>( weapon + owner_off, saved_owner );
		}
	}

} // namespace features::changer