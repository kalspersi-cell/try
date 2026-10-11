#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
namespace features::changer {

	namespace detail {

		inline static std::uint32_t murmurhash2_lower( const char* str, int len, std::uint32_t seed )
		{
			constexpr auto m{ 0x5bd1e995 };
			constexpr auto r{ 24 };

			auto h = seed ^ len;
			auto i{ 0 };

			while ( len >= 4 )
			{
				auto k =
					static_cast< std::uint32_t >( ( str[ i ] >= 'A' && str[ i ] <= 'Z' ) ? str[ i ] + 32 : str[ i ] ) |
					( static_cast< std::uint32_t >( ( str[ i + 1 ] >= 'A' && str[ i + 1 ] <= 'Z' ) ? str[ i + 1 ] + 32 : str[ i + 1 ] ) << 8 ) |
					( static_cast< std::uint32_t >( ( str[ i + 2 ] >= 'A' && str[ i + 2 ] <= 'Z' ) ? str[ i + 2 ] + 32 : str[ i + 2 ] ) << 16 ) |
					( static_cast< std::uint32_t >( ( str[ i + 3 ] >= 'A' && str[ i + 3 ] <= 'Z' ) ? str[ i + 3 ] + 32 : str[ i + 3 ] ) << 24 );

				k *= m;
				k ^= k >> r;
				k *= m;

				h *= m;
				h ^= k;

				i += 4;
				len -= 4;
			}

			switch ( len )
			{
			case 3: h ^= static_cast< std::uint32_t >( ( str[ i + 2 ] >= 'A' && str[ i + 2 ] <= 'Z' ) ? str[ i + 2 ] + 32 : str[ i + 2 ] ) << 16; [[fallthrough]];
			case 2: h ^= static_cast< std::uint32_t >( ( str[ i + 1 ] >= 'A' && str[ i + 1 ] <= 'Z' ) ? str[ i + 1 ] + 32 : str[ i + 1 ] ) << 8; [[fallthrough]];
			case 1: h ^= static_cast< std::uint32_t >( ( str[ i ] >= 'A' && str[ i ] <= 'Z' ) ? str[ i ] + 32 : str[ i ] ); h *= m;
			}

			h ^= h >> 13;
			h *= m;
			h ^= h >> 15;

			return h;
		}

		inline static std::uint32_t make_subclass_token( std::int16_t def_index )
		{
			const auto s = std::to_string( def_index );
			return murmurhash2_lower( s.c_str( ), static_cast< int >( s.length( ) ), 0x31415926 );
		}

		// Copies a NUL-terminated engine string with fault-checked reads. A stale
		// pointer then yields an empty string instead of taking the process down.
		[[nodiscard]] inline std::string safe_string( const char* ptr, std::size_t max_length = 260 )
		{
			if ( !ptr )
			{
				return {};
			}

			std::string out{};
			out.reserve( 64 );

			for ( std::size_t i = 0; i < max_length; ++i )
			{
				const auto c = memory::safe_read<char>( reinterpret_cast< std::uintptr_t >( ptr ) + i );
				if ( !c || *c == '\0' )
				{
					break;
				}

				out.push_back( *c );
			}

			return out;
		}

		// Changing the item definition index destroys and rebuilds both the world model
		// and the first-person view model. Two of those inside a couple of frames -- a
		// config load landing next to a menu click, or a fast knife swap -- tore the
		// scene graph apart while the HUD walk was still using it. Only one model change
		// is allowed per window; the caller retries on the next frame.
		inline constexpr auto k_model_change_cooldown{ std::chrono::milliseconds( 150 ) };

	} // namespace detail

	namespace {

		[[nodiscard]] bool model_change_allowed( std::chrono::steady_clock::time_point& last )
		{
			const auto now = std::chrono::steady_clock::now( );
			if ( last.time_since_epoch( ).count( ) != 0 && now - last < detail::k_model_change_cooldown )
			{
				return false;
			}

			last = now;
			return true;
		}

	} // namespace

	void knives::on_frame_stage_notify( )
	{
		this->process_hud_clear( );

		// If the composite material was null during a previous rebuild_paint call
		// (round start, first spawn), retry the paint-only update now that the
		// material may have been populated by the engine.
		if ( this->m_pending_paint_retry && this->m_pending_paint_weapon )
		{
			const auto composite_material = memory::safe_read<std::uintptr_t>( this->m_pending_paint_weapon + 0x608 ).value_or( 0 );
			if ( composite_material )
			{
				memory::try_call<void>( PATTERN( patterns::weapon_update_composite_material ), this->m_pending_paint_weapon + 0x608, true );
				memory::try_call_vfunc<void>( this->m_pending_paint_weapon, 10, 1 );
				memory::try_call<void>( PATTERN( patterns::weapon_update_skin ), this->m_pending_paint_weapon, true );
				this->m_pending_paint_retry = false;
				this->m_pending_paint_weapon = 0;
			}
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn || !local.controller )
		{
			return;
		}

		const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		// Snapshot the selection under the shared lock. Holding a pointer into the
		// map while the render thread inserts or erases is what used to crash when
		// swapping knives.
		settings::changer::applied_skin selected_skin_copy{};
		const settings::changer::applied_skin* selected_skin{ nullptr };
		const econ_item_system::item_def* selected_knife_def{ nullptr };

		{
			std::shared_lock lock( settings::g_changer.skins.mtx );

			for ( const auto& [def_idx, skin] : settings::g_changer.skins.data )
			{
				const auto def = g_econ_item_system.find_def( def_idx );
				if ( !def || def->category != econ_item_system::item_category::knife )
				{
					continue;
				}

				selected_skin_copy = skin;
				selected_skin = &selected_skin_copy;
				selected_knife_def = def;
				break;
			}
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		const auto active_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		const auto active_weapon = systems::g_entities.lookup( active_handle );

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_original = {};
			this->m_overridden = false;
			this->m_last_active_handle = 0;
			this->m_tracked_weapon = 0;
			this->m_last_model_change = {};
			this->m_pending_paint_retry = false;
			this->m_pending_paint_weapon = 0;
			this->m_custom_pending = false;
			this->m_custom_requests = 0;
			this->m_custom_last_request = {};
			this->m_custom_failed_path.clear( );
			this->m_tracked_pawn = local.pawn;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto iv = weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
			const auto current_def_index = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
			const auto current_def = g_econ_item_system.find_def( static_cast< std::int16_t >( current_def_index ) );

			if ( !current_def || current_def->category != econ_item_system::item_category::knife )
			{
				continue;
			}

			if ( !memory::safe_read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 ).value_or( 0 ) )
			{
				continue;
			}

			// The captured "original" belongs to one entity. If the engine handed us a
			// different knife (respawn, pickup, entity re-creation) the snapshot is
			// meaningless and restoring from it would write a def index that no longer
			// matches this weapon.
			if ( this->m_tracked_weapon != weapon )
			{
				this->m_original = {};
				this->m_overridden = false;
				this->m_pending_paint_retry = false;
				this->m_pending_paint_weapon = 0;
				this->m_tracked_weapon = weapon;
			}

			if ( !selected_knife_def )
			{
				this->restore( weapon, iv, active_weapon, local.pawn );
				break;
			}

			if ( !this->m_original.captured )
			{
				// Only adopt an original once the item view actually names a knife. An
				// unpopulated view (def index 0, or a gun while the knife is mid-swap)
				// would later be written back and looked up in the item schema, where a
				// missing definition is a null dereference inside the engine.
				const auto original_def = g_econ_item_system.find_def( static_cast< std::int16_t >( current_def_index ) );
				if ( !original_def || original_def->category != econ_item_system::item_category::knife )
				{
					break;
				}

				this->capture_original( weapon, iv );

				// A freshly equipped knife used to stop here and wait for the
				// next frame; that frame took the "already matches" exit on the
				// def index, so only the model was ever written and the skin
				// never appeared. Apply the full skin on the same pass.
				const auto steam_id_now = memory::read<std::uintptr_t>( local.controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
				static_cast<void>( this->apply( weapon, iv, selected_knife_def, selected_skin, static_cast< std::uint32_t >( steam_id_now & 0xffffffff ), active_weapon, local.pawn, true ) );
			}

			const auto current_pk = memory::read<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ) );
			const auto def_matches = current_def_index == static_cast< std::uint16_t >( selected_knife_def->def_index );

			if ( def_matches && current_pk == selected_skin->paint_kit_id )
			{
				break;
			}

			const auto steam_id = memory::read<std::uintptr_t>( local.controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
			const auto account_id = static_cast< std::uint32_t >( steam_id & 0xffffffff );

			this->apply( weapon, iv, selected_knife_def, selected_skin, account_id, active_weapon, local.pawn, !def_matches );
			break;
		}

		if ( active_handle != this->m_last_active_handle )
		{
			this->m_last_active_handle = active_handle;

			if ( this->m_overridden && active_weapon )
			{
				const auto iv = active_weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
				const auto def_index = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
				const auto def = g_econ_item_system.find_def( static_cast< std::int16_t >( def_index ) );

				if ( def && def->category == econ_item_system::item_category::knife )
				{
					const auto paint_kit_id = memory::read<int>( active_weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ) );
					const auto pk = g_econ_item_system.find_paint_kit( paint_kit_id );
					this->update_view_model( local.pawn, pk );

					// The freshly equipped knife's HUD clone is re-pointed at the new weapon
					// on every switch, so it is re-selected here as well.
					this->update_view_model( local.pawn, pk );
				}
			}
		}

		this->apply_custom_model( local.pawn, active_weapon );
	}

	// A custom knife model from the models page. It is written to the first-person
	// weapon entity rather than to the weapon itself: that entity is what the view
	// renders, it owns no item definition or subclass, and touching those is what
	// used to leave the game drawing one weapon as another.
	void knives::apply_custom_model( std::uintptr_t pawn, std::uintptr_t active_weapon )
	{
		const auto& desired = settings::g_changer.agents.weapon_model;

		if ( desired.empty( ) )
		{
			this->m_custom_pending = false;
			this->m_custom_requests = 0;
			this->m_custom_failed_path.clear( );
			return;
		}

		if ( this->m_custom_failed_path == desired )
		{
			return;
		}

		// Only while a knife is actually out. The entity renders every weapon, so
		// writing a knife model while a rifle is held would draw the rifle as a
		// knife -- exactly the bug this guard exists to prevent.
		if ( !active_weapon )
		{
			return;
		}

		const auto active_iv = active_weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
		const auto active_def_index = memory::read<std::uint16_t>( active_iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		const auto active_def = g_econ_item_system.find_def( static_cast< std::int16_t >( active_def_index ) );

		if ( !active_def || active_def->category != econ_item_system::item_category::knife )
		{
			this->m_custom_pending = false;
			return;
		}

		const auto view_model = detail::find_hud_model_weapon( pawn );
		if ( !view_model )
		{
			return;
		}

		if ( custom_models::model_matches( view_model, desired ) )
		{
			this->m_custom_pending = false;
			return;
		}

		constexpr auto k_window{ std::chrono::seconds( 5 ) };
		constexpr auto k_interval{ std::chrono::seconds( 1 ) };
		constexpr auto k_max_requests{ 3 };

		const auto now = std::chrono::steady_clock::now( );

		if ( !this->m_custom_pending )
		{
			this->m_custom_pending = true;
			this->m_custom_started = now;
			this->m_custom_requests = 0;
		}

		if ( now - this->m_custom_started > k_window )
		{
			this->m_custom_failed_path = desired;
			this->m_custom_pending = false;
			return;
		}

		// The interval is the fallback spacing, not the rule: a path the resource
		// system already holds is written the moment it is live, so the view model is
		// not pointed at a model the engine cannot resolve yet (which is what draws
		// the error model in place of the knife). A request that cannot be brought
		// forward still goes out when the interval expires.
		if ( this->m_custom_requests >= k_max_requests
			|| ( this->m_custom_requests > 0
				&& now - this->m_custom_last_request < k_interval
				&& !custom_models::model_ready( desired ) ) )
		{
			return;
		}

		++this->m_custom_requests;
		this->m_custom_last_request = now;

		custom_models::precache_model( desired );

		memory::try_call<void>( PATTERN( patterns::set_player_model ), view_model, desired.c_str( ) );

		// A custom model carries one mesh group. Leaving the previous model's mask
		// in place makes the engine look for groups this model does not have, which
		// is another way the error model appears.
		const auto view_model_scene_node = memory::safe_read<std::uintptr_t>( view_model + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( view_model_scene_node )
		{
			memory::try_call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), view_model_scene_node, std::uint64_t{ 1 } );
		}
	}

	void knives::capture_original( std::uintptr_t weapon, std::uintptr_t iv )
	{
		if ( this->m_original.captured )
		{
			return;
		}

		this->m_original.def_index = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		this->m_original.id_high = memory::read<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ) );
		this->m_original.id_low = memory::read<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ) );
		this->m_original.account_id = memory::read<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ) );
		this->m_original.initialized = memory::read<bool>( iv + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ) );
		this->m_original.paint_kit = memory::read<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ) );
		this->m_original.seed = memory::read<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackSeed"_hash ) );
		this->m_original.wear = memory::read<float>( weapon + SCHEMA( "C_EconEntity", "m_flFallbackWear"_hash ) );
		this->m_original.stattrak = memory::read<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackStatTrak"_hash ) );
		this->m_original.captured = true;
	}

	void knives::apply( std::uintptr_t weapon, std::uintptr_t iv, const econ_item_system::item_def* def, const settings::changer::applied_skin* skin, std::uint32_t account_id, std::uintptr_t active_weapon, std::uintptr_t pawn, bool change_model )
	{
		// A pure skin swap on the knife that is already equipped does not need the
		// definition index or the subclass token touched at all -- only the paint
		// fields. Skipping the rebuild removes the crash window and the visible model
		// pop for what is supposed to be a material change.
		if ( change_model && !model_change_allowed( this->m_last_model_change ) )
		{
			return;
		}

		this->m_pending_hud_iv = 0;

		if ( change_model )
		{
			memory::write<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ), static_cast< std::uint16_t >( def->def_index ) );
		}

		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), 0xf0000000 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), 0x10 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), account_id );
		memory::write<bool>( iv + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), true );

		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ), skin->paint_kit_id );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackSeed"_hash ), skin->seed );
		memory::write<float>( weapon + SCHEMA( "C_EconEntity", "m_flFallbackWear"_hash ), skin->wear );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackStatTrak"_hash ), skin->stattrak ? 0 : -1 );

		const auto pk = g_econ_item_system.find_paint_kit( skin->paint_kit_id );

		if ( change_model )
		{
			this->update_model( weapon, iv, static_cast< std::uint16_t >( def->def_index ) );
		}

		this->rebuild_paint( weapon, active_weapon, pawn, pk );
		this->schedule_hud_clear( iv );

		this->m_overridden = true;
	}

	void knives::restore( std::uintptr_t weapon, std::uintptr_t iv, std::uintptr_t active_weapon, std::uintptr_t pawn )
	{
		if ( !this->m_overridden || !this->m_original.captured )
		{
			return;
		}

		// Never write back a def index the item schema cannot resolve: the engine's
		// model lookup that follows dereferences the definition it finds, so a stale
		// capture (an item view that had not been populated yet) used to crash here.
		const auto original_def = g_econ_item_system.find_def( static_cast< std::int16_t >( this->m_original.def_index ) );
		if ( !original_def || original_def->category != econ_item_system::item_category::knife )
		{
			this->m_original = {};
			this->m_overridden = false;
			return;
		}

		const auto current_def = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		const auto change_model = current_def != this->m_original.def_index;

		if ( change_model && !model_change_allowed( this->m_last_model_change ) )
		{
			return;
		}

		this->m_pending_hud_iv = 0;

		if ( change_model )
		{
			memory::write<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ), this->m_original.def_index );
		}

		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), this->m_original.id_high );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), this->m_original.id_low );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), this->m_original.account_id );
		memory::write<bool>( iv + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), this->m_original.initialized );

		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ), this->m_original.paint_kit );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackSeed"_hash ), this->m_original.seed );
		memory::write<float>( weapon + SCHEMA( "C_EconEntity", "m_flFallbackWear"_hash ), this->m_original.wear );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackStatTrak"_hash ), this->m_original.stattrak );

		const auto pk = g_econ_item_system.find_paint_kit( this->m_original.paint_kit );

		if ( change_model )
		{
			this->update_model( weapon, iv, this->m_original.def_index );
		}

		this->rebuild_paint( weapon, active_weapon, pawn, pk );
		this->schedule_hud_clear( iv );

		this->m_overridden = false;
	}

	void knives::update_model( std::uintptr_t weapon, std::uintptr_t iv, std::uint16_t def_index )
	{
		const auto token = detail::make_subclass_token( static_cast< std::int16_t >( def_index ) );

		memory::write<std::uint32_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ), token );
		memory::try_call<void>(PATTERN (patterns::weapon_get_viewmodel), weapon );

		const auto model_path_ptr = memory::try_call<const char*>(PATTERN (patterns::weapon_get_model_path), iv );

		// Only hand the engine a path that survived a bounded, fault-checked read and
		// actually names a model. set_player_model resolves the string by path, so a
		// truncated or bogus pointer used to walk off the end of it.
		const auto model_path = detail::safe_string( model_path_ptr );
		if ( model_path.find( ".vmdl" ) == std::string::npos )
		{
			return;
		}

		memory::try_call<void>(PATTERN (patterns::set_player_model), weapon, model_path.c_str( ) );
	}

	void knives::rebuild_paint( std::uintptr_t weapon, std::uintptr_t active_weapon, std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		const auto is_legacy = pk && pk->legacy_model;
		const auto mesh_group = is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 };

		if ( weapon == active_weapon )
		{
			this->update_view_model( pawn, pk );
		}

		const auto weapon_scene_node = memory::safe_read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( weapon_scene_node )
		{
			memory::try_call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), weapon_scene_node, mesh_group );
		}

		// 0x608 is the weapon's composite material handle. A weapon that has only just
		// been created -- round start, first spawn, a fresh pickup -- still has it null,
		// and the engine function dereferences it unconditionally. That null read is
		// the crash the fatal handler caught at client.dll+0x14E47E0, with rcx/rdi
		// pointing straight at weapon+0x608. Nothing to rebuild yet: schedule a retry
		// so the skin appears without needing a team switch.
		const auto composite_material = memory::safe_read<std::uintptr_t>( weapon + 0x608 ).value_or( 0 );
		if ( composite_material )
		{
			memory::try_call<void>( PATTERN( patterns::weapon_update_composite_material ), weapon + 0x608, true );
			this->m_pending_paint_retry = false;
			this->m_pending_paint_weapon = 0;
		}
		else
		{
			// Composite material not yet populated; retry on the next frame.
			this->m_pending_paint_retry = true;
			this->m_pending_paint_weapon = weapon;
		}

		memory::try_call_vfunc<void>( weapon, 10, 1 );
		memory::try_call<void>( PATTERN( patterns::weapon_update_skin ), weapon, true );
	}

	void knives::update_view_model( std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		const auto view_model = detail::find_hud_model_weapon( pawn );
		if ( !view_model )
		{
			return;
		}

		const auto view_model_scene_node = memory::safe_read<std::uintptr_t>( view_model + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( !view_model_scene_node )
		{
			return;
		}

		const auto is_legacy = pk && pk->legacy_model;
		memory::try_call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), view_model_scene_node, is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 } );
	}

	void knives::clear_hud_icon( std::uintptr_t iv )
	{
		const auto invalidate = PATTERN( patterns::econ_item_view_invalidate_description );
		if ( iv && invalidate )
		{
			memory::try_call<void>( invalidate, iv );
		}
	}

	void knives::schedule_hud_clear( std::uintptr_t iv )
	{
		this->clear_hud_icon( iv );
		this->m_pending_hud_iv = 0;
	}

	void knives::process_hud_clear( )
	{
		this->m_pending_hud_iv = 0;
	}

} // namespace features::changer
