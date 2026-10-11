#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::combat {

	void misc::antiaim::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_antiaim_active = false;

		if ( !settings::g_combat.m_antiaim.enabled.value )
		{
			return;
		}

		if ( systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}

		if ( settings::g_combat.m_antiaim.manual_left.value && settings::g_combat.m_antiaim.manual_right.value )
		{
			settings::g_combat.m_antiaim.manual_right.value = false;
			settings::g_combat.m_antiaim.manual_right.bind.active = false;
		}

		const auto local = systems::g_local.get( );

		if ( settings::g_combat.m_antiaim.manual_left.value )
		{
			this->m_yaw_side = -1;
		}
		else if ( settings::g_combat.m_antiaim.manual_right.value )
		{
			this->m_yaw_side = 1;
		}
		else if ( settings::g_combat.m_antiaim.freestand.value )
		{
			// Freestand picks the side, so the manual keys stay the override and
			// holding one of them immediately wins over the automatic choice.
			const auto scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			const auto origin = scene_node ? memory::read<math::vector3>( scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) ) : math::vector3{};

			this->m_freestand_result = this->evaluate_freestand_side( origin, local );
			this->m_yaw_side = this->m_freestand_result;
		}
		else
		{
			this->m_yaw_side = 0;
			this->m_freestand_result = 0;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto& ctx = g_shared.ctx( );

		if ( cmd->buttons.value & cstypes::command_buttons::in_use )
		{
			return;
		}

		if ( ctx.weapon_type == cstypes::weapon_type::grenade )
		{
			if ( memory::read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) ) > 0.0f )  // bail regardless of pin state
			{
				return;
			}
		}

		const auto move_type = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		if ( this->is_near_ladder( local.pawn ) )
		{
			return;
		}

		this->m_old_angles = view_angles;
		this->m_antiaim_active = true;

		this->m_modified_angles = this->m_old_angles;
		this->m_modified_angles.x = this->get_pitch( this->m_old_angles.x );
		this->m_modified_angles.y = this->get_yaw( this->m_old_angles, local );

		// Auto pitch removed for better performance

		math::helpers::normalize_angles( this->m_modified_angles );

		base->mutable_viewangles( )->set_x( this->m_modified_angles.x );
		base->mutable_viewangles( )->set_y( this->m_modified_angles.y );
		base->mutable_viewangles( )->set_z( this->m_modified_angles.z );

		this->m_should_correct = true;

		this->correct_movement( cmd );
	}

	// Direction indicator: an arc that shows where the anti-aim yaw points, plus
	// an optional ghost arc for the yaw the camera is actually looking at.
	//
	// Drawn in screen space around the player's projected feet instead of as
	// geometry lying on the world ground plane. Ground-plane geometry collapses
	// into an unreadable sliver at normal FPS view angles and pops in and out as
	// individual corners cross behind the camera, which is what made the old
	// indicator look so bad. Screen space keeps a constant, legible shape that
	// stays put no matter where the camera looks.
	void misc::antiaim::on_render( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_antiaim;

		if ( !cfg.enabled.value || !cfg.direction_indicator.value )
		{
			return;
		}

		if ( !this->m_antiaim_active )
		{
			return;
		}

		const auto opacity = std::clamp( cfg.direction_indicator_opacity.value, 0.0f, 100.0f ) / 100.0f;
		if ( opacity <= 0.0f )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		if ( screen_w <= 0 || screen_h <= 0 )
		{
			return;
		}

		// Anchor on the player's feet when the camera can see them, otherwise fall
		// back to a fixed spot so the marker never disappears entirely.
		auto center = math::vector2{ static_cast< float >( screen_w ) * 0.5f, static_cast< float >( screen_h ) * 0.62f };

		if ( systems::g_frame_data.valid( ) )
		{
			const auto feet = systems::g_view.project( systems::g_frame_data.origin( ) );
			if ( systems::g_view.projection_valid( feet ) )
			{
				center = feet;
			}
		}

		// Breathing scale for the whole marker.
		auto pulse = 1.0f;
		if ( cfg.direction_indicator_pulse.value )
		{
			const auto t = static_cast< float >( GetTickCount64( ) ) * 0.001f * std::max( cfg.direction_indicator_pulse_speed.value, 0.05f );
			pulse = 0.82f + 0.18f * std::sinf( t * 6.2831853f );
		}

		const auto radius = cfg.direction_indicator_radius.value;
		const auto yaw_offset = cfg.direction_indicator_offset.value * ( std::numbers::pi_v<float> / 180.0f );
		const auto outline = std::max( cfg.direction_indicator_outline.value, 0.0f );
		const auto glow_on = cfg.direction_indicator_glow.value;
		const auto glow_strength = std::clamp( cfg.direction_indicator_glow_strength.value, 0.0f, 1.0f );
		// The marker rotates with the camera: a world yaw becomes the screen-space
		// angle between it and the view direction, so 0 points up the screen.
		const auto view_yaw = systems::g_view.angles( ).y;
		const auto screen_angle_for = [ & ]( float world_yaw )
			{
				return ( world_yaw - view_yaw ) * ( std::numbers::pi_v<float> / 180.0f );
			};

		const auto glow_seg = [ & ]( const math::vector2& a, const math::vector2& b, const xdraw::color& col, float thickness )
			{
				if ( !glow_on )
				{
					return;
				}

				auto& glow = xdraw::get_glow( );
				const float seg[ ]{ a.x, a.y, b.x, b.y };
				const xdraw::color cols[ ]{ col, col };
				glow.polyline_gradient( seg, cols, false, thickness * ( 1.4f + glow_strength * 1.4f ) );
			};

		const auto draw_shape = [ & ]( float world_yaw, const xdraw::color& src, float alpha_scale, float size_scale )
			{
				const auto a = std::clamp( static_cast< float >( src.a ) * opacity * alpha_scale, 0.0f, 255.0f );
				if ( a < 1.0f )
				{
					return;
				}

				const auto col = src.alpha( static_cast< std::uint8_t >( a ) );
				const auto yaw = screen_angle_for( world_yaw ) + yaw_offset;

				{
					// Soft fading arc: brightest at the centre, tapering off at both ends.
					// Sweep and radius both scale with size_scale so the ghost marker can
					// sit inside the real one without looking like a different widget.
					constexpr auto arc_segments{ 48 };
					const auto arc_sweep_rad = 60.0f * ( std::numbers::pi_v<float> / 180.0f );
					const auto half_sweep = arc_sweep_rad * 0.5f;
					const auto arc_radius = radius * size_scale;

					std::vector<math::vector2> pts;
					pts.reserve( arc_segments + 1 );

					for ( auto i = 0; i <= arc_segments; ++i )
					{
						const auto t = static_cast< float >( i ) / arc_segments;
						const auto ang = yaw - half_sweep + arc_sweep_rad * t;

						pts.push_back( math::vector2{ center.x + std::sinf( ang ) * arc_radius, center.y - std::cosf( ang ) * arc_radius } );
					}

					const auto total = static_cast< float >( pts.size( ) - 1 );
					const auto max_thickness = std::max( outline, 1.5f );

					const auto fade_at = [ ]( std::size_t idx, float total ) -> float
						{
							const auto frac = static_cast< float >( idx ) / total;
							const auto edge = 1.0f - std::fabsf( frac - 0.5f ) * 2.0f;
							return edge * edge * edge * ( edge * ( edge * 6.0f - 15.0f ) + 10.0f );
						};

					for ( auto i = 0ull; i + 1 < pts.size( ); ++i )
					{
						const auto f0 = fade_at( i, total );
						const auto f1 = fade_at( i + 1, total );

						const auto a0 = static_cast< std::uint8_t >( a * std::fmaxf( f0, 0.05f ) );
						const auto a1 = static_cast< std::uint8_t >( a * std::fmaxf( f1, 0.05f ) );
						const auto thickness = max_thickness * ( ( f0 + f1 ) * 0.5f * 0.85f + 0.15f );

						const float seg[ ]{ pts[ i ].x, pts[ i ].y, pts[ i + 1 ].x, pts[ i + 1 ].y };
						const xdraw::color cols[ ]{ col.alpha( a0 ), col.alpha( a1 ) };

						glow_seg( pts[ i ], pts[ i + 1 ], col.alpha( static_cast< std::uint8_t >( a0 * glow_strength ) ), thickness );
						draw_list.polyline_gradient( seg, cols, false, thickness );
					}

					return;
				}
			};

		// Freestand: mark which side was chosen so it is obvious why the body is
		// leaning the way it is without having to watch the model.
		if ( cfg.freestand.value && cfg.freestand_visualize.value && this->m_freestand_result != 0 )
		{
			const auto side_yaw = this->m_old_angles.y + ( this->m_freestand_result < 0 ? -90.0f : 90.0f );
			draw_shape( side_yaw, cfg.direction_indicator_fake_color.value, 0.55f, 0.55f );
		}

		// Ghost arc: the view direction the camera is aiming along, so the gap
		// between camera and body is visible at a glance.
		if ( cfg.direction_indicator_fake.value )
		{
			draw_shape( this->m_old_angles.y, cfg.direction_indicator_fake_color.value, 0.7f, 0.72f );
		}

		draw_shape( this->m_real_yaw, cfg.direction_indicator_color.value, 1.0f, 1.0f );
	}

	float misc::antiaim::get_pitch( float view_pitch )
	{
		auto pitch = view_pitch;
		
		switch ( settings::g_combat.m_antiaim.pitch )
		{
		case settings::combat::antiaim::pitch_mode::none:
			// Keep current pitch
			break;
		case settings::combat::antiaim::pitch_mode::down:
			pitch = 89.0f;
			break;
		case settings::combat::antiaim::pitch_mode::up:
			pitch = -89.0f;
			break;
		}
		
		return pitch;
	}

	float misc::antiaim::get_yaw( const math::vector3& view_angles, const systems::local::snapshot& local )
	{
		this->m_desync_offset = 0.0f;
		// BASE ANTI-AIM: Body ALWAYS faces backward (180 degrees from camera view)
		// body_yaw = view_yaw + 180 => head always hidden behind back
		// As camera rotates right/left, body rotates in sync smoothly.
		constexpr auto backward_offset{ 180.0f };

		const auto view_yaw = view_angles.y;
		auto base_yaw = view_yaw + backward_offset; // Body always opposite to camera

		const auto local_game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto local_origin = memory::read<math::vector3>( local_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

		// ENHANCED ANTI-AIM: Backstab avoidance with better detection
		if ( settings::g_combat.m_antiaim.avoid_backstab.value )
		{
			const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
			const auto eye_pos = local_origin + memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
			constexpr auto backstab_range_sq = 400.0f * 400.0f; // Increased range
			auto knife_dist = std::numeric_limits<float>::max( );
			auto knife_yaw{ 0.0f };
			auto knife_found{ false };

			for ( const auto& p : players )
			{
				if ( !p.ptr || p.ptr == local.controller )
				{
					continue;
				}

				if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
				{
					continue;
				}

				const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
				const auto pawn = systems::g_entities.lookup( pawn_handle );

				if ( !pawn || pawn == local.pawn )
				{
					continue;
				}

				const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
				if ( !local.is_this_other_team( team ) )
				{
					continue;
				}

				const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
				if ( health <= 0 )
				{
					continue;
				}

				const auto enemy_game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !enemy_game_scene_node )
				{
					continue;
				}

				const auto enemy_origin = memory::read<math::vector3>( enemy_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto dx = enemy_origin.x - local_origin.x;
				const auto dy = enemy_origin.y - local_origin.y;
				const auto dist_sq = dx * dx + dy * dy;

				if ( dist_sq > backstab_range_sq )
				{
					continue;
				}

				const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
				if ( !weapon_services )
				{
					continue;
				}

				const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
				if ( !weapon_handle )
				{
					continue;
				}

				const auto weapon = systems::g_entities.lookup( weapon_handle );
				if ( !weapon )
				{
					continue;
				}

				const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
				if ( !weapon_vdata )
				{
					continue;
				}

				const auto weapon_type = memory::read<std::uint32_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
				if ( weapon_type != cstypes::weapon_type::knife )
				{
					continue;
				}

				if ( dist_sq < knife_dist )
				{
					knife_dist = dist_sq;
					knife_yaw = std::atan2f( dy, dx ) * ( 180.0f / std::numbers::pi_v<float> );
					knife_found = true;
				}
			}

			if ( knife_found )
			{
				// Face knife attacker: front toward attacker means back is protected.
				// knife_yaw points from us TO attacker. Setting yaw = knife_yaw makes
				// our front face them, so our back faces away (protected from backstab).
				auto bsyaw = knife_yaw;
				if ( this->m_yaw_side == -1 )
					bsyaw -= 90.0f;
				else if ( this->m_yaw_side == 1 )
					bsyaw += 90.0f;

				math::helpers::normalize_angle( bsyaw );
				this->m_real_yaw = bsyaw;
				return bsyaw;
			}
		}

		// BACK-TO-ENEMY: when an enemy is close, spin the body around so the head
		// faces AWAY from them and can't be shot. We point our yaw directly away
		// from the nearest threat instead of relying on force left / force right.
		if ( settings::g_combat.m_antiaim.near_enemy_turn.value )
		{
			const auto turn_range = std::max( settings::g_combat.m_antiaim.near_enemy_range.value, 64.0f );
			const auto turn_range_sq = turn_range * turn_range;
			const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

			// Pick the threat closest to the crosshair, not the physically
			// nearest one: the enemy actually aiming at us defines which way
			// to turn; distance only breaks near-ties.
			const auto view_yaw = systems::g_input.get_view_angles( ).y;
			auto best_score = std::numeric_limits<float>::max( );
			auto near_yaw{ 0.0f };
			auto near_found{ false };

			for ( const auto& p : players )
			{
				if ( !p.ptr || p.ptr == local.controller )
				{
					continue;
				}

				if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
				{
					continue;
				}

				const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
				const auto pawn = systems::g_entities.lookup( pawn_handle );

				if ( !pawn || pawn == local.pawn )
				{
					continue;
				}

				const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
				if ( !local.is_this_other_team( team ) )
				{
					continue;
				}

				const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
				if ( health <= 0 )
				{
					continue;
				}

				const auto enemy_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !enemy_scene_node )
				{
					continue;
				}

				const auto enemy_origin = memory::read<math::vector3>( enemy_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto dx = enemy_origin.x - local_origin.x;
				const auto dy = enemy_origin.y - local_origin.y;
				const auto dist_sq = dx * dx + dy * dy;

				if ( dist_sq >= turn_range_sq )
				{
					continue;
				}

				const auto enemy_yaw = std::atan2f( dy, dx ) * ( 180.0f / std::numbers::pi_v<float> );
				auto ang_diff = std::fabsf( enemy_yaw - view_yaw );
				while ( ang_diff > 180.0f )
				{
					ang_diff = 360.0f - ang_diff;
				}

				// Angle to the crosshair dominates; distance nudges near-ties.
				const auto score = ang_diff * ang_diff * 1000.0f + dist_sq * 0.001f;
				if ( score < best_score )
				{
					best_score = score;
					near_yaw = enemy_yaw;
					near_found = true;
				}
			}

			if ( near_found )
			{
				// Point our head directly AWAY from the enemy so their shots toward
				// where the head should be sail past the turned-around body.
				auto turn_yaw = near_yaw + 180.0f;
				if ( this->m_yaw_side == -1 )
					turn_yaw -= 90.0f;
				else if ( this->m_yaw_side == 1 )
					turn_yaw += 90.0f;

				math::helpers::normalize_angle( turn_yaw );
				this->m_real_yaw = turn_yaw;
				return turn_yaw;
			}
		}

		// Camera-relative AA: base_yaw = view_yaw + 180 always.
		// Body smoothly follows camera, head always hidden behind the back.
		// No target-based override to avoid inconsistency.
		auto yaw = base_yaw;
		if ( this->m_yaw_side == -1 )
		{
			yaw -= 90.0f;
		}
		else if ( this->m_yaw_side == 1 )
		{
			yaw += 90.0f;
		}

		// Compensate for the model's inherent sideways roll. The roll is produced
		// by the walk/strafe leg blend, so it scales with horizontal speed and is
		// gone entirely in the air - a flat 33° offset visibly swung the body
		// left/right the moment the player jumped. Fade it across take-off and
		// landing so the body never snaps.
		if (settings::g_combat.m_antiaim.auto_yaw_adjust.value)
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed_2d = std::sqrt( velocity.x * velocity.x + velocity.y * velocity.y );
			const auto flags = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
			const auto on_ground = ( flags & cstypes::entity_flags::on_ground ) != 0;

			constexpr auto max_roll{ 33.0f }; // thx saphy
			constexpr auto run_speed{ 250.0f };
			const auto move_blend = std::clamp( speed_2d / run_speed, 0.0f, 1.0f );
			const auto target = max_roll * move_blend * ( on_ground ? 1.0f : 0.0f );

			static auto roll_comp = max_roll;
			roll_comp += ( target - roll_comp ) * 0.22f;
			yaw += roll_comp;
		}

		// ENHANCED SPINBOT: Smoother acceleration
		if ( settings::g_combat.m_antiaim.spinbot.value )
		{
			static float spinbot_angle = 0.0f;
			static float spinbot_velocity = 0.0f;
			const auto speed = settings::g_combat.m_antiaim.spinbot_speed.value;
			const auto direction_multiplier = settings::g_combat.m_antiaim.spinbot_direction.value == settings::combat::antiaim::spin_direction::right ? 1.0f : -1.0f;
			
			// Smooth acceleration for less robotic feel
			const auto target_velocity = speed * direction_multiplier;
			spinbot_velocity += ( target_velocity - spinbot_velocity ) * 0.15f; // Smooth acceleration
			spinbot_angle += spinbot_velocity;
			
			// Normalize angle to -180 to 180
			while ( spinbot_angle > 180.0f )
				spinbot_angle -= 360.0f;
			while ( spinbot_angle < -180.0f )
				spinbot_angle += 360.0f;
			
			yaw = view_yaw + spinbot_angle;
		}
		// ENHANCED JITTER: More unpredictable with micro-adjustments
		else if ( settings::g_combat.m_antiaim.jitter.value )
		{
			static float jitter_time = 0.0f;
			static float jitter_micro_offset = 0.0f;
			
			const auto speed = settings::g_combat.m_antiaim.jitter_speed.value;
			const auto max_angle = settings::g_combat.m_antiaim.jitter_angle.value;
			
			// Main jitter movement
			jitter_time += speed * 0.025f;
			
			// Add micro-jitter for unpredictability (small random offsets)
			jitter_micro_offset += ( ( std::rand( ) % 200 - 100 ) / 100.0f * 5.0f - jitter_micro_offset ) * 0.3f;
			
			// Combined smooth sine wave with micro-jitter
			const auto jitter_angle = std::sin( jitter_time ) * max_angle + jitter_micro_offset;
			
			// Apply yaw angle if enabled
			auto yaw_offset = 0.0f;
			if ( settings::g_combat.m_antiaim.jitter_yaw.value )
			{
				yaw_offset = settings::g_combat.m_antiaim.jitter_yaw_angle.value;
			}
			
			// Jitter is applied on top of the camera-relative backward base (+180)
			yaw = view_yaw + 180.0f + jitter_angle + yaw_offset;
		}
		// 1 tick AA: the yaw offset flips on a tick grid so the model snaps
		// side to side every N ticks like the old CSGO robot antiaim.
		else if ( settings::g_combat.m_antiaim.one_tick_aa.value )
		{
			const auto period = std::clamp( settings::g_combat.m_antiaim.one_tick_aa_ticks.value, 1, 16 );
			const auto phase = ( g_shared.ctx( ).current_tick / period ) & 1;

			const auto flip = phase ? 120.0f : -120.0f;
			yaw = view_yaw + 180.0f + flip;
		}

		// Publish the single authoritative angle. The direction indicator draws
		// this; every per-mode local above is a relative offset and would make the
		// marker snap around whenever the mode changed.
		math::helpers::normalize_angle( yaw );
		this->m_real_yaw = yaw;

		return yaw;
	}

	void misc::antiaim::correct_movement (systems::input::usercmd* cmd) {
		if (!this->m_should_correct) {
			return;
		}

		if (!settings::g_combat.m_antiaim.movement_correction.value) {
			return; // Skip movement correction if disabled
		}

		this->m_should_correct = false;

		const auto base = cmd->csgo_user_cmd.mutable_base ();
		const auto forward_move = base->forwardmove ();
		const auto side_move = base->leftmove ();

		if (forward_move == 0.0f && side_move == 0.0f) {
			return;
		}

		math::vector3 new_forward {}, new_left {};
		math::helpers::angle_vectors_left (this->m_modified_angles, &new_forward, &new_left, nullptr);

		math::vector3 old_forward {}, old_left {};
		math::helpers::angle_vectors_left (this->m_old_angles, &old_forward, &old_left, nullptr);

		new_forward.z = 0.0f; new_left.z = 0.0f;
		old_forward.z = 0.0f; old_left.z = 0.0f;
		new_forward.normalize ();
		new_left.normalize ();
		old_forward.normalize ();
		old_left.normalize ();

		const auto intent = old_forward * forward_move + old_left * -side_move;
		const auto intent_len = intent.length ();

		if (intent_len == 0.0f) {
			return;
		}

		const auto intent_dir = intent / intent_len;
		const auto corrected_forward = new_forward.dot (intent_dir) * intent_len;
		const auto corrected_side = -new_left.dot (intent_dir) * intent_len;

		base->set_forwardmove (std::clamp (corrected_forward, -1.0f, 1.0f));
		base->set_leftmove (std::clamp (corrected_side, -1.0f, 1.0f));

		if (systems::g_prediction.pre ().flags & cstypes::entity_flags::on_ground) {
			auto buttons = cmd->buttons.value;
			buttons &= ~static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

			if (base->forwardmove () > 0.0f) {
				buttons |= cstypes::command_buttons::in_forward;
			} else if (base->forwardmove () < 0.0f) {
				buttons |= cstypes::command_buttons::in_back;
			}

			if (base->leftmove () > 0.0f) {
				buttons |= cstypes::command_buttons::in_moveleft;
			} else if (base->leftmove () < 0.0f) {
				buttons |= cstypes::command_buttons::in_moveright;
			}

			cmd->buttons.value = buttons;
		}
	}

	bool misc::antiaim::is_near_ladder( std::uintptr_t local_pawn ) const
	{
		return false; // ill do this lader.
	}

	// Freestand: instead of forcing a side by hand, probe both candidate yaws and
	// keep whichever one leaves the body less visible. Exposure is the sum of how
	// directly each nearby enemy can see the player from the two candidate sides,
	// with distance weighting so a close enemy matters far more than a distant one.
	// A trace that hits a wall first means that side is hidden, which is exactly
	// the corner situation freestanding is meant to exploit.
	// Freestand is about walls, not enemies. The idea: when you stand near a
	// corner, only one of the two body sides is actually exposed to the enemy.
	// Leaning the anti-aim into the wall side buries that half of the hitbox in
	// geometry, so the enemy's aimbot can only reach the half that is already
	// behind cover. This casts a short probe out to each side and keeps whichever
	// one hits a wall sooner.
	//
	// The probe is a small hull around the player's own bounds rather than a thin
	// ray, because a ray slips through the tiny gap a player model leaves when
	// standing flush against a wall.
	float misc::antiaim::measure_wall_distance( const math::vector3& origin, const math::vector3& side_dir, float range ) const
	{
		const auto end = origin + side_dir * range;

		// Player hull, half-extents roughly matching a standing CS2 hitbox.
		const auto mins = math::vector3{ -16.0f, -16.0f, -36.0f };
		const auto maxs = math::vector3{ 16.0f, 16.0f, 36.0f };

		const auto trace = systems::g_tracing.trace_hull( origin, end, mins, maxs, systems::g_local.get( ).pawn );

		if ( trace.fraction >= 1.0f )
		{
			// Nothing in range on this side: treat it as fully exposed.
			return range;
		}

		return range * std::clamp( trace.fraction, 0.0f, 1.0f );
	}

	// Commit a freshly scored side, but refuse to flip while the current side is
	// still young. Two sides that score within a hair of each other would
	// otherwise trade places every tick, which reads to the enemy as a body that
	// never settles on a yaw and is worse than not freestanding at all.
	int misc::antiaim::evaluate_freestand_side( const math::vector3& origin, const systems::local::snapshot& local ) const
	{
		const auto side = this->compute_freestand_side( origin, local );

		if ( side == this->m_freestand_committed )
		{
			return side;
		}

		const auto now = static_cast< float >( GetTickCount64( ) ) * 0.001f;

		if ( this->m_freestand_committed != 0 && now - this->m_freestand_switch_time < 0.12f )
		{
			return this->m_freestand_committed;
		}

		this->m_freestand_switch_time = now;
		this->m_freestand_committed = side;

		return side;
	}

	int misc::antiaim::compute_freestand_side( const math::vector3& origin, const systems::local::snapshot& local ) const
	{
		const auto range = std::max( settings::g_combat.m_antiaim.freestand_range.value, 32.0f );

		// Left and right are perpendicular to the camera, matching the +/-90
		// yaw offset the freestand result is applied with.
		const auto view_yaw = systems::g_input.get_view_angles( ).y;
		const auto rad = view_yaw * ( std::numbers::pi_v<float> / 180.0f );
		const auto forward = math::vector3{ std::cosf( rad ), std::sinf( rad ), 0.0f };
		const auto left = math::vector3{ -forward.y, forward.x, 0.0f };
		const auto right = math::vector3{ forward.y, -forward.x, 0.0f };

		// Candidate body points: three heights down the model, each shifted half
		// a player width toward the side being tested. Rotating the real yaw by
		// 90 degrees displaces the head roughly by a body half-width, so testing
		// the shifted head is what tells us if that rotation hides it.
		const auto eye_offset = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		constexpr auto body_shift{ 18.0f };
		constexpr float height_scale[]{ 1.0f, 0.72f, 0.45f };
		constexpr float height_weight[]{ 3.0f, 1.5f, 1.0f };

		math::vector3 left_points[ 3 ]{};
		math::vector3 right_points[ 3 ]{};
		for ( auto i = 0; i < 3; ++i )
		{
			const auto height = eye_offset * height_scale[ i ];
			left_points[ i ] = origin + left * body_shift + height;
			right_points[ i ] = origin + right * body_shift + height;
		}

		// Side-wall scan: probe straight out of the head, chest and waist on each
		// side and add the hit distances up. A side with geometry at every height
		// is "walled"; a side whose probes all run the full range is "open". That
		// is the difference between standing in a corner and standing in a
		// doorway, and it is what the old single body-height probe kept missing.
		constexpr auto k_side_wall_range{ 48.0f };
		const auto side_probe_extent = math::vector3{ -2.0f, -2.0f, -2.0f };
		const auto side_probe_maxs = math::vector3{ 2.0f, 2.0f, 2.0f };

		const auto side_wall_depth = [ & ]( const math::vector3& dir ) -> float
		{
			auto total{ 0.0f };

			for ( auto i = 0; i < 3; ++i )
			{
				const auto from = origin + eye_offset * height_scale[ i ];
				const auto trace = systems::g_tracing.trace_hull( from, from + dir * k_side_wall_range, side_probe_extent, side_probe_maxs, local.pawn );
				total += k_side_wall_range * std::clamp( trace.fraction, 0.0f, 1.0f );
			}

			return total;
		};

		const auto left_wall_depth = side_wall_depth( left );
		const auto right_wall_depth = side_wall_depth( right );

		// Require a clear difference: one side genuinely walled (its probes stop
		// well short of the range at every height) and the other genuinely open
		// (every probe runs the full way). A pillar or doorway that only trims the
		// probes is left to the enemy logic below, which is what keeps this rule
		// from flipping the body around on every doorway.
		const auto left_walled = left_wall_depth < k_side_wall_range * 1.35f;
		const auto left_open = left_wall_depth > k_side_wall_range * 2.55f;
		const auto right_walled = right_wall_depth < k_side_wall_range * 1.35f;
		const auto right_open = right_wall_depth > k_side_wall_range * 2.55f;

		// Facing a wall with the other side open: turn into the wall, so the head
		// and the front of the body are the covered part and the open side only
		// sees the back. Open on the right -> face left, open on the left -> face
		// right. This is the explicit rule the old probe kept getting backwards.
		if ( left_walled && right_open )
		{
			return -1;
		}

		if ( right_walled && left_open )
		{
			return 1;
		}

		// Every living enemy becomes a threat. The closest one owns the decision
		// and the others only reinforce or veto it, so the body always commits
		// to hiding from whoever is actually on top of us.
		struct threat_t
		{
			std::uintptr_t pawn{};
			math::vector3 eye{};
			math::vector3 aim_forward{};
			float distance{};
		};

		std::array<threat_t, 32> threats{};
		auto threat_count{ 0u };
		auto closest_index{ 0u };

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( threat_count >= threats.size( ) )
			{
				break;
			}

			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			if ( memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) <= 0 )
			{
				continue;
			}

			const auto scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !scene_node )
			{
				continue;
			}

			const auto enemy_origin = memory::read<math::vector3>( scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
			const auto enemy_eye = enemy_origin + memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );

			// The enemy's own aim ray, so the decision reflects where they are
			// actually looking instead of assuming they stare at our feet.
			auto aim_forward = math::vector3{};
			math::helpers::angle_vectors_left( memory::read<math::vector3>( pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) ), &aim_forward );

			if ( !std::isfinite( aim_forward.x ) || aim_forward.length_sqr( ) < 0.5f )
			{
				aim_forward = ( origin + eye_offset * 0.6f - enemy_eye ).normalized( );
			}

			const auto delta = enemy_eye - origin;
			const auto distance = std::sqrtf( delta.x * delta.x + delta.y * delta.y );

			if ( threat_count == 0 || distance < threats[ closest_index ].distance )
			{
				closest_index = threat_count;
			}

			threats[ threat_count++ ] = { pawn, enemy_eye, aim_forward, distance };
		}

		// Nobody alive to hide from: lean into whichever wall is closer, so the
		// body is already tucked behind cover before someone peeks.
		if ( threat_count == 0 )
		{
			const auto left_distance = this->measure_wall_distance( origin, left, range );
			const auto right_distance = this->measure_wall_distance( origin, right, range );

			const auto left_has_wall = left_distance < range - 0.5f;
			const auto right_has_wall = right_distance < range - 0.5f;

			if ( left_has_wall == right_has_wall )
			{
				return this->m_freestand_result;
			}

			return left_has_wall ? -1 : 1;
		}

		const auto closest = threats[ closest_index ];

		// Head-to-wall probes: geometry this close behind a candidate head means
		// turning that way presses the head into the wall, so the enemy swinging
		// on us sees the body first and the head never peeks.
		constexpr auto k_head_wall_probe{ 96.0f };
		constexpr auto k_head_wall_cover{ 26.0f };
		const auto k_head_probe_mins = math::vector3{ -2.0f, -2.0f, -2.0f };
		const auto k_head_probe_maxs = math::vector3{ 2.0f, 2.0f, 2.0f };

		// The wall rule only engages while the closest enemy can actually see our
		// body: if nobody can see us yet the shot does not exist and the side is
		// picked purely on cover.
		const auto closest_sees_body = systems::g_tracing.is_visible( closest.eye, origin + eye_offset * 0.55f, closest.pawn, local.pawn );

		// Score a candidate side: occluded body points count positive, visible
		// ones negative, with the head (index 0) weighted hardest because it is
		// the hitbox anti-aim has to hide. The closest enemy also contributes how
		// far the candidate head sits off his crosshair -- that is what decides
		// the open-field case where neither side has cover, and it makes the
		// turn react the moment he starts looking at us.
		struct side_score_t
		{
			float total{};
			bool head_visible{};
			bool head_wall_covered{};
		};

		const auto evaluate_side = [ & ]( const math::vector3* points ) -> side_score_t
		{
			side_score_t score{};

			for ( auto i = 0u; i < threat_count; ++i )
			{
				const auto& threat = threats[ i ];

				// The closest enemy counts fully; the rest fade with distance
				// but never below a quarter of his weight.
				const auto proximity = threat.distance > 1.0f
					? std::clamp( closest.distance / threat.distance, 0.25f, 1.0f )
					: 1.0f;

				for ( auto h = 0; h < 3; ++h )
				{
					const auto weight = height_weight[ h ] * proximity;

					if ( systems::g_tracing.is_visible( threat.eye, points[ h ], threat.pawn, local.pawn ) )
					{
						score.total -= weight;

						if ( h == 0 )
						{
							score.head_visible = true;
						}
					}
					else
					{
						score.total += weight;
					}
				}

				if ( threat.pawn == closest.pawn )
				{
					const auto to_head = points[ 0 ] - threat.eye;
					const auto projected = threat.aim_forward * to_head.dot( threat.aim_forward );
					score.total += std::min( ( to_head - projected ).length( ) / 40.0f, 2.0f );

					// Hide the head against the nearest wall. Probing away from the
					// enemy tells us whether this side has geometry right behind the
					// head; the closer the wall, the harder the head is to hit while
					// the body stays available.
					if ( closest_sees_body && to_head.length_sqr( ) > 1.0f )
					{
						const auto away = to_head.normalized( );
						const auto wall = systems::g_tracing.trace_hull( points[ 0 ], points[ 0 ] + away * k_head_wall_probe, k_head_probe_mins, k_head_probe_maxs, local.pawn );

						if ( wall.fraction < 1.0f )
						{
							const auto depth = k_head_wall_probe * std::clamp( wall.fraction, 0.0f, 1.0f );

							if ( depth <= k_head_wall_cover )
							{
								score.head_wall_covered = true;
							}

							score.total += ( 1.0f - depth / k_head_wall_probe ) * 3.0f;
						}
					}
				}
			}

			return score;
		};

		const auto left_score = evaluate_side( left_points );
		const auto right_score = evaluate_side( right_points );

		// A side the closest enemy cannot see our head from wins outright:
		// burying the head takes priority over every body-exposure nuance, and
		// switching immediately is the whole point of a freestand.
		if ( left_score.head_visible != right_score.head_visible )
		{
			return left_score.head_visible ? 1 : -1;
		}

		// Both heads are visible, but one side has a wall right behind the head:
		// turning that way buries the head the instant we rotate, so it wins
		// before any body-exposure tally. This is the "only the body peeks" rule.
		if ( left_score.head_wall_covered != right_score.head_wall_covered )
		{
			return left_score.head_wall_covered ? -1 : 1;
		}

		// Both sides equally (in)visible: keep the current side unless the other
		// one is clearly better, so the body does not thrash every tick.
		constexpr auto margin{ 0.5f };
		const auto delta = left_score.total - right_score.total;

		if ( std::fabsf( delta ) >= margin )
		{
			return delta > 0.0f ? -1 : 1;
		}

		if ( this->m_freestand_result != 0 )
		{
			return this->m_freestand_result;
		}

		// First evaluation with no clear winner: use the wall probe to pick a
		// starting side instead of leaving the body facing the enemy dead on.
		const auto left_distance = this->measure_wall_distance( origin, left, range );
		const auto right_distance = this->measure_wall_distance( origin, right, range );

		if ( left_distance != right_distance )
		{
			return left_distance < right_distance ? -1 : 1;
		}

		return -1;
	}

	void misc::antiaim::on_bullet_impact( const math::vector3& impact_pos, std::uintptr_t attacker_pawn, const systems::local::snapshot& local )
	{
		// Auto pitch feature removed - function disabled
		return;
		{
			return;
		}

		if ( !local.is_alive || !local.pawn )
		{
			return;
		}

		// Get local player position and velocity
		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto local_origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto eye_offset = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		const auto eye_pos = local_origin + eye_offset;
		
		// Get player velocity for prediction
		const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		
		// Get player bounds for better hitbox detection
		const auto collision_property = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		auto player_radius = 32.0f; // Default CS:GO player capsule radius
		if ( collision_property )
		{
			const auto mins = memory::read<math::vector3>( collision_property + 0x08 );
			const auto maxs = memory::read<math::vector3>( collision_property + 0x14 );
			player_radius = std::max( std::abs( mins.x ), std::abs( maxs.x ) );
		}

		// Get attacker position for trajectory calculation
		const auto attacker_game_scene_node = memory::read<std::uintptr_t>( attacker_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !attacker_game_scene_node )
		{
			return;
		}

		const auto attacker_origin = memory::read<math::vector3>( attacker_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto attacker_eye_offset = memory::read<math::vector3>( attacker_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		const auto attacker_eye = attacker_origin + attacker_eye_offset;

		// Calculate bullet trajectory direction
		const auto trajectory_dir = ( impact_pos - attacker_eye ).normalized( );
		const auto trajectory_length = attacker_eye.distance( impact_pos );
		
		// ===== ADVANCED MULTI-POINT DETECTION =====
		// Check multiple body positions: feet, waist, chest, head
		const std::vector<math::vector3> body_points = {
			local_origin,                              // Feet
			local_origin + eye_offset * 0.33f,        // Waist
			local_origin + eye_offset * 0.66f,        // Chest  
			eye_pos,                                   // Head/Eyes
			local_origin + math::vector3{ player_radius, 0.0f, eye_offset.z * 0.5f },  // Right shoulder
			local_origin + math::vector3{ -player_radius, 0.0f, eye_offset.z * 0.5f }  // Left shoulder
		};
		
		// Adaptive danger radius based on distance
		const auto distance_to_attacker = eye_pos.distance( attacker_eye );
		const auto base_danger_radius = 45.0f; // Tighter base detection
		const auto distance_scale = std::min( 1.0f + ( distance_to_attacker / 1000.0f ), 2.5f ); // Scale up for distance
		const auto danger_radius = base_danger_radius * distance_scale;
		
		bool threat_detected = false;
		float closest_distance = FLT_MAX;
		
		// Check each body point against bullet trajectory
		for ( const auto& body_point : body_points )
		{
			// Account for player movement prediction (predict 0.1s ahead)
			const auto predicted_pos = body_point + velocity * 0.1f;
			
			// Calculate closest point on bullet ray to this body point
			const auto to_body = predicted_pos - attacker_eye;
			const auto dot = to_body.dot( trajectory_dir );
			
			// Bullet must be traveling towards player
			if ( dot < 0.0f )
			{
				continue;
			}
			
			// Don't check beyond the bullet impact point
			if ( dot > trajectory_length )
			{
				continue;
			}
			
			const auto closest_point = attacker_eye + trajectory_dir * dot;
			const auto distance = predicted_pos.distance( closest_point );
			
			if ( distance < danger_radius + player_radius )
			{
				threat_detected = true;
				closest_distance = std::min( closest_distance, distance );
			}
		}
		
		if ( !threat_detected )
		{
			return; // No threat to any body part
		}

		// ===== SMART COOLDOWN SYSTEM =====
		const auto current_time = memory::read<float>( addresses::globals::global_vars + 0x10 );
		
		// Adaptive cooldown: shorter for different attackers, longer for same attacker
		const auto cooldown = ( m_auto_pitch_state.last_attacker_pawn == attacker_pawn ) ? 0.4f : 0.2f;
		
		if ( m_auto_pitch_state.last_attacker_pawn == attacker_pawn && 
		     ( current_time - m_auto_pitch_state.trigger_time ) < cooldown )
		{
			return;
		}

		// ===== TRIGGER AUTO PITCH =====
		m_auto_pitch_state.active = true;
		m_auto_pitch_state.trigger_time = current_time;
		m_auto_pitch_state.last_attacker_pawn = attacker_pawn;
		m_auto_pitch_state.last_impact = impact_pos;
		
		// Smarter flick calculation based on threat
		const auto threat_level = 1.0f - ( closest_distance / ( danger_radius + player_radius ) );
		
		// Random flick direction
		m_auto_pitch_state.flick_direction = ( std::rand( ) % 2 == 0 ) ? 1 : -1;
		
		// Stronger flick for closer threats: 70-140 degrees
		const auto base_flick = 70.0f + ( std::rand( ) % 71 );
		const auto threat_multiplier = 1.0f + ( threat_level * 0.3f );
		m_auto_pitch_state.yaw_offset = base_flick * threat_multiplier * m_auto_pitch_state.flick_direction;

		// Get attacker name for log
		const auto attacker_controller_handle = memory::read<std::uint32_t>( attacker_pawn + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
		if ( attacker_controller_handle )
		{
			const auto attacker_controller = systems::g_entities.lookup( attacker_controller_handle );
			if ( attacker_controller )
			{
				const auto name_ptr = memory::safe_read<std::uintptr_t>(
					attacker_controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) ).value_or( 0 );
				const auto attacker_name = memory::read_string( name_ptr, 127 );
				
				if ( !attacker_name.empty( ) )
				{
					features::misc::g_other.add_evasion_log( attacker_name );
				}
			}
		}
	}

	void misc::antiaim::apply_auto_pitch( const systems::local::snapshot& local )
	{
		// Auto pitch feature removed
		return;
	}

	void misc::antiaim::trigger_auto_pitch_on_shot( )
	{
		// Auto pitch feature removed
		return;
	}

	namespace {

		math::vector3 quickpeek_ground_snap( std::uintptr_t skip_pawn, const math::vector3& feet_pos )
		{
			const auto start = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z + 64.0f };
			const auto end = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z - 8192.0f };
			const auto tr = systems::g_tracing.trace( start, end, skip_pawn );

			if ( tr.fraction <= 0.0f || tr.fraction >= 0.997f )
			{
				return feet_pos;
			}

			auto out = tr.position;
			out.z += 1.0f;
			return out;
		}

	} // namespace

	void misc::duckpeek::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_fake_stand_active = false;

		if ( !cmd || !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			this->m_was_active = false;
			return;
		}

		this->m_was_active = true;

		if ( g_rage.should_release_duck_for_shot( ) )
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			this->m_fake_stand_active = true;
			return;
		}

		cmd->buttons.value |= cstypes::command_buttons::in_duck;

		if ( g_rage.duckpeek_wants_reduck( ) )
		{
			g_rage.clear_duckpeek_reduck( );
		}
	}

	void misc::duckpeek::on_override_view( std::uintptr_t view_setup )
	{
		(void)view_setup;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			this->m_fake_stand_active = false;
		}
	}

	void misc::quickpeek::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
			return;
		}

		const auto local = systems::g_local.get( );
		const auto& ctx = g_shared.ctx( );

		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->reset( );
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		constexpr auto movement_cancel_mask = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
		const auto curr_movement_bits = cmd->buttons.value & movement_cancel_mask;

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

		if ( this->m_saved_origin.length_sqr( ) < 0.001f )
		{
			this->m_saved_origin = quickpeek_ground_snap( local.pawn, origin );
			this->m_should_retrack = false;
			this->m_fired = false;
			this->m_active = true;
			this->create_particle( );
			this->m_prev_movement_bits = curr_movement_bits;
			return;
		}

		this->update_particle( );

		const auto distance = ( origin - this->m_saved_origin ).length_2d( );

		if ( this->m_should_retrack && ( curr_movement_bits & ~this->m_prev_movement_bits ) != 0 )
		{
			this->m_should_retrack = false;
		}

		if ( this->m_should_retrack )
		{
			// Rubber band: yank the client velocity toward the peek origin so the
			// return snaps back instantly while the command stream drags the
			// server position home with it.
			const auto diff = this->m_saved_origin - origin;
			const auto dist = diff.length( );
			if ( dist > 1.0f )
			{
				const auto max_speed = std::max( memory::read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) ), 250.0f );
				const auto dir = diff * ( 1.0f / dist );
				memory::write<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), { dir.x * max_speed * 1.35f, dir.y * max_speed * 1.35f, 0.0f } );
			}
		}

		if ( this->m_should_retrack && ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) )
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed = velocity.length_2d( );

			if ( distance < 5.0f && speed < 15.0f )
			{
				this->m_should_retrack = false;
				this->m_fired = false;
			}
			else if ( distance < speed * 0.1f && speed > 15.0f )
			{
				const auto vel_angle = math::helpers::vector_to_angle( velocity * -1.0f );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - vel_angle.y );

				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );

				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}

				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}

				cmd->buttons.value = buttons;
			}
			else
			{
				const auto diff = this->m_saved_origin - origin;
				const auto angle_to_pos = math::helpers::vector_to_angle( diff );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - angle_to_pos.y );

				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );

				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}

				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}

				cmd->buttons.value = buttons;
			}
		}

		if ( ( cmd->buttons.value & cstypes::command_buttons::in_attack ) && !g_rage.is_cocking_revolver( ) )
		{
			this->m_should_retrack = true;
			this->m_fired = true;
		}

		this->m_prev_movement_bits = curr_movement_bits;
	}

	void misc::quickpeek::reset_if_needed( )
	{
		if ( !this->m_active )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn )
		{
			this->reset( );
			return;
		}

		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
		}
	}

	void misc::quickpeek::create_particle( )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/halo.vpcf" };

		if ( !this->m_particle_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };

				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};

				std::uintptr_t m_unknown3{ 0 };
				std::uintptr_t m_unknown4{ 0 };
			} buffer;

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );
			buffer.m_unknown4 = 'fcpv';
			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_particle_loaded = true;
		}

		auto effect_index{ invalid_effect_index };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		this->m_particle_effect = effect_index;

		if ( effect_index == invalid_effect_index )
		{
			return;
		}

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_saved_origin, 0 );
	}

	void misc::quickpeek::update_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		const auto& cfg = settings::g_combat.m_quickpeek;
		const auto& col = this->m_should_retrack ? cfg.retrack_color : cfg.color;
		const auto color = math::vector3{ static_cast< float >( col.value.r ), static_cast< float >( col.value.g ), static_cast< float >( col.value.b ) };

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 1, &color, 0 );
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 0, &this->m_saved_origin, 0 );
	}

	void misc::quickpeek::release_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( particle_manager )
		{
			memory::call<void>(PATTERN (patterns::particle_destroy_effect), particle_manager, this->m_particle_effect, true, true );
		}

		this->m_particle_effect = invalid_effect_index;
	}

	void misc::quickpeek::reset( )
	{
		this->release_particle( );
		this->m_saved_origin = {};
		this->m_should_retrack = false;
		this->m_fired = false;
		this->m_active = false;
		this->m_prev_movement_bits = 0;
	}

	void misc::autostop::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !features::combat::g_rage.should_stop( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto& ctx = g_shared.ctx( );

		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		auto velocity = prestate.networked_velocity;
		auto speed = velocity.length_2d( );

		if ( speed <= 1.0f )
		{
			return;
		}

		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto control = std::fmaxf( speed, sv_stopspeed );
		const auto drop = control * sv_friction * surface_friction * cstypes::tick_interval;
		const auto post_friction = std::fmaxf( speed - drop, 0.0f );

		if ( post_friction > 0.0f )
		{
			velocity *= ( post_friction / speed );
			speed = post_friction;
		}
		else
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}

		if ( speed < 2.0f )
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}

		auto accel = CONVAR ("sv_accelerate")->get<float>( );
		const auto accel_base = this->get_effective_accel_base( local.pawn, movement_services, prestate.flags, ctx.weapon_max_speed );

		if ( ctx.is_scoped )
		{
			const auto weapon_ratio = std::fminf( 1.0f, ctx.weapon_max_speed / 250.0f );
			const auto v20 = std::fmaxf( 250.0f, memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) ) * weapon_ratio;
			const auto scoped_max = v20 * 0.52f;

			if ( speed > scoped_max - 5.0f )
			{
				const auto t = 1.0f - std::fmaxf( 0.0f, speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
				accel *= std::clamp( t, 0.0f, 1.0f );
			}
		}

		const auto wish_x = -velocity.x / speed;
		const auto wish_y = -velocity.y / speed;
		const auto accel_speed = std::fminf( accel * accel_base * surface_friction * cstypes::tick_interval, speed );

		velocity.x += wish_x * accel_speed;
		velocity.y += wish_y * accel_speed;

		const auto move_magnitude = std::clamp( speed / ctx.weapon_max_speed, 0.0f, 1.0f );
		const auto yaw_rad = base->viewangles( )->y( ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto sy = std::sinf( yaw_rad );
		const auto cy = std::cosf( yaw_rad );

		const auto forward_move = std::clamp( ( wish_x * cy + wish_y * sy ) * move_magnitude, -1.0f, 1.0f );
		const auto left_move = std::clamp( ( wish_x * sy - wish_y * cy ) * -move_magnitude, -1.0f, 1.0f );

		base->set_forwardmove( forward_move );
		base->set_leftmove( left_move );

		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( subtick_moves )
		{
			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( step )
			{
				step->set_button( 0 );
				step->set_pressed( false );
				step->set_when( 0.0f );
				step->set_analog_forward_delta( forward_move - prestate.last_movement_impulses.x );
				step->set_analog_left_delta( left_move - prestate.last_movement_impulses.y );
			}
		}

		if ( forward_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_forward;
		}
		else if ( forward_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_back;
		}

		if ( left_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveleft;
		}
		else if ( left_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveright;
		}
	}

	float misc::autostop::get_effective_accel_base( std::uintptr_t local_pawn, std::uintptr_t movement_services, std::uint32_t flags, float max_weapon_speed ) const
	{
		const auto max_speed_base = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
		const auto is_ducked = ( flags & 4 ) != 0;
		const auto ducking_state = memory::read<bool>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_bDucking"_hash ) );
		const auto is_scoped = g_shared.ctx( ).is_scoped;
		const auto is_ducking = is_ducked || ducking_state;
		const auto v19 = std::fmaxf( 250.0f, max_speed_base );

		auto friction_scale{ 1.0f };

		if (CONVAR ("sv_accelerate_use_weapon_speed")->get<bool>( ) )
		{
			const auto weapon_ratio = std::fminf( 1.0f, max_weapon_speed / 250.0f );

			if ( !is_ducking && !is_scoped )
			{
				friction_scale = weapon_ratio;
			}
		}

		if ( is_ducking )
		{
			friction_scale = std::fminf( 0.34f, friction_scale );
		}

		auto accel_base = v19 * friction_scale;

		if ( is_scoped && !is_ducking )
		{
			accel_base *= 0.52f;
		}

		return accel_base;
	}

} // namespace features::combat
