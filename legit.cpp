#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/random/random.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	void legit::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_legitbot.enabled.value )
			return;

		auto& ctx = g_shared.ctx( );
		if ( !ctx.valid )
			return;

		// Only work with guns
		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
			return;

		const auto& config = settings::g_combat.m_legitbot.get_group( ctx.weapon_type );
		const auto local = systems::g_local.get( );
		
		if ( !local.pawn || !local.controller || !local.is_alive )
			return;

		const auto view_angles = systems::g_input.get_view_angles( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );

		this->m_cached_view_angles = view_angles;
		this->m_cached_aim_punch = aim_punch;

		// Standalone RCS
		if ( config.standalone_rcs.value )
		{
			this->update_standalone_rcs( view_angles, aim_punch, config.standalone_rcs_strength.value, 
				config.standalone_rcs_min.value, config.standalone_rcs_max.value, !config.aimbot.value, local );
		}

		this->m_target = {};
		this->m_has_shot_intent = false;
		this->m_trigger_shot_this_tick = false;

		const auto has_aim_or_trigger = config.aimbot.value || config.triggerbot.value;

		// Check if aimbot or triggerbot enabled. No spread keeps running on its
		// own: it straightens the weapon's spread for a shot the player fires,
		// the same way the ragebot's version keeps working with the bot disabled.
		if ( !has_aim_or_trigger && !config.no_spread.value )
			return;

		// Get shoot position. Only the aim and trigger paths need it; the no spread
		// path works off the command angles, so a tick that has nothing to aim at
		// does not pay for the interpolation.
		auto shoot_position = math::vector3{};
		if ( has_aim_or_trigger )
		{
			shoot_position = g_shared.get_interpolated_shoot_position( local.pawn, false );
		}

		// AIMBOT
		if ( config.aimbot.value )
		{
			auto detection_angles = view_angles;
			if ( config.rcs.value && aim_punch.length_sqr( ) > 0.0001f )
			{
				detection_angles.x += aim_punch.x;
				detection_angles.y += aim_punch.y;
				math::helpers::normalize_angles( detection_angles );
			}

			this->m_target = this->find_target( shoot_position, detection_angles, config, local );
			if ( this->m_target.has_target( ) )
			{
				this->apply_aimbot( cmd, this->m_target, view_angles, aim_punch, config, local );
			}
		}

		// TRIGGERBOT
		if ( config.triggerbot.value && g_shared.can_shoot( cmd, local.controller ) )
		{
			this->apply_triggerbot( cmd, shoot_position, view_angles, aim_punch, config, local );
		}

		// No spread runs once, here, and only on the tick a bullet is actually
		// leaving. Its seed search walks up to 720 view angles and calls into the
		// engine twice for each one, so correcting every tick - which is what the
		// aim paths used to do with their normal angles - is what ate the frame
		// rate. Correcting the shot tick instead costs the search once per bullet,
		// and it doubles as the fix for a shot the player fires himself.
		if ( config.no_spread.value )
		{
			this->apply_shot_no_spread( cmd, view_angles, aim_punch );
		}
	}

	void legit::on_render( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_combat.m_legitbot.enabled.value )
			return;

		const auto& ctx = g_shared.ctx( );
		if ( !ctx.valid )
			return;

		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
			return;

		const auto& config = settings::g_combat.m_legitbot.get_group( ctx.weapon_type );

		// Draw FOV circle
		if ( config.visualize_fov.value )
		{
			this->draw_fov( draw_list, this->m_cached_view_angles, this->m_cached_aim_punch, 
				config.fov.value, config.fov_color, config.rcs.value );
		}

		// Draw silent aim FOV
		if ( config.silent_visualize_fov.value && config.silent_aim.value )
		{
			this->draw_fov( draw_list, this->m_cached_view_angles, this->m_cached_aim_punch, 
				config.silent_fov.value, config.silent_fov_color, config.rcs.value );
		}
	}

	void legit::invalidate_if_needed( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->m_trigger_release = false;
			this->m_trigger_pending_pawn = 0;
			this->m_trigger_delay_start = 0.0f;
		}
	}

	legit::target_result legit::find_target( const math::vector3& shoot_position, const math::vector3& view_angles, 
		const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const
	{
		target_result best{};
		best.fov = 999.0f;

		const auto max_fov = config.silent_aim.value ? config.silent_fov.value : config.fov.value;

		// Iterate through all players
		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
				continue;

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
				continue;

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
				continue;

			const auto team = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
				continue;

			const auto health = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
				continue;

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
				continue;

			// Get lagcomp records
			const auto records = g_shared.lc( ).get_valid_records( pawn );
			if ( records.empty( ) )
				continue;

			const auto record = records.front( );
			if ( !record || !record->valid )
				continue;

			// Scan hitboxes
			const auto point = this->scan_player( pawn, record, shoot_position, view_angles, config, local );
			if ( !point.valid )
				continue;

			const auto aim = math::helpers::calculate_angle( shoot_position, point.position );
			const auto fov = math::helpers::angle_distance( view_angles, aim );

			if ( fov > max_fov )
				continue;

			// Score based on distance/FOV
			if ( fov < best.fov )
			{
				best.pawn = pawn;
				best.aim_angle = aim;
				best.fov = fov;
				best.health = health;
				best.record = record;
				best.best_point = point;
			}
		}

		return best;
	}

	legit::scan_point legit::scan_player( std::uintptr_t pawn, shared::lagcomp::record* record, 
		const math::vector3& shoot_position, const math::vector3& view_angles, 
		const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const
	{
		const auto pen_ctx = g_shared.pen( ).prepare_target( pawn, record );
		const auto& skeleton = record->bones;

		scan_point best{};
		best.fov = 999.0f;
		best.damage = -1.0f;

		const auto max_fov = config.silent_aim.value ? config.silent_fov.value : config.fov.value;

		for ( const auto& hitbox : pen_ctx.hitboxes )
		{
			const auto cfg_idx = hitgroup_to_cfg( systems::g_hitboxes.hitgroup_from_hitbox( hitbox.index ) );
			if ( cfg_idx < 0 || !config.hitboxes.values[ cfg_idx ] )
				continue;

			const auto bone_index = hitbox.bone;
			if ( bone_index < 0 || bone_index >= record->bone_count )
				continue;

			const auto& bone = skeleton[ bone_index ];
			if ( bone.position.length_sqr( ) < 1.0f )
				continue;

			const auto center = ( hitbox.mins + hitbox.maxs ) * 0.5f;
			const auto position = bone.position + ( hitbox.translation_only ? center : bone.rotation.rotate_vector( center ) );
			const auto aim = math::helpers::calculate_angle( shoot_position, position );
			const auto fov = math::helpers::angle_distance( view_angles, aim );

			if ( fov > max_fov )
				continue;

			// Check penetration
			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( shoot_position, position, pen_ctx, local.pawn, local.team, pen ) )
				continue;

			const auto visible = !pen.penetrated;
			if ( !visible && !config.autowall.value )
				continue;

			if ( !visible && pen.damage < static_cast< float >( config.min_damage.value ) )
				continue;

			// Pick best point
			const auto is_better = ( pen.damage > best.damage ) || ( pen.damage == best.damage && fov < best.fov );
			if ( is_better )
			{
				best.position = position;
				best.damage = pen.damage;
				best.fov = fov;
				best.hitgroup = pen.hitgroup;
				best.cfg_index = cfg_idx;
				best.bone_index = static_cast< int >( bone_index );
				best.hitbox = hitbox;
				best.visible = visible;
				best.is_center = true;
				best.valid = true;
			}
		}

		return best;
	}

	void legit::apply_aimbot( systems::input::usercmd* cmd, const target_result& tgt, const math::vector3& view_angles, 
		const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local )
	{
		auto aim_angle = tgt.aim_angle;

		// Apply RCS
		if ( config.rcs.value )
		{
			this->apply_rcs( aim_angle, aim_punch, config.rcs_min.value, config.rcs_max.value );
		}

		// SILENT AIM - directly set angles in command
		if ( config.silent_aim.value )
		{
			math::helpers::normalize_angles( aim_angle );

			// Remember the direction this shot is aimed along. No spread bends it
			// once, at the end of the tick, and only if a bullet really leaves.
			this->m_shot_intent = aim_angle;
			this->m_has_shot_intent = true;

			this->set_command_angles( cmd, aim_angle, aim_punch, false );

			return; // Skip smoothing for silent aim
		}

		// VISIBLE AIMBOT - smooth aim with viewangles
		auto delta = aim_angle - view_angles;
		math::helpers::normalize_angles( delta );

		const auto delta_length = std::sqrtf( delta.x * delta.x + delta.y * delta.y );
		if ( delta_length < 0.001f )
			return;

		auto move_x = delta.x;
		auto move_y = delta.y;

		// Apply smoothing
		if ( config.smooth.value > 0 )
		{
			const auto smooth_factor = 1.0f / static_cast< float >( config.smooth.value );
			move_x *= smooth_factor;
			move_y *= smooth_factor;
		}

		// Clamp to not overshoot
		if ( std::fabsf( move_x ) > std::fabsf( delta.x ) ) move_x = delta.x;
		if ( std::fabsf( move_y ) > std::fabsf( delta.y ) ) move_y = delta.y;

		// Calculate new viewangles
		auto new_view = view_angles + math::vector3{ move_x, move_y, 0.0f };
		math::helpers::normalize_angles( new_view );

		// Set viewangles
		systems::g_input.set_view_angles( new_view );

		// Set command angles. The bullet is corrected from where the crosshair
		// actually is at the end of the tick, so a shot taken while the view is
		// still gliding onto the target goes straight as well.
		auto command_aim = new_view;
		this->m_shot_intent = command_aim;
		this->m_has_shot_intent = true;
		this->set_command_angles( cmd, command_aim, aim_punch, false );
	}

	void legit::apply_triggerbot( systems::input::usercmd* cmd, const math::vector3& shoot_position, 
		const math::vector3& view_angles, const math::vector3& aim_punch, 
		const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local )
	{
		const auto& ctx = g_shared.ctx( );

		// A shot is held for exactly one command. Holding the button instead - the way
		// this used to work - let every weapon whose fire interval is shorter than the
		// hold fire again, which is what turned rifles and SMGs into a burst no matter
		// what the delay was set to.
		if ( this->m_trigger_release )
		{
			this->m_trigger_release = false;
			cmd->buttons.value &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_attack );
			cmd->buttons.value_changed |= static_cast< std::uintptr_t >( cstypes::command_buttons::in_attack );
			return;
		}

		auto corrected_angles = view_angles;
		if ( config.rcs.value && aim_punch.length_sqr( ) > 0.0001f )
		{
			corrected_angles.x += aim_punch.x;
			corrected_angles.y += aim_punch.y;
			math::helpers::normalize_angles( corrected_angles );
		}

		// The crosshair itself is the test: the ray leaves the eye along the angles the
		// shot will use and has to enter the hitbox. The old two degree cone around a
		// hitbox centre fired at players the crosshair was nowhere near, and it only
		// got looser the further away the target stood.
		constexpr auto k_trigger_ray_length{ 8192.0f };

		math::vector3 forward{};
		math::helpers::angle_vectors_left( corrected_angles, &forward, nullptr, nullptr );
		const auto ray_delta = forward * k_trigger_ray_length;

		// Capsules and boxes are both posed in bone space, so the ray is taken into
		// that space once and clipped against the hitbox there. The entry point it
		// reports is also the point the penetration test is run to.
		const auto ray_enters_hitbox = [ ]( const math::vector3& origin, const math::vector3& delta,
			const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float& fraction ) -> bool
		{
			if ( hitbox.radius > 0.001f )
			{
				return g_shared.ray_vs_capsule( origin, delta,
					bone.rotation.rotate_vector( hitbox.mins ) + bone.position,
					bone.rotation.rotate_vector( hitbox.maxs ) + bone.position,
					hitbox.radius, fraction );
			}

			auto inverse = bone.rotation;
			inverse.x = -inverse.x;
			inverse.y = -inverse.y;
			inverse.z = -inverse.z;

			const auto local_origin = inverse.rotate_vector( origin - bone.position );
			const auto local_delta = inverse.rotate_vector( delta );

			auto entry_time{ 0.0f };
			auto exit_time{ 1.0f };

			const auto clip_axis = [ & ]( float ray_origin, float ray_delta, float minimum, float maximum )
			{
				if ( std::fabsf( ray_delta ) < 1.0e-8f )
				{
					return ray_origin >= minimum && ray_origin <= maximum;
				}

				auto first = ( minimum - ray_origin ) / ray_delta;
				auto second = ( maximum - ray_origin ) / ray_delta;
				if ( first > second )
				{
					std::swap( first, second );
				}

				entry_time = std::max( entry_time, first );
				exit_time = std::min( exit_time, second );
				return entry_time <= exit_time;
			};

			if ( !clip_axis( local_origin.x, local_delta.x, hitbox.mins.x, hitbox.maxs.x )
				|| !clip_axis( local_origin.y, local_delta.y, hitbox.mins.y, hitbox.maxs.y )
				|| !clip_axis( local_origin.z, local_delta.z, hitbox.mins.z, hitbox.maxs.z ) )
			{
				return false;
			}

			fraction = entry_time;
			return true;
		};

		std::uintptr_t hit_pawn = 0;
		auto found = false;

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
				continue;

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
				continue;

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
				continue;

			const auto team = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
				continue;

			const auto health = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
				continue;

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
				continue;

			const auto records = g_shared.lc( ).get_valid_records( pawn );
			if ( records.empty( ) || !records.front( ) || !records.front( )->valid )
				continue;

			const auto rec = records.front( );
			const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );

			// Check if the crosshair ray actually reaches one of the hitboxes
			for ( const auto& entry : hitbox_set )
			{
				if ( entry.bone < 0 || entry.bone >= rec->bone_count )
					continue;

				const auto& bone = rec->bones[ entry.bone ];
				if ( bone.position.length_sqr( ) < 1.0f )
					continue;

				// Where the crosshair ray enters this hitbox, if it does at all.
				auto fraction{ 1.0f };
				if ( !ray_enters_hitbox( shoot_position, ray_delta, entry, bone, fraction ) )
					continue;

				const auto impact = shoot_position + ray_delta * fraction;

				// Check if we can hit
				const auto pen_ctx = g_shared.pen( ).prepare_target( pawn, rec );
				shared::penetration::result pen{};
				if ( !g_shared.pen( ).run( shoot_position, impact, pen_ctx, local.pawn, local.team, pen ) )
					continue;

				const auto hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( entry.index );
				if ( config.trigger_head_only.value && hitgroup != 1 )
					continue;

				const auto visible = !pen.penetrated;
				if ( !visible && !config.autowall.value )
					continue;

				if ( !visible && pen.damage < static_cast< float >( config.min_damage.value ) )
					continue;

				// Found valid target
				hit_pawn = pawn;
				found = true;
				break;
			}

			if ( found )
				break;
		}

		if ( !found )
		{
			this->m_trigger_pending_pawn = 0;
			return;
		}

		const auto now = ctx.current_time;

		// The delay is both the reaction time after the crosshair lands on somebody
		// and the wait before the next bullet, so the trigger can never outrun it.
		auto pace = static_cast< float >( config.trigger_delay.value ) / 1000.0f;

		// The weapon's own cycle time is the floor: one bullet per cycle is what the
		// delay is allowed to slow down, never to speed up.
		if ( ctx.weapon_vdata )
		{
			const auto cycle_time = memory::safe_read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) ).value_or( 0.0f );
			if ( cycle_time > 0.0f && cycle_time < 2.0f )
			{
				pace = std::max( pace, cycle_time );
			}
		}

		if ( this->m_trigger_pending_pawn != hit_pawn )
		{
			this->m_trigger_pending_pawn = hit_pawn;
			this->m_trigger_delay_start = now;
		}

		// m_trigger_delay_start is pushed one pace ahead of the shot below, so this
		// comparison holds the next bullet back for the full delay as well.
		if ( now - this->m_trigger_delay_start < pace )
			return;

		// The validated crosshair ray is the direction this bullet has to take.
		// No spread bends it once at the end of the tick, so the ray test above
		// stays untouched and no seed search runs on a tick that shoots nothing.
		this->m_shot_intent = corrected_angles;
		this->m_has_shot_intent = true;

		// Shoot! One bullet: the button goes down for this command and is let go
		// again on the next one.
		g_shared.last_shoot_tick( ) = memory::read<std::int32_t>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		cmd->buttons.value |= static_cast< std::uintptr_t >( cstypes::command_buttons::in_attack );
		cmd->buttons.value_changed |= static_cast< std::uintptr_t >( cstypes::command_buttons::in_attack );

		this->m_trigger_shot_this_tick = true;
		this->m_trigger_release = true;
		this->m_trigger_delay_start = now + pace;
	}

	bool legit::apply_no_spread( math::vector3& aim_angle, std::uintptr_t controller ) const
	{
		auto& shared_ctx = g_shared.ctx( );
		if ( !shared_ctx.valid || !shared_ctx.weapon )
		{
			return false;
		}

		// The correction has to be built from the spread state the server will use
		// for this bullet, so read it here instead of reusing a cached value. That
		// is what makes a shot taken mid-jump come out straight - the airborne
		// inaccuracy is part of the cone being cancelled.
		shared_ctx.spread = g_shared.get_spread( );
		shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );

		// An accurate shot already lands where the crosshair points, and the seed
		// search below would only hand back the same angle. Skipping it keeps the
		// first, accurate bullet of a burst free.
		if ( shared_ctx.inaccuracy + shared_ctx.spread < 0.0001f )
		{
			return false;
		}

		const auto tick_base = controller
			? memory::read<int>( controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) )
			: shared_ctx.current_tick;

		const auto corrected = g_shared.find_spread_correction( aim_angle, tick_base );

		// A zero angle means the seed search found nothing that cancels for this
		// tick; leave the caller's angle alone rather than aim nowhere.
		if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
		{
			return false;
		}

		aim_angle = corrected;
		return true;
	}

	void legit::set_command_angles( systems::input::usercmd* cmd, const math::vector3& aim_angle, const math::vector3& aim_punch, bool write_roll )
	{
		auto command_aim = aim_angle;
		command_aim.x -= aim_punch.x;
		command_aim.y -= aim_punch.y;

		// normalize_angles() zeroes the roll, which is exactly the part of a no
		// spread correction that cancels the spread, so keep it and put it back.
		const auto roll = command_aim.z;
		math::helpers::normalize_angles( command_aim );

		if ( write_roll )
		{
			command_aim.z = roll;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( base )
		{
			if ( const auto angles = base->mutable_viewangles( ) )
			{
				angles->set_x( command_aim.x );
				angles->set_y( command_aim.y );

				if ( write_roll )
				{
					angles->set_z( command_aim.z );
				}
			}
		}

		// Update input history
		const auto input_history_size = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < input_history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
				continue;

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( command_aim.x );
				angles->set_y( command_aim.y );

				if ( write_roll )
				{
					angles->set_z( command_aim.z );
				}
			}
		}
	}

	void legit::apply_shot_no_spread( systems::input::usercmd* cmd, const math::vector3& view_angles, const math::vector3& aim_punch )
	{
		// Only a bullet that actually leaves this tick is worth correcting: the
		// button has to be down and the weapon off its cooldown. Every other tick
		// returns here, so the seed search never runs in the idle path.
		if ( ( cmd->buttons.value & static_cast< std::uintptr_t >( cstypes::command_buttons::in_attack ) ) == 0 )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller || !local.pawn )
		{
			return;
		}

		// A ragebot shot owns the angles this tick; do not overwrite them.
		if ( g_rage.is_firing_this_tick( ) )
		{
			return;
		}

		// The trigger already fired its own bullet, and it advanced the shoot tick
		// while doing so, so only an unfired shot has to prove the weapon is ready.
		if ( !this->m_trigger_shot_this_tick && !g_shared.can_shoot( cmd, local.controller ) )
		{
			return;
		}

		// Aim and trigger hand over the direction they fired along. Without them
		// the bullet follows whatever the command already carries (the player's
		// view, or the anti-aim angles), which is the right thing to correct.
		auto shot_angle = view_angles;
		if ( this->m_has_shot_intent )
		{
			shot_angle = this->m_shot_intent;
		}
		else if ( const auto base = cmd->csgo_user_cmd.base( ) )
		{
			if ( const auto angles = base->viewangles( ) )
			{
				shot_angle = math::vector3{ angles->x( ), angles->y( ), 0.0f };
			}
		}

		// Could not find a cancelling seed for this tick: leave the command angles
		// exactly as they were instead of firing a bent one.
		if ( !this->apply_no_spread( shot_angle, local.controller ) )
		{
			return;
		}

		this->set_command_angles( cmd, shot_angle, aim_punch, true );
	}

	void legit::apply_rcs( math::vector3& aim_angle, const math::vector3& aim_punch, int rand_min, int rand_max ) const
	{
		if ( aim_punch.length_sqr( ) < 0.0001f )
			return;

		const auto factor = this->compute_rcs_factor( rand_min, rand_max );

		aim_angle.x -= aim_punch.x * factor;
		aim_angle.y -= aim_punch.y * factor;
		math::helpers::normalize_angles( aim_angle );
	}

	void legit::update_standalone_rcs( const math::vector3& view_angles, const math::vector3& aim_punch, 
		int amount, int rand_min, int rand_max, bool apply, const systems::local::snapshot& local )
	{
		const auto shots_fired = memory::read<int>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_hash ) );
		if ( shots_fired > 1 )
		{
			const auto factor = this->compute_rcs_factor( rand_min, rand_max );
			const auto scale = static_cast< float >( amount ) / 100.0f;

			const auto punch_scaled = math::vector3
			{
				aim_punch.x * scale * factor,
				aim_punch.y * scale * factor,
				0.0f
			};

			if ( apply )
			{
				auto new_angles = view_angles;
				new_angles.x += this->m_old_punch.x - punch_scaled.x;
				new_angles.y += this->m_old_punch.y - punch_scaled.y;
				math::helpers::normalize_angles( new_angles );

				systems::g_input.set_view_angles( new_angles );
			}

			this->m_old_punch = punch_scaled;
		}
		else
		{
			this->m_old_punch = {};
		}
	}

	float legit::compute_rcs_factor( int rand_min, int rand_max ) const
	{
		const auto seed = static_cast< std::uint32_t >( g_shared.ctx( ).current_time * 1000.0f );
		const auto t = static_cast< float >( seed % 1000 ) / 1000.0f;
		const auto min_scale = static_cast< float >( rand_min ) / 100.0f;
		const auto max_scale = static_cast< float >( rand_max ) / 100.0f;
		return min_scale + ( max_scale - min_scale ) * t;
	}

	void legit::draw_fov( xdraw::draw_list& draw_list, const math::vector3& view_angles, const math::vector3& aim_punch, 
		float fov_degrees, const config::col& color, bool rcs_active ) const
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( screen_w );
		const auto sh = static_cast< float >( screen_h );

		const auto camera_fov_rad = math::helpers::deg_to_rad( systems::g_view.fov( ) );
		const auto aimbot_fov_rad = math::helpers::deg_to_rad( fov_degrees );
		const auto radius = std::tanf( aimbot_fov_rad ) / std::tanf( camera_fov_rad * 0.5f ) * ( sw * 0.5f );

		const auto cx = sw * 0.5f;
		const auto cy = sh * 0.5f;
		const auto& c = color.value;

		draw_list.circle( cx, cy, radius, xdraw::color{ c.r, c.g, c.b, c.a }, 1.5f, 64 );
	}

	int legit::hitgroup_to_cfg( int hitgroup )
	{
		static constexpr int table[ ]{ -1, 0, 1, 2, 3, 3, 4, 4 };

		if ( hitgroup < 0 || hitgroup >= static_cast< int >( std::size( table ) ) )
			return -1;

		return table[ hitgroup ];
	}

} // namespace features::combat
