// ============================================================================
// RAGE.CPP - Completely Rewritten Advanced CS2 Ragebot
// ============================================================================
// Features:
// - Adaptive resolver with anti-aim detection (Static, Jitter, Spin, Fake, LBY)
// - Predictive yaw calculation with cycle detection
// - Advanced hitchance model with weapon-specific calculations
// - Multi-point scanning with dynamic safety scoring
// - Full autowall with multi-surface penetration
// - Backtrack integration with optimal tick selection
// - No-spread seed prediction and compensation
// - Adaptive learning from shot feedback
// ============================================================================

#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	// ========================================================================
	// RESOLVER - Advanced Anti-Aim Detection & Prediction
	// ========================================================================
	
	enum class AAState : uint8_t {
		STATIC = 0,
		LBY,
		JITTER,
		FAKE,
		SPIN,
		UNKNOWN,
		COUNT
	};

	struct PlayerHistory {
		std::deque<float> yaw_samples;
		std::deque<float> yaw_deltas;
		AAState last_state = AAState::UNKNOWN;
		uint32_t hits = 0;
		uint32_t misses = 0;
		float confidence[static_cast<size_t>(AAState::COUNT)] = {};
		int cycle_period = 0;
		float cycle_confidence = 0.f;
		// Last tick this player's yaw was sampled, so a ragebot pass that runs
		// twice in one command (auto revolver) cannot double-feed the history.
		int last_feed_tick = -1;
	};

	// Global resolver state per player
	static std::unordered_map<std::uintptr_t, PlayerHistory> g_resolver_history;

	// ========================================================================
	// RESOLVER HELPER FUNCTIONS
	// ========================================================================

	static inline float normalize_yaw_f( float y ) {
		while ( y > 180.f ) y -= 360.f;
		while ( y < -180.f ) y += 360.f;
		return y;
	}

	static inline float yaw_delta_f( float a, float b ) {
		return normalize_yaw_f( a - b );
	}

	static void feed_resolver_yaw( std::uintptr_t pawn, float yaw ) {
		auto& h = g_resolver_history[pawn];
		if ( !h.yaw_samples.empty( ) ) {
			h.yaw_deltas.push_back( yaw_delta_f( yaw, h.yaw_samples.back( ) ) );
			while ( h.yaw_deltas.size( ) > 96 )
				h.yaw_deltas.pop_front( );
		}
		h.yaw_samples.push_back( yaw );
		while ( h.yaw_samples.size( ) > 96 )
			h.yaw_samples.pop_front( );
	}

	static void report_resolver_shot( std::uintptr_t pawn, bool hit ) {
		auto& h = g_resolver_history[pawn];
		if ( hit ) h.hits++;
		else h.misses++;

		if ( h.last_state != AAState::UNKNOWN ) {
			size_t i = static_cast<size_t>( h.last_state );
			h.confidence[i] = std::clamp( h.confidence[i] + ( hit ? 0.12f : -0.18f ), 0.f, 1.f );
		}

	}

	static AAState classify_aa_state( PlayerHistory& h ) {
		if ( h.yaw_samples.size( ) < 10 )
			return AAState::UNKNOWN;

		float abs_sum = 0.f;
		for ( float d : h.yaw_deltas )
			abs_sum += std::fabs( d );
		float avg_speed = abs_sum / std::max<size_t>( 1, h.yaw_deltas.size( ) );

		float mn = h.yaw_samples.front( ), mx = h.yaw_samples.front( );
		for ( float y : h.yaw_samples ) {
			mn = std::min( mn, y );
			mx = std::max( mx, y );
		}
		float spread = mx - mn;

		AAState st;
		if ( avg_speed < 0.4f && spread < 1.5f )
			st = AAState::STATIC;
		else if ( avg_speed > 16.f )
			st = AAState::SPIN;
		else if ( spread > 25.f && avg_speed > 2.5f )
			st = AAState::JITTER;
		else if ( spread > 25.f )
			st = AAState::FAKE;
		else if ( spread < 6.f )
			st = AAState::LBY;
		else
			st = AAState::UNKNOWN;

		if ( st == h.last_state ) {
			size_t i = static_cast<size_t>( st );
			h.confidence[i] = std::clamp( h.confidence[i] + 0.06f, 0.f, 1.f );
		}
		h.last_state = st;
		return st;
	}

	static void detect_resolver_cycle( PlayerHistory& h ) {
		const auto& d = h.yaw_deltas;
		if ( d.size( ) < 32 ) {
			h.cycle_period = 0;
			h.cycle_confidence = 0.f;
			return;
		}

		float mean = 0.f;
		for ( float v : d )
			mean += v;
		mean /= static_cast<float>( d.size( ) );

		int best_lag = 0;
		float best_corr = 0.f;

		for ( int lag = 6; lag <= 24; ++lag ) {
			if ( static_cast<int>( d.size( ) ) < lag * 2 )
				break;

			float num = 0.f, d1 = 0.f, d2 = 0.f;
			for ( size_t i = lag; i < d.size( ); ++i ) {
				float a = d[i] - mean;
				float b = d[i - lag] - mean;
				num += a * b;
				d1 += a * a;
				d2 += b * b;
			}

			if ( d1 <= 0.f || d2 <= 0.f )
				continue;

			float corr = num / std::sqrt( d1 * d2 );
			if ( corr > best_corr ) {
				best_corr = corr;
				best_lag = lag;
			}
		}

		if ( best_corr > 0.62f ) {
			h.cycle_period = best_lag;
			h.cycle_confidence = best_corr;
		}
		else {
			h.cycle_period = 0;
			h.cycle_confidence = 0.f;
		}
	}

	static float predict_next_yaw( const PlayerHistory& h, AAState st, float cur ) {
		if ( h.yaw_deltas.empty( ) )
			return cur;

		float last = h.yaw_deltas.back( );

		// Cycle-based prediction
		if ( h.cycle_period > 0 && h.cycle_confidence > 0.62f &&
			static_cast<int>( h.yaw_deltas.size( ) ) > h.cycle_period ) {
			float cycled = h.yaw_deltas[h.yaw_deltas.size( ) - h.cycle_period];
			return normalize_yaw_f( cur + cycled * h.cycle_confidence );
		}

		// State-based prediction
		switch ( st ) {
		case AAState::STATIC:
			return cur;
		case AAState::LBY:
			return normalize_yaw_f( cur + last * 0.92f );
		case AAState::JITTER:
			return normalize_yaw_f( cur - last * 1.12f );
		case AAState::FAKE: {
			float mn = h.yaw_samples.front( ), mx = h.yaw_samples.front( );
			for ( float y : h.yaw_samples ) {
				mn = std::min( mn, y );
				mx = std::max( mx, y );
			}
			float mid = ( mn + mx ) * 0.5f;
			return normalize_yaw_f( cur + ( mid - cur ) * 0.72f );
		}
		case AAState::SPIN: {
			float s = 0.f;
			for ( float d : h.yaw_deltas )
				s += d;
			return normalize_yaw_f( cur + ( s / static_cast<float>( h.yaw_deltas.size( ) ) ) * 1.25f );
		}
		default:
			return cur;
		}
	}

	// ========================================================================
	// RESOLVER CANDIDATES & SHOT FEEDBACK
	// ========================================================================

	// How many candidate rotations scan_player will test: the two desync
	// extremes plus, at most, one predicted turn-cycle offset.
	static constexpr int k_resolver_max_offsets = 3;

	// Score cost charged to every point that came from a rotated pose. It has to
	// be big enough that the recorded pose wins whenever it produces any usable
	// hit (so a working resolver never makes the ragebot worse), yet small enough
	// that a rotated pose which actually reaches the body still beats no shot.
	static constexpr float k_resolver_penalty = 75.0f;

	// Rotate a point about the player origin in the horizontal plane. Bone heads
	// and hitbox corners both live in world space, so rotating about the origin
	// moves the whole pose onto a different body yaw while keeping the height.
	static math::vector3 rotate_point_yaw( const math::vector3& point, const math::vector3& origin, float degrees ) {
		if ( degrees == 0.0f ) {
			return point;
		}

		const auto rad = degrees * ( 3.14159265358979f / 180.0f );
		const auto c = std::cosf( rad );
		const auto s = std::sinf( rad );
		const auto dx = point.x - origin.x;
		const auto dy = point.y - origin.y;

		return { origin.x + dx * c - dy * s, origin.y + dx * s + dy * c, point.z };
	}

	// Shot feedback. The pending shot is remembered so a player_hurt arriving a
	// few ticks later can be attributed to the resolver state that produced it.
	static std::mutex g_resolver_mtx;
	static std::uintptr_t g_resolver_pending_pawn{};
	static int g_resolver_pending_tick{ -1 };

	void reset_resolver( ) {
		std::lock_guard lock( g_resolver_mtx );
		g_resolver_history.clear( );
		g_resolver_pending_pawn = 0;
		g_resolver_pending_tick = -1;
	}

	void on_resolver_shot_fired( std::uintptr_t pawn ) {
		std::lock_guard lock( g_resolver_mtx );

		// A previous shot nobody reported a hit for is a miss. Attribute it before
		// arming the new one so the confidence reflects real outcomes.
		if ( g_resolver_pending_pawn && g_resolver_pending_tick >= 0 ) {
			const auto elapsed = g_shared.ctx( ).current_tick - g_resolver_pending_tick;
			if ( elapsed > 0 && elapsed <= 16 ) {
				report_resolver_shot( g_resolver_pending_pawn, false );
			}
		}

		g_resolver_pending_pawn = pawn;
		g_resolver_pending_tick = g_shared.ctx( ).current_tick;
	}

	void on_resolver_target_hit( std::uintptr_t victim_pawn ) {
		if ( !victim_pawn ) {
			return;
		}

		std::lock_guard lock( g_resolver_mtx );

		if ( g_resolver_pending_pawn == victim_pawn ) {
			g_resolver_pending_pawn = 0;
			g_resolver_pending_tick = -1;
		}

		report_resolver_shot( victim_pawn, true );
	}

	// ========================================================================
	// MAIN RAGEBOT ENTRY POINT
	// ========================================================================

	void rage::on_create_move( systems::input::usercmd* cmd )
	{
		auto& ctx = g_shared.ctx( );
		const auto local = systems::g_local.get( );
		this->update_penetration_crosshair( local );

		if ( !ctx.valid )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		this->m_should_stop = false;
		this->m_firing_this_tick = false;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_release_duck_for_shot = false;
			this->m_duckpeek_reduck = false;
		}

		if ( this->m_zeus_fired )
		{
			this->m_zeus_fired = false;

			if ( settings::g_combat.m_zeusbot.drop_after && !systems::g_local.is_in_deathmatch( ) )
			{
				memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, "drop", 0x7ffef001 );
			}

			return;
		}

		const auto is_knife = ctx.weapon_type == cstypes::weapon_type::knife;
		const auto is_taser = ctx.weapon_type == cstypes::weapon_type::taser;

		if ( !is_knife && !is_taser && ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg ) )
		{
			return;
		}

		// Knifebot has been removed - only zeusbot and ragebot are enabled
		const auto enabled = is_taser ? settings::g_combat.m_zeusbot.enabled
			: settings::g_combat.m_ragebot.enabled;
		if ( !enabled )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		// Skip knife attacks since knifebot is removed
		if ( is_knife )
		{
			return;
		}

		auto aim_ctx = this->build_context( cmd, local );

		if ( is_taser )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_taser( cmd, aim_ctx, local );
		}
		else if ( ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
		{
			this->auto_revolver( cmd, aim_ctx, local );
		}
		else
		{
			this->m_revolver_cock_ticks = 0;

			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_gun( cmd, aim_ctx, local );
		}
	}

	void rage::on_render( xdraw::draw_list& draw_list )
	{
		this->draw_penetration_crosshair( draw_list );

		// VISUALIZE AIMBOT: Show hitboxes on players
		const auto& shared_ctx = g_shared.ctx( );
		if ( shared_ctx.valid && settings::g_combat.m_ragebot.enabled.value )
		{
			const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
			if ( config.visualize_aimbot.value )
			{
				const auto local = systems::g_local.get( );
				if ( local.pawn && local.is_alive )
				{
					this->draw_visualize_aimbot( draw_list, config, local );
				}
			}
		}

		// Debug multipoints feature has been removed
		return;

		std::lock_guard lock( m_debug_mtx );

		for ( const auto& pt : m_debug_points )
		{
			const auto screen = systems::g_view.project( pt.position );
			if ( !systems::g_view.projection_valid( screen ) )
			{
				continue;
			}

			xdraw::color col{};
			switch ( pt.hitbox_index )
			{
			case 0:
				col = { 255, 80,  80  }; break; // head — red
			case 2: case 3:
				col = { 220, 220, 60  }; break; // stomach — yellow
			case 4: case 5: case 6:
				col = { 255, 160, 60  }; break; // chest — orange
			case 7: case 8: case 9: case 10: case 11: case 12:
				col = { 80,  160, 255 }; break; // legs — blue
			case 13: case 14: case 15: case 16: case 17: case 18:
				col = { 180, 80,  255 }; break; // arms — purple
			default:
				col = { 200, 200, 200 }; break;
			}

			const auto alpha  = pt.is_center ? std::uint8_t{ 255 } : std::uint8_t{ 160 };
			const auto radius = pt.is_center ? 3.5f : 2.0f;

			draw_list.circle_filled( screen.x, screen.y, radius, col.alpha( alpha ) );
		}
	}

	rage::aim_context rage::build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const
	{
		auto& ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );

		aim_context out{};
		out.velocity = prestate.velocity;
		out.spread = g_shared.get_spread( );
		out.predicted_inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				g_shared.sh( ).snapshot( local.pawn, ctx.weapon_services );

				out.velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
				out.spread = g_shared.get_spread( );
				out.predicted_inaccuracy = g_shared.get_inaccuracy( true );
			} );

		ctx.spread = out.spread;
		ctx.inaccuracy = out.predicted_inaccuracy;

		out.view_angles = systems::g_input.get_view_angles( );
		out.on_ground = ( prestate.flags & cstypes::entity_flags::on_ground ) != 0;
		out.is_scoped = ctx.is_scoped;
		out.weapon_max_speed = ctx.weapon_max_speed;
		out.accurate_threshold = ctx.weapon_max_speed * 0.34f;

		return out;
	}

	bool rage::process_doubletap( systems::input::usercmd* cmd, const systems::local::snapshot& local, bool charge_dt )
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		if ( !config.doubletap.value )
		{
			return false;
		}

		const auto base_cmd = cmd->csgo_user_cmd.mutable_base( );
		if ( !base_cmd )
		{
			return false;
		}

		const auto client_tick = base_cmd->client_tick( );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		// IMPROVED DOUBLETAP: Better timing and reliability
		// Check weapon state more thoroughly
		const auto is_reloading = memory::read<bool>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) );
		const auto clip_ammo = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) );
		
		// Ensure we have at least 2 bullets for doubletap
		if ( clip_ammo < 2 || is_reloading )
		{
			return false;
		}

		// More lenient tick check - allow shooting if we're within 1 tick of being ready
		const auto can_attack = client_tick >= ( next_primary - 1 ) && 
			tick_base >= g_shared.last_shoot_tick( ) + 1 && 
			clip_ammo >= 2 && 
			!is_reloading;

		const auto should_attack = can_attack && charge_dt;

		if ( should_attack )
		{
			if ( const auto subtick_moves = base_cmd->mutable_subtick_moves( ) )
			{
				const auto old_size = subtick_moves->m_current_size;
				const auto press = systems::g_input.acquire_subtick_step( subtick_moves );
				const auto release = systems::g_input.acquire_subtick_step( subtick_moves );
				if ( press && release )
				{
					press->set_button( cstypes::command_buttons::in_attack );
					press->set_pressed( true );
					press->set_when( 0.0f );
					press->set_analog_forward_delta( 0.0f );
					press->set_analog_left_delta( 0.0f );

					release->set_button( cstypes::command_buttons::in_attack );
					release->set_pressed( false );
					// Slightly earlier release for better doubletap timing
					release->set_when( 0.95f );
					release->set_analog_forward_delta( 0.0f );
					release->set_analog_left_delta( 0.0f );

					// The subtick release is the final state. Do not let the
					// base command turn this pulse into a held attack.
					cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
					cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
					cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
					return true;
				}

				// Do not leave a partial press in the command if allocation of
				// the matching release failed.
				subtick_moves->m_current_size = old_size;
			}
		}

		cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
		cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
		return false;
	}

	std::optional<rage::stop_prediction> rage::predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		auto predicted_velocity = ctx.velocity;
		const auto speed = predicted_velocity.length_2d( );
		const auto will_stop = ctx.on_ground && ( speed > ctx.accurate_threshold || ( ctx.is_scoped && speed > 1.0f ) );

		if ( !will_stop )
		{
			return std::nullopt;
		}

		auto sim_vel = predicted_velocity;
		sim_vel.z = 0.0f;
		predicted_velocity.z = 0.0f;

		const auto sv_friction = CONVAR("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR("sv_accelerate")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_move_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;

		for ( auto i = 0; i < 15; ++i )
		{
			const auto sim_speed = sim_vel.length_2d( );
			if ( sim_speed < 1.0f )
			{
				break;
			}

			const auto control = std::fmaxf( sim_speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( sim_speed - drop, 0.0f );
			auto accel = sv_accelerate;

			if ( shared_ctx.is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, shared_ctx.weapon_max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * shared_ctx.weapon_max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			if ( new_speed > 0.0f )
			{
				sim_vel *= ( new_speed / sim_speed );
			}
			else
			{
				sim_vel = {};
				break;
			}
		}

		const auto avg_vel = ( predicted_velocity + sim_vel ) * 0.5f;
		const auto stop_ticks = g_shared.calculate_stop_ticks( predicted_velocity, shared_ctx.weapon_max_speed, local.pawn );
		const auto stop_time = static_cast< float >( stop_ticks ) * cstypes::tick_interval;

		return stop_prediction
		{
			.eye =
			{
				current_eye.x + avg_vel.x * stop_time,
				current_eye.y + avg_vel.y * stop_time,
				current_eye.z
			},
			.inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, sim_vel )
		};
	}

	std::vector<rage::candidate> rage::gather_candidates( const systems::local::snapshot& local, float max_distance_sq ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		std::vector<candidate> out;
		out.reserve( players.size( ) );

		const_cast<rage*>( this )->m_extrapolated_records.clear( );
		const_cast<rage*>( this )->m_extrapolated_records.reserve( players.size( ) );

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

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			auto records = g_shared.lc( ).get_valid_records( pawn );
			auto extrap = g_shared.lc( ).extrapolate( pawn );

			if ( extrap )
			{
				// Occlusion pre-check: if solid world geometry blocks the line from our
				// eye to the extrapolated origin the autowall traces will all fail anyway.
				// Dropping the extrap record early avoids wasting CPU on those traces and,
				// more importantly, stops the ragebot from locking onto a position it
				// cannot actually hit — which was the main cause of misses when a target
				// moved behind cover during the extrapolation window.
				//
				// The mask 0x1c3003 matches what penetration::run uses (all world +
				// static props), so the same geometry that blocks bullets blocks here.
				// We skip the target pawn itself so partial occluders on the player
				// model don't reject a valid extrap; the real scan will handle those.
				const auto local_eye = g_shared.get_eye_position( local.pawn );
				const auto extrap_origin = extrap->origin;
				const auto los = systems::g_tracing.trace(
					local_eye,
					{ extrap_origin.x, extrap_origin.y, extrap_origin.z + 36.0f }, // chest height
					local.pawn,
					0x1c3003, 4
				);

				// fraction == 1.0 means nothing solid was hit — origin is reachable
				if ( los.fraction >= 0.97f )
				{
					const_cast< rage* >( this )->m_extrapolated_records.push_back( std::move( *extrap ) );
					records.push_back( &const_cast< rage* >( this )->m_extrapolated_records.back( ) );
				}
			}

			if ( records.empty( ) )
			{
				continue;
			}

			if ( max_distance_sq > 0.0f )
			{
				const auto& origin = systems::g_prediction.pre( ).origin;
				const auto delta_front = records.front( )->origin - origin;
				auto closest_sq = delta_front.x * delta_front.x + delta_front.y * delta_front.y + delta_front.z * delta_front.z;

				if ( records.size( ) > 1 )
				{
					const auto delta_back = records.back( )->origin - origin;
					const auto back_sq = delta_back.x * delta_back.x + delta_back.y * delta_back.y + delta_back.z * delta_back.z;
					closest_sq = std::min( closest_sq, back_sq );
				}

				if ( closest_sq > max_distance_sq )
				{
					continue;
				}
			}

			candidate c{};
			c.pawn = pawn;
			c.health = health;
			c.armor = memory::read<int>( pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );

			// Resolver: classify the enemy's anti-aim from their live eye yaw and
			// turn that into the extra pose rotations scan_player should test. The
			// recorded pose is still scanned untouched, so this only ever adds
			// options for a target that is genuinely desyncing.
			if ( settings::g_combat.m_ragebot.general.resolver.value )
			{
				// The hurt event runs on the game's event thread and inserts into the
				// same map, so all history access shares this lock.
				std::lock_guard resolver_lock( g_resolver_mtx );

				const auto eye_angles = memory::read<math::vector3>( pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
				auto& hist = g_resolver_history[ pawn ];
				const auto tick = g_shared.ctx( ).current_tick;

				// Sampling and classification both carry state, so only run them once
				// per tick even if the ragebot plans more than one shot this command.
				if ( hist.last_feed_tick != tick )
				{
					hist.last_feed_tick = tick;
					feed_resolver_yaw( pawn, eye_angles.y );
					hist.last_state = classify_aa_state( hist );
					detect_resolver_cycle( hist );
				}

				const auto desync = std::clamp( static_cast< float >( settings::g_combat.m_ragebot.general.resolver_desync.value ), 5.0f, 60.0f );

				// A jitter or static anti-aim sits at one of the two desync extremes.
				c.resolver_offsets[ c.resolver_offset_count++ ] = -desync;
				c.resolver_offsets[ c.resolver_offset_count++ ] = desync;

				// A repeating turn cycle says where the next pose lands even when
				// neither extreme matches the pose the body is actually on.
				if ( hist.cycle_period > 0 && hist.cycle_confidence > 0.62f && c.resolver_offset_count < k_resolver_max_offsets )
				{
					const auto predicted = predict_next_yaw( hist, hist.last_state, eye_angles.y );
					const auto delta = yaw_delta_f( predicted, eye_angles.y );

					if ( std::fabs( delta ) > 1.0f )
					{
						c.resolver_offsets[ c.resolver_offset_count++ ] = std::clamp( delta, -170.0f, 170.0f );
					}
				}

			}

			const auto pick_record_indices = [ &records ]( std::array<int, k_max_scan_records>& out_indices ) -> int
				{
					const auto count = records.size( );
					if ( count == 0 )
					{
						return 0;
					}

					auto picked{ 0 };
					const auto add_index = [ & ]( int idx )
						{
							if ( picked >= k_max_scan_records )
							{
								return;
							}

							for ( auto i = 0; i < picked; ++i )
							{
								if ( out_indices[ i ] == idx )
								{
									return;
								}
							}

							out_indices[ picked++ ] = idx;
						};

					add_index( 0 );

					if ( count > 2 )
					{
						add_index( static_cast< int >( count / 2 ) );
					}

					if ( count > 1 )
					{
						add_index( static_cast< int >( count - 1 ) );
					}

					return picked;
				};

			std::array<int, k_max_scan_records> record_indices{};
			const auto picked_count = pick_record_indices( record_indices );

			for ( auto i = 0; i < picked_count; ++i )
			{
				c.records[ i ] = records[ static_cast< std::size_t >( record_indices[ i ] ) ];
			}

			c.record_count = picked_count;

			if ( shared_ctx.weapon_type >= cstypes::weapon_type::pistol && shared_ctx.weapon_type <= cstypes::weapon_type::lmg )
			{
				const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
				c.min_damage = this->get_min_damage( config, health, config.min_damage_override.value );
			}

			out.push_back( c );
		}

		return out;
	}

	void rage::run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire )
	{
		// Check if any weapon group has no_spread enabled
		bool any_no_spread = false;
		for ( const auto& group : settings::g_combat.m_ragebot.groups )
		{
			if ( group.no_spread.value )
			{
				any_no_spread = true;
				break;
			}
		}

		// Allow no spread to work even if ragebot is disabled
		if ( !settings::g_combat.m_ragebot.enabled && !any_no_spread )
		{
			return;
		}

		auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		
		// Auto-scope: crouch = scope, uncrouch = unscope
		// Natural behavior: hold crouch to scope, release to unscope
		if ( config.auto_scope.value )
		{
			const bool is_crouching = ( cmd->buttons.value & cstypes::command_buttons::in_duck ) != 0;
			
			if ( is_crouching && !ctx.is_scoped )
			{
				// Crouching but not scoped — scope in after optional delay
				if ( this->m_auto_scope_delay_ticks >= config.auto_scope_delay.value )
				{
					cmd->buttons.value |= cstypes::command_buttons::in_zoom;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_zoom;
					this->m_auto_scope_delay_ticks = 0;
				}
				else
				{
					this->m_auto_scope_delay_ticks++;
				}
			}
			else if ( !is_crouching && ctx.is_scoped )
			{
				// Released crouch while scoped — unscope
				cmd->buttons.value |= cstypes::command_buttons::in_zoom;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_zoom;
				this->m_auto_scope_delay_ticks = 0;
			}
			else if ( !is_crouching )
			{
				this->m_auto_scope_delay_ticks = 0;
			}
		}


		const auto finish_doubletap = [ & ]( bool charge_dt )
		{
			if ( config.doubletap.value && shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver )
			{
				return this->process_doubletap( cmd, local, charge_dt );
			}

			return false;
		};

		auto candidates = this->gather_candidates( local );

		{
			std::lock_guard lock( m_debug_mtx );
			m_debug_points.clear( );
		}

		if ( candidates.empty( ) )
		{
			finish_doubletap( false );
			// Clear auto-stop if no candidates
			this->m_should_stop = false;
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			auto shoot_position = g_shared.get_shoot_position( );
			if ( shoot_position.length_sqr( ) < 1.0f )
			{
				shoot_position = g_shared.get_eye_position( local.pawn );
			}

			eye_candidates.entries[ 0 ].position = shoot_position;
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		const auto scan_from_eye_candidates = [ & ]( const math::vector3& eye_offset, float inaccuracy )
		{
			std::vector<scan_hit> hits_out;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				const auto eye = eye_candidates.entries[ i ].position + eye_offset;
				auto hits = this->scan_players( eye, inaccuracy, ctx, candidates, local );
				auto found_direct{ false };

				for ( auto& hit : hits )
				{
					auto source_eye = eye_candidates.entries[ i ];
					source_eye.position = eye;
					hit.source_eye = source_eye;
					found_direct = found_direct || !hit.penetrated;
					hits_out.push_back( std::move( hit ) );
				}

				if ( found_direct )
				{
					break;
				}
			}

			return hits_out;
		};

		if ( config.no_spread.value )
		{
			// ===== MASSIVELY IMPROVED NO SPREAD SYSTEM =====
			shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );
			
			// Scan with perfect accuracy (no inaccuracy)
			auto all_hits = scan_from_eye_candidates( {}, 0.0f );

			if ( all_hits.empty( ) )
			{
				finish_doubletap( false );
				return;
			}

			// Enhanced target selection for no-spread mode
			const auto best = this->select_best( ctx, all_hits, 0.0f );
			if ( !best.valid )
			{
				finish_doubletap( false );
				return;
			}

			// Apply damage overrides for no spread mode
			float min_damage_threshold = static_cast<float>( config.min_damage.value );
			if ( config.nospread_min_damage_override.value )
			{
				min_damage_threshold = static_cast<float>( config.nospread_min_damage.value );
			}

			float max_damage_cap = FLT_MAX;
			if ( config.nospread_max_damage_cap.value )
			{
				max_damage_cap = static_cast<float>( config.nospread_max_damage.value );
			}

			// Reject if damage doesn't meet thresholds
			if ( best.hit.damage < min_damage_threshold || best.hit.damage > max_damage_cap )
			{
				finish_doubletap( false );
				return;
			}

			// Additional accuracy checks for no-spread
			const auto distance_to_target = best.hit.source_eye.position.distance( best.hit.position );
			
			// Don't fire if target is too far for reliable no-spread (configurable by weapon)
			const auto max_nospread_range = 5000.0f; // 50 meters
			if ( distance_to_target > max_nospread_range )
			{
				finish_doubletap( false );
				return;
			}

			if ( !allow_fire )
			{
				return;
			}

			const auto subtick_attack = finish_doubletap( true );
			this->fire_gun( cmd, best, false, best.hit.source_eye.position, local, subtick_attack );
			return;
		}

		const auto primary_eye = eye_candidates.entries[ 0 ].position;
		const auto& prestate = systems::g_prediction.pre( );

		// Current-shot selection is always based on current engine shoot-history.
		auto current_hits = scan_from_eye_candidates( {}, ctx.predicted_inaccuracy );
		const auto best = this->select_best( ctx, current_hits, ctx.predicted_inaccuracy );
		
		// Pixel Wallbang: EXTREMELY AGGRESSIVE - shoot ANY penetrable target
		// Bypasses ALL checks: hitchance, damage, accuracy, everything
		if ( config.pixel_wallbang.value && best.valid && best.hit.penetrated )
		{
			// Shoot immediately, no questions asked
			// Skip all other logic including auto-stop, duckpeek, everything
			if ( allow_fire )
			{
				// Force fire even with doubletap
				const auto subtick_attack = config.doubletap.value && shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver
					? this->process_doubletap( cmd, local, true )
					: false;
				
				this->fire_gun( cmd, best, true, best.hit.source_eye.position, local, subtick_attack );
			}
			return; // Exit immediately, don't do any other checks
		}
		
		// Also check for ANY wallbang opportunity even if not the best target
		if ( config.pixel_wallbang.value )
		{
			for ( const auto& hit : current_hits )
			{
				if ( hit.penetrated && hit.damage > 0.0f ) // Any damage through wall = shoot
				{
					if ( allow_fire )
					{
						const auto subtick_attack = config.doubletap.value && shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver
							? this->process_doubletap( cmd, local, true )
							: false;
						
						target wallbang_target;
						wallbang_target.hit = hit;
						wallbang_target.valid = true;
						wallbang_target.hitchance = 1.0f;
						wallbang_target.score = hit.score;
						
						this->fire_gun( cmd, wallbang_target, true, hit.source_eye.position, local, subtick_attack );
					}
					return; // Found wallbang, shoot immediately
				}
			}
		}

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( config.hitchance ) / 100.0f;
		const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
		const auto is_ducked = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;

		const auto standing_inaccuracy = duckpeek_active ? this->get_standing_inaccuracy( local, ctx ) : ctx.predicted_inaccuracy;
		const auto standing_hc = best.valid
			? ( duckpeek_active ? this->evaluate_hitchance( best.hit, ctx, standing_inaccuracy, config ) : best.hitchance )
			: 0.0f;

		// Make force shot in air much more aggressive
		const auto accurate = best.valid && standing_hc >= needed_hc;
		const auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		
		// Force shot in air now works with lower accuracy requirements
		const auto force = best.valid && ( ctx.on_ground ? ( config.force_shot.value && max_acc ) : ( config.force_shot_air.value ) );
		const auto shot_viable = accurate || force;

		// Apply auto-stop ONLY if we have a valid target and will shoot
		if ( best.valid && shot_viable && ( shared_ctx.valid && local.pawn && local.is_alive ) )
		{
			this->apply_auto_stop( cmd, ctx, config, local );
		}
		else if ( best.valid )
		{
			// Have target but not ready to shoot - check if we should stop for accuracy
			if ( !shot_viable && this->should_stop_movement( ctx ) )
			{
				const auto stop = this->predict_stop( ctx, primary_eye, local );
				if ( stop )
				{
					const auto future_offset = stop->eye - primary_eye;
					auto planned_hits = scan_from_eye_candidates( future_offset, stop->inaccuracy );
					const auto planned = this->select_best( ctx, planned_hits, stop->inaccuracy );
					if ( planned.valid )
					{
						// Apply auto-stop for future shot
						this->apply_auto_stop( cmd, ctx, config, local );
					}
				}
			}
		}
		else
		{
			// No valid target - clear auto-stop
			this->m_should_stop = false;
			this->m_auto_stop_state = { };
		}

		if ( !best.valid )
		{
			finish_doubletap( false );
			return;
		}

		if ( duckpeek_active && allow_fire )
		{
			if ( shot_viable )
			{
				this->m_release_duck_for_shot = true;
			}
			else if ( !this->m_duckpeek_reduck )
			{
				this->m_release_duck_for_shot = false;
			}
		}

		auto ready_to_fire = shot_viable;
		if ( duckpeek_active )
		{
			if ( is_ducked )
			{
				ready_to_fire = false;
			}
			else
			{
				ready_to_fire = ready_to_fire && this->m_release_duck_for_shot;
			}
		}

		if ( ready_to_fire && allow_fire )
		{
			const auto subtick_attack = finish_doubletap( true );
			this->fire_gun( cmd, best, !accurate && force, best.hit.source_eye.position, local, subtick_attack );

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}
		}
		else
		{
			finish_doubletap( false );
		}
	}

	void rage::run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_zeusbot.enabled )
		{
			return;
		}

		auto candidates = this->gather_candidates( local );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_taser( eye_candidates.entries[ i ].position, ctx, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};

		for ( const auto& h : all_hits )
		{
			if ( !best.valid || h.score > best.score )
			{
				best.hit = h;
				best.hitchance = 1.0f;
				best.score = h.score;
				best.valid = true;
			}
		}

		if ( best.valid )
		{
			this->m_zeus_fired = true;
			this->fire_melee( cmd, best, local );
		}
	}

	void rage::run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		// Knifebot has been removed - function disabled
		return;

		const auto info = this->get_knife_info( local );
		if ( !info.can_slash && !info.can_stab )
		{
			return;
		}

		constexpr auto max_knife_dist_sq = 150.0f * 150.0f;
		auto candidates = this->gather_candidates( local, max_knife_dist_sq );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		if ( eye_candidates.count == 0 )
		{
			eye_candidates.entries[ 0 ].position = g_shared.get_shoot_position( );
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_knife( eye_candidates.entries[ i ].position, ctx, info, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};
		target best_backstab{};

		for ( const auto& h : all_hits )
		{
			auto& dest = h.is_backstab ? best_backstab : best;

			if ( !dest.valid || h.score > dest.score )
			{
				dest.hit = h;
				dest.hitchance = 1.0f;
				dest.score = h.score;
				dest.valid = true;
			}
		}

		auto& chosen = best_backstab.valid ? best_backstab : best;
		if ( !chosen.valid )
		{
			return;
		}

		this->m_knife_attack = static_cast< std::uint8_t >( chosen.hit.attack_type );
		this->fire_melee( cmd, chosen, local );
	}

	void rage::auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_ragebot.enabled )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		if ( !g_shared.can_shoot( cmd, local.controller ) )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		if ( !settings::g_combat.m_autos.revolver.value )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		constexpr auto cock_ticks{ 13 };
		if ( this->m_revolver_cock_ticks >= cock_ticks )
		{
			// End the held cycle. Target selection adds attack back on this
			// command only when the revolver should actually fire.
			cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
			cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
			this->m_revolver_cock_ticks = 0;

			this->run_gun( cmd, ctx, local );
			return;
		}

		// Keep target and hitchance planning active throughout the cock cycle.
		// Autostop consumes this command's decision on the following command.
		this->run_gun( cmd, ctx, local, false );

		cmd->buttons.value |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		if ( history_index >= 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
		}

		++this->m_revolver_cock_ticks;
	}

	std::vector<rage::scan_hit> rage::scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		std::vector<std::vector<scan_hit>> per_candidate( candidates.size( ) );

		threadpool::parallel_for( 0, static_cast< int >( candidates.size( ) ), [ & ]( int begin, int end )
			{
				for ( auto ci = begin; ci < end; ++ci )
				{
					auto& cand = candidates[ ci ];
					auto& candidate_hits = per_candidate[ ci ];
					candidate_hits.reserve( 24 );

					for ( auto ri = 0; ri < cand.record_count; ++ri )
					{
						if ( !cand.records[ ri ] || !cand.records[ ri ]->valid )
						{
							continue;
						}

						auto hits = this->scan_player( eye, inaccuracy, ctx, cand, cand.records[ ri ], local );

						for ( auto& h : hits )
						{
							candidate_hits.push_back( std::move( h ) );
						}
					}
				}
			}, 1 );

		std::vector<scan_hit> flat;
		auto total_hits{ std::size_t{} };
		for ( const auto& hits : per_candidate )
		{
			total_hits += hits.size( );
		}
		flat.reserve( total_hits );

		for ( auto& v : per_candidate )
		{
			for ( auto& h : v )
			{
				flat.push_back( std::move( h ) );
			}
		}

		return flat;
	}

	std::vector<rage::scan_hit> rage::scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local ) const
	{
		// idk how this happens
		if (!cand.pawn || cand.record_count <= 0 || cand.health <= 0)
			return {};

		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record );

		std::array<int, 19> scan_order{};
		auto scan_count{ 0 };

		// Check if hitbox override is enabled
		const auto& hitbox_override = settings::g_combat.m_hitbox_override;
		const bool use_override = hitbox_override.enabled.value;
		
		if ( use_override )
		{
			// Force only selected hitboxes when override is enabled
			if ( hitbox_override.hitboxes.values[ 0 ] ) // head
			{
				scan_order[ scan_count++ ] = 0;
			}
			
			if ( hitbox_override.hitboxes.values[ 1 ] ) // neck
			{
				scan_order[ scan_count++ ] = 1;
			}
			
			if ( hitbox_override.hitboxes.values[ 2 ] ) // torso (chest + stomach)
			{
				scan_order[ scan_count++ ] = 4;
				scan_order[ scan_count++ ] = 5;
				scan_order[ scan_count++ ] = 6;
				scan_order[ scan_count++ ] = 3;
				scan_order[ scan_count++ ] = 2;
			}
			
			if ( hitbox_override.hitboxes.values[ 3 ] ) // arms
			{
				for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}
			
			if ( hitbox_override.hitboxes.values[ 4 ] ) // legs
			{
				for ( auto idx : { 7, 8, 9, 10 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}
			
			if ( hitbox_override.hitboxes.values[ 5 ] ) // paws
			{
				for ( auto idx : { 11, 12 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}
		}
		else
		{
			// Use normal config hitboxes when override is disabled
			if ( config.hitboxes.values[ 0 ] )
			{
				scan_order[ scan_count++ ] = 0;
			}

			if ( config.hitboxes.values[ 1 ] )
			{
				scan_order[ scan_count++ ] = 4;
				scan_order[ scan_count++ ] = 5;
				scan_order[ scan_count++ ] = 6;
			}

			if ( config.hitboxes.values[ 2 ] )
			{
				scan_order[ scan_count++ ] = 3;
				scan_order[ scan_count++ ] = 2;
			}

			if ( config.hitboxes.values[ 3 ] )
			{
				for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}

			if ( config.hitboxes.values[ 4 ] )
			{
				for ( auto idx : { 7, 8, 9, 10 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}

			if ( config.hitboxes.values[ 5 ] )
			{
				for ( auto idx : { 11, 12 } )
				{
					scan_order[ scan_count++ ] = idx;
				}
			}
		}

		// Old configs can deserialize with every hitbox disabled. Keep the
		// ragebot operational with the core head and torso hitboxes.
		if ( scan_count == 0 )
		{
			// Always include head hitbox when no hitboxes are selected
			scan_order[ scan_count++ ] = 0;

			for ( auto idx : { 4, 5, 6, 3, 2 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		struct trace_point
		{
			math::vector3 position;
			int hitbox_index;
			int bone_index;
			systems::hitboxes::entry hitbox;
			bool is_center;
			float resolver_penalty;
			float resolver_rotation;
		};

		struct scan_entry
		{
			int hitbox_index;
			const systems::hitboxes::entry* hb;
		};

		std::vector<scan_entry> entries;
		entries.reserve( static_cast< std::size_t >( scan_count ) );

		for ( auto idx = 0; idx < scan_count; ++idx )
		{
			const auto hitbox_index = scan_order[ idx ];
			const systems::hitboxes::entry* hb{ nullptr };

			for ( const auto& entry : hitbox_set )
			{
				if ( entry.index == hitbox_index )
				{
					hb = &entry;
					break;
				}
			}

			if ( !hb || hb->bone < 0 || hb->bone >= 28 )
			{
				continue;
			}

			entries.push_back( scan_entry{ hitbox_index, hb } );
		}

		std::vector<trace_point> points;
		points.reserve( static_cast< std::size_t >( scan_count ) * 12 * 2 );

		// Build the trace points for one yaw rotation of the pose. Rotation 0 is
		// the recorded pose itself; the resolver supplies the other desync
		// extremes so a jittering target is scanned on every candidate body yaw
		// at once. The score penalty keeps a rotated point from winning unless
		// the recorded pose genuinely produces nothing usable.
		//
		// Duplicate detection uses an O(1) hash-set keyed on (hitbox, rotation_slot,
		// quantised_x, quantised_y, quantised_z). The old std::any_of linear scan
		// over the growing points vector was O(N²) per call, which showed up as
		// 100+ FPS drops because scan_player runs inside a parallel_for that is
		// called multiple times per frame.
		struct point_key
		{
			int hitbox_index;
			int rotation_slot; // 0 = recorded pose, 1 = resolver rotations
			int qx, qy, qz;   // position quantised to 0.1 u grid

			bool operator==( const point_key& o ) const noexcept
			{
				return hitbox_index == o.hitbox_index && rotation_slot == o.rotation_slot &&
					   qx == o.qx && qy == o.qy && qz == o.qz;
			}
		};
		struct point_key_hash
		{
			std::size_t operator()( const point_key& k ) const noexcept
			{
				std::size_t h = static_cast< std::size_t >( k.hitbox_index );
				h ^= static_cast< std::size_t >( k.rotation_slot ) * 0x9e3779b9u + 0x6c62272eu;
				h ^= static_cast< std::size_t >( k.qx ) * 0x517cc1b7u;
				h ^= static_cast< std::size_t >( k.qy ) * 0x27d4eb2fu;
				h ^= static_cast< std::size_t >( k.qz ) * 0xb5026f5au;
				return h;
			}
		};
		std::unordered_set< point_key, point_key_hash > seen_points;
		seen_points.reserve( static_cast< std::size_t >( scan_count ) * 12 * 2 );

		const auto rotation_origin = record->origin;
		const auto emit_points = [ & ]( float rotation, float penalty )
		{
			const int rotation_slot = ( rotation == 0.0f ) ? 0 : 1;

			for ( const auto& entry : entries )
			{
				const auto& hb = *entry.hb;
				const auto& bone = skeleton[ hb.bone ];
				if ( bone.position.length_sqr( ) < 1.0f )
				{
					continue;
				}

				const auto hitbox_center = ( hb.mins + hb.maxs ) * 0.5f;
				const auto center = rotate_point_yaw( bone.rotation.rotate_vector( hitbox_center ) + bone.position, rotation_origin, rotation );

				const auto make_key = [ & ]( const math::vector3& pos ) -> point_key
				{
					return {
						entry.hitbox_index,
						rotation_slot,
						static_cast< int >( pos.x * 10.0f ),
						static_cast< int >( pos.y * 10.0f ),
						static_cast< int >( pos.z * 10.0f )
					};
				};

				// Always emit the hitbox centre — no duplicate check needed since
				// each (hitbox, rotation) pair produces exactly one centre.
				trace_point cp{};
				cp.position = center;
				cp.hitbox_index = entry.hitbox_index;
				cp.bone_index = hb.bone;
				cp.hitbox = hb;
				cp.is_center = true;
				cp.resolver_penalty = penalty;
				cp.resolver_rotation = rotation;
				points.push_back( cp );
				seen_points.insert( make_key( center ) );

				if ( config.pointscale <= 0.0f )
				{
					continue;
				}

				const auto mps = this->generate_multipoints( hb, center, bone.rotation, config.pointscale, eye, inaccuracy );

				for ( const auto& mp : mps )
				{
					const auto rotated = rotation == 0.0f ? mp : rotate_point_yaw( mp, rotation_origin, rotation );

					if ( !seen_points.insert( make_key( rotated ) ).second )
					{
						continue; // O(1) duplicate rejection
					}

					trace_point tp{};
					tp.position = rotated;
					tp.hitbox_index = entry.hitbox_index;
					tp.bone_index = hb.bone;
					tp.hitbox = hb;
					tp.is_center = false;
					tp.resolver_penalty = penalty;
					tp.resolver_rotation = rotation;
					points.push_back( tp );
				}
			}
		};

		emit_points( 0.0f, 0.0f );

		for ( auto i = 0; i < cand.resolver_offset_count; ++i )
		{
			emit_points( cand.resolver_offsets[ i ], k_resolver_penalty );
		}

		if ( points.empty( ) )
		{
			return {};
		}

		std::vector<scan_hit> results;
		results.reserve( static_cast< std::size_t >( scan_count ) * 2 );

		// Whether a hitbox's centre was already enough to stop testing its
		// multipoints. Split per rotation: a resolver rotation is a genuinely
		// different pose, so one pose's centre must never suppress another's ring.
		std::array<std::array<bool, 19>, 2> center_sufficient{};
		const auto sufficient_slot = [ & ]( const trace_point& tp ) -> bool*
		{
			if ( tp.hitbox_index < 0 || tp.hitbox_index >= static_cast< int >( center_sufficient[ 0 ].size( ) ) )
			{
				return nullptr;
			}

			return &center_sufficient[ tp.resolver_rotation == 0.0f ? 0 : 1 ][ tp.hitbox_index ];
		};

		for ( const auto& tp : points )
		{
			if ( const auto slot = sufficient_slot( tp ); slot && !tp.is_center && *slot )
			{
				continue;
			}

			const auto aim = math::helpers::calculate_angle( eye, tp.position );
			const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

			if ( fov > config.max_fov )
			{
				continue;
			}

			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( eye, tp.position, pen_ctx, local.pawn, local.team, pen ) )
			{
				continue;
			}

			const auto is_lethal = pen.damage >= static_cast< float >( cand.health );
			if ( !is_lethal && pen.damage < cand.min_damage )
			{
				continue;
			}

			const auto actual_hitbox_index = pen.hitbox >= 0 ? pen.hitbox : tp.hitbox_index;
			const systems::hitboxes::entry* actual_hitbox{ nullptr };

			for ( const auto& entry : hitbox_set )
			{
				if ( entry.index == actual_hitbox_index )
				{
					actual_hitbox = &entry;
					break;
				}
			}

			if ( !actual_hitbox || actual_hitbox->bone < 0 || actual_hitbox->bone >= 28 )
			{
				continue;
			}

			if ( !tp.is_center && tp.hitbox_index == 0 && actual_hitbox_index != 0 )
			{
				continue;
			}

			if ( tp.is_center && actual_hitbox_index == tp.hitbox_index )
			{
				if ( const auto slot = sufficient_slot( tp ); slot )
				{
					*slot = !pen.penetrated || pen.damage >= static_cast< float >( cand.health );
				}
			}

			scan_hit h{};
			h.position = tp.position;
			h.aim_angle = aim;
			h.damage = pen.damage;
			h.fov = fov;
			h.hitbox_index = actual_hitbox_index;
			h.hitgroup = pen.hitgroup;
			h.bone_index = actual_hitbox->bone;
			h.hitbox = *actual_hitbox;
			h.is_center = tp.is_center && actual_hitbox_index == tp.hitbox_index;
			h.penetrated = pen.penetrated;
			h.pawn = cand.pawn;
			h.health = cand.health;
			h.record = record;
			h.resolver_penalty = tp.resolver_penalty;
			h.resolver_rotation = tp.resolver_rotation;

			results.push_back( h );
		}

		return results;
	}

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const
	{
		auto hitgroup_priority = [ ]( int hitbox_index ) -> int
			{
				if ( hitbox_index == 0 ) { return 4; }
				if ( hitbox_index >= 1 && hitbox_index <= 6 ) { return 3; }
				if ( hitbox_index >= 13 && hitbox_index <= 18 ) { return 2; }
				if ( hitbox_index >= 7 && hitbox_index <= 12 ) { return 1; }
				return 0;
			};

		struct record_group
		{
			shared::lagcomp::record* record;
			std::vector<int> hit_indices;
		};

		std::vector<record_group> groups;
		groups.reserve( 16 );

		for ( auto i = 0; i < static_cast< int >( hits.size( ) ); ++i )
		{
			auto rec = hits[ i ].record;
			auto found{ false };

			for ( auto& g : groups )
			{
				if ( g.record == rec )
				{
					g.hit_indices.push_back( i );
					found = true;
					break;
				}
			}

			if ( !found )
			{
				record_group g{};
				g.record = rec;
				g.hit_indices.reserve( 16 );
				g.hit_indices.push_back( i );
				groups.push_back( std::move( g ) );
			}
		}

		// Twelve, not eight: a resolver rotation roughly triples the candidate
		// count per record, so a smaller cap would push the rotated poses out of
		// the full hitchance evaluation before they get a chance to win.
		constexpr auto top_k_per_record{ 12 };

		auto cheap_score = [ & ]( const scan_hit& h ) -> float
			{
				const auto lethal_bonus = h.damage >= static_cast< float >( h.health ) ? 100000.0f : 0.0f;
				const auto direct_bonus = h.penetrated ? 0.0f : 5000.0f;
				const auto center_bonus = h.is_center ? 2000.0f : 0.0f;

				return lethal_bonus + direct_bonus + center_bonus + h.damage * 20.0f +
					static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * 10.0f - h.fov - h.resolver_penalty;
			};

		for ( auto& group : groups )
		{
			if ( static_cast< int >( group.hit_indices.size( ) ) <= top_k_per_record )
			{
				continue;
			}

			std::partial_sort
			(
				group.hit_indices.begin( ),
				group.hit_indices.begin( ) + top_k_per_record,
				group.hit_indices.end( ),
				[ & ]( int a, int b ) { return cheap_score( hits[ a ] ) > cheap_score( hits[ b ] ); }
			);

			group.hit_indices.resize( top_k_per_record );
		}

		struct evaluated_hit
		{
			int hit_index;
			float hitchance;
			float score;
			bool passes_hitchance;
		};

		std::vector<evaluated_hit> evaluated;
		evaluated.reserve( hits.size( ) );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto needed_hc = config.hitchance_override.value
			? static_cast< float >( config.hitchance_override_value ) / 100.0f
			: static_cast< float >( config.hitchance ) / 100.0f;

		for ( auto& group : groups )
		{
			if ( !group.record || !group.record->valid )
			{
				continue;
			}

			for ( const auto idx : group.hit_indices )
			{
				const auto& h = hits[ idx ];

				if ( !h.record || !h.record->valid )
				{
					continue;
				}

				if ( h.bone_index < 0 || h.bone_index >= 28 )
				{
					continue;
				}

				const auto& bone = group.record->bones[ h.bone_index ];
				const auto hc = config.no_spread.value
					? 1.0f
					: g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, config.spread_samples.value );
				const auto hp = static_cast< float >( h.health );
				const auto can_kill = h.damage >= hp;
				const auto passes_hitchance = config.no_spread.value || hc >= needed_hc;
				auto score = passes_hitchance ? 1000000.0f : 0.0f;

				if ( can_kill )
				{
					score += 100000.0f + hc * 10000.0f;
				}
				else
				{
					score += h.damage * hc * 100.0f + h.damage * 5.0f;
				}

				score += h.penetrated ? 0.0f : 250.0f;
				score += h.is_center ? 50.0f : 0.0f;
				score += static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * 2.0f;
				score -= h.fov * 0.1f;
			score -= h.resolver_penalty;

				evaluated.push_back( evaluated_hit{ idx, hc, score, passes_hitchance } );
			}
		}

		target best{};

		for ( const auto& e : evaluated )
		{
			if ( e.hit_index < 0 || e.hit_index >= static_cast< int >( hits.size( ) ) )
			{
				continue;
			}

			const auto& h = hits[ e.hit_index ];

			if ( !h.record || !h.record->valid )
			{
				continue;
			}

			const auto best_passes_hitchance = best.valid && ( config.no_spread.value || best.hitchance >= needed_hc );
			auto is_better = !best.valid || ( e.passes_hitchance != best_passes_hitchance ? e.passes_hitchance : e.score > best.score );
			if ( best.valid && e.passes_hitchance == best_passes_hitchance && std::fabsf( e.score - best.score ) < 0.01f )
			{
				if ( h.record->tick != best.hit.record->tick )
				{
					is_better = h.record->tick > best.hit.record->tick;
				}
				else if ( h.is_center != best.hit.is_center )
				{
					is_better = h.is_center;
				}
				else
				{
					is_better = h.fov < best.hit.fov;
				}
			}

			if ( is_better )
			{
				best.hit = h;
				best.hitchance = e.hitchance;
				best.score = e.score;
				best.valid = true;
			}
		}

		return best;
	}

	float rage::evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy, const settings::combat::ragebot::weapon_group& config ) const
	{
		if ( !hit.record || !hit.record->valid || hit.bone_index < 0 || hit.bone_index >= 28 )
		{
			return 0.0f;
		}

		return g_shared.calculate_hitchance( hit.source_eye.position, hit.aim_angle, hit.hitbox, hit.record->bones[ hit.bone_index ], inaccuracy, ctx.spread, config.spread_samples.value );
	}

	float rage::get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const
	{
		auto velocity = ctx.velocity;
		velocity.z = 0.0f;

		const auto speed = velocity.length_2d( );
		if ( speed > ctx.accurate_threshold )
		{
			return g_shared.get_inaccuracy_at_velocity( local.pawn, velocity );
		}

		const auto& shared_ctx = g_shared.ctx( );
		if ( !shared_ctx.weapon_vdata )
		{
			return ctx.predicted_inaccuracy;
		}

		const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
		return std::max( inaccuracy_stand, g_shared.get_inaccuracy_at_velocity( local.pawn, velocity ) );
	}

	std::vector<rage::scan_hit> rage::scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
				if ( hitbox_set.count <= 0 )
				{
					continue;
				}

				record->apply( );
				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone < 0 || hb.bone >= 28 )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					if ( bone.position.length_sqr( ) < 1.0f )
					{
						continue;
					}

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_zeusbot.max_fov )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					const auto trace = this->trace_taser_hit( eye, forward, shared_ctx.range * 0.85f, cand.pawn, local.pawn );
					if ( trace.hit_entity != cand.pawn )
					{
						continue;
					}

					const auto dist = ( center - eye ).length( );
					const auto range_fraction = dist / shared_ctx.range;

					scan_hit h{};
					h.position = center;
					h.aim_angle = aim;
					h.damage = 500.0f;
					h.score = ( 10000.0f - dist ) * ( range_fraction > 0.92f ? 0.8f : 1.0f );
					h.fov = fov;
					h.hitbox_index = hb.index;
					h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
					h.bone_index = hb.bone;
					h.hitbox = hb;
					h.is_center = true;
					h.pawn = cand.pawn;
					h.health = cand.health;
					h.record = record;

					results.push_back( h );
				}

				record->restore( );
			}
		}

		return results;
	}

	rage::knife_info rage::get_knife_info( const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto next_secondary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
		const auto last_shot_time = memory::read<float>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		const auto cur_time = static_cast< float >( tick_base ) * cstypes::tick_interval;

		return knife_info
		{
			.can_slash = tick_base >= next_primary,
			.can_stab = tick_base >= next_secondary,
			.charged = ( cur_time - last_shot_time ) > 0.4f,
			.armor_ratio = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) )
		};
	}

	std::vector<rage::scan_hit> rage::scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		// Knifebot has been removed - function disabled
		return {};
	}

	void rage::fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local, bool subtick_attack )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		// Feed the resolver so a following player_hurt is attributed to the pose
		// this shot was planned on, and a silent miss widens the next sweep.
		on_resolver_shot_fired( tgt.hit.pawn );

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		auto aim_angle = config.no_spread.value ? math::helpers::calculate_angle( shoot_eye, tgt.hit.position ) : tgt.hit.aim_angle;

		if ( config.no_spread.value )
		{
			auto stamp_tick = tick_base;
			auto stamp_frac{ 0.0f };

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				auto tick_add = [ ]( int t, float f, int dt, float df )
					{
						f += df;
						auto carry = static_cast< int >( std::floor( f ) );
						f -= static_cast< float >( carry );
						return std::pair{ t + dt + carry, f };
					};

				std::tie( stamp_tick, stamp_frac ) = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );
			}

			const auto corrected = g_shared.find_spread_correction( aim_angle, stamp_tick );
			if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
			{
				this->m_firing_this_tick = false;
				return;
			}

			aim_angle = corrected;
		}

		g_shared.last_shoot_tick( ) = tick_base;

		// Auto pitch removed
		
		if ( settings::g_misc.m_impacts.console_log.value )
		{
			const auto hitgroup_name = systems::g_hitboxes.hitgroup_to_name( tgt.hit.hitgroup );
			const auto bt_delta = g_shared.ctx( ).current_tick - tgt.hit.record->tick;
			logging::console::print(
				xs( "[rage] shot target hp {} for {:.0f} in {} (hc {:.0f}%, bt {}t{})" ),
				tgt.hit.health,
				tgt.hit.damage,
				hitgroup_name,
				tgt.hitchance * 100.0f,
				bt_delta,
				was_forced ? xs( ", forced" ) : ""
			);
		}

		features::misc::g_impacts.on_boom( tgt.hit.pawn, tgt.hit.hitgroup, tgt.hit.damage, tgt.hitchance, shared_ctx.inaccuracy, shared_ctx.spread, aim_angle, shoot_eye, tgt.hit.record->tick, g_shared.lc( ).get_skeleton( *tgt.hit.record ), was_forced );
		features::esp::player::g_chams.os ().push (tgt.hit.pawn);
		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_size = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
			{
				continue;
			}

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( aim_angle.x - aim_punch.x );
				angles->set_y( aim_angle.y - aim_punch.y );

				if ( config.no_spread.value )
				{
					angles->set_z( aim_angle.z );
				}
			}

			entry->set_render_tick_count( record_time.tick + 1 );
			entry->set_render_tick_fraction( 0.0f );

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				auto tick_add = [ ]( int t, float f, int dt, float df )
					{
						f += df;
						auto carry = static_cast< int >( std::floor( f ) );
						f -= static_cast< float >( carry );
						return std::pair{ t + dt + carry, f };
					};

				const auto [stamp_tick, stamp_frac] = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );

				entry->set_player_tick_count( stamp_tick );
				entry->set_player_tick_fraction( stamp_frac );
			}

			if ( entry->has_sv_interp0( ) )
			{
				const auto interp = entry->mutable_sv_interp0( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_sv_interp1( ) )
			{
				const auto interp = entry->mutable_sv_interp1( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_cl_interp( ) )
			{
				const auto interp = entry->mutable_cl_interp( );
				interp->set_frac( 0.0f );
			}
		}

		if ( !subtick_attack )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

			if ( history_size > 0 )
			{
				cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
			}
		}

		math::vector3 forward{};
		{
			if ( const auto angles = base->viewangles( ) )
			{
				math::helpers::angle_vectors_left( { angles->x( ), angles->y( ), angles->z( ) }, &forward );
			}
		}

		const auto punched_aim = math::vector3{ aim_angle.x - aim_punch.x, aim_angle.y - aim_punch.y, 0.0f };
		const auto facing_away = forward.dot( ( tgt.hit.record->origin - systems::g_prediction.pre( ).networked_origin ).normalized( ) ) < 0.707107f;

		auto command_aim = punched_aim;
		if ( !subtick_attack && facing_away && settings::g_combat.m_antiaim.hide_shots.value )
		{
			command_aim.x = 179.9f;
			command_aim.y = std::remainderf( punched_aim.y + 180.0f, 360.0f );
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( command_aim.x );
			angles->set_y( command_aim.y );
		}

		if ( !config.silent.value )
		{
			systems::g_input.set_view_angles( punched_aim );
		}
	}

	void rage::fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		g_shared.last_shoot_tick( ) = tick_base;

		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		const auto entry = history_index >= 0 ? cmd->csgo_user_cmd.mutable_input_history( history_index ) : nullptr;

		if ( entry )
		{
			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( tgt.hit.aim_angle.x );
				angles->set_y( tgt.hit.aim_angle.y );
			}

			entry->set_render_tick_count( record_time.tick + 1 );
			entry->set_render_tick_fraction( 0.0f );

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				auto tick_add = [ ]( int t, float f, int dt, float df )
					{
						f += df;
						auto carry = static_cast< int >( std::floor( f ) );
						f -= static_cast< float >( carry );
						return std::pair{ t + dt + carry, f };
					};

				const auto [stamp_tick, stamp_frac] = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );

				entry->set_player_tick_count( stamp_tick );
				entry->set_player_tick_fraction( stamp_frac );
			}

			if ( entry->has_sv_interp0( ) )
			{
				const auto interp = entry->mutable_sv_interp0( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_sv_interp1( ) )
			{
				const auto interp = entry->mutable_sv_interp1( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_cl_interp( ) )
			{
				const auto interp = entry->mutable_cl_interp( );
				interp->set_frac( 0.0f );
			}

		}

		const auto is_secondary = tgt.hit.attack_type == 1;
		const auto attack_button = is_secondary
			? cstypes::command_buttons::in_second_attack
			: cstypes::command_buttons::in_attack;

		cmd->buttons.value |= attack_button;
		cmd->buttons.value_changed |= attack_button;
		cmd->buttons.value_scroll |= attack_button;

		if ( history_index >= 0 )
		{
			if ( is_secondary )
			{
				cmd->csgo_user_cmd.set_attack2_start_history_index( history_index );
			}
			else
			{
				cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
			}
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( tgt.hit.aim_angle.x );
			angles->set_y( tgt.hit.aim_angle.y );
		}
	}

	std::vector<math::vector3> rage::generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy ) const
	{
		std::vector<math::vector3> out;

		auto scale = std::clamp( pointscale / 100.0f, 0.0f, 1.0f );
		if ( scale <= 0.01f )
		{
			return out;
		}

		const auto hb_mid   = ( hitbox.mins + hitbox.maxs ) * 0.5f;
		const auto capsule_a = center + bone_rot.rotate_vector( hitbox.mins - hb_mid );
		const auto capsule_b = center + bone_rot.rotate_vector( hitbox.maxs - hb_mid );

		// Dynamic pointscale feature has been removed - keeping manual scale only

		// Build a view-relative frame so points rotate correctly above and below us.
		const auto shoot_dir = ( center - shoot_pos ).normalized( );
		const auto ang       = math::helpers::vector_to_angle( shoot_dir );

		math::vector3 left{}, up{};
		math::helpers::angle_vectors_left( ang, nullptr, &left, &up );

		// angle_vectors_left returns left, so negate it for right.
		const auto right = math::vector3{ -left.x, -left.y, -left.z };

		// Trace from outside through the center to find the real capsule surface.
		// This is accurate around rounded end caps, where radius offsets are not.
		const auto surface_point = [ & ]( const math::vector3& direction ) -> math::vector3
		{
			const auto dir = direction.normalized( );

			if ( hitbox.radius > 0.001f )
			{
				const auto reach = ( capsule_b - capsule_a ).length( ) + hitbox.radius * 2.0f + 1.0f;
				const auto origin = center + dir * reach;
				const auto delta = dir * ( reach * -2.0f );
				auto fraction{ 1.0f };

				if ( g_shared.ray_vs_capsule( origin, delta, capsule_a, capsule_b, hitbox.radius, fraction ) )
				{
					return origin + delta * fraction;
				}
			}
			else
			{
				// Intersect box hitboxes in bone space using their directional support.
				auto inverse = bone_rot;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_dir = inverse.rotate_vector( dir );
				const auto extents = ( hitbox.maxs - hitbox.mins ) * 0.5f;
				auto distance = 8192.0f;

				if ( std::fabs( local_dir.x ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.x / local_dir.x ) );
				if ( std::fabs( local_dir.y ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.y / local_dir.y ) );
				if ( std::fabs( local_dir.z ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.z / local_dir.z ) );

				if ( distance < 8192.0f )
				{
					return center + dir * distance;
				}
			}

			return center;
		};

		const auto scaled_surface = [ & ]( const math::vector3& direction )
		{
			const auto surface = surface_point( direction );
			return center + ( surface - center ) * scale;
		};

		switch ( hitbox.index )
		{
		case 0: // head
		{
			out.reserve( 4 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			out.push_back( scaled_surface( up ) );
			out.push_back( scaled_surface( -up ) );
			break;
		}

		case 2: case 3: // stomach / pelvis
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}

		case 4: case 5: case 6: // chest
		{
			out.reserve( 3 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			if ( hitbox.index == 6 )
			{
				out.push_back( scaled_surface( up ) );
			}
			break;
		}

		case 7: case 8: case 9: case 10: case 11: case 12: // legs / feet
		{
			out.reserve( 2 );
			out.push_back( capsule_a );
			out.push_back( capsule_b );
			break;
		}

		case 13: case 14: case 15: case 16: case 17: case 18: // arms
		{
			out.reserve( 1 );
			out.push_back( capsule_b );
			break;
		}

		default:
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}
		}

		return out;
	}

	bool rage::should_stop_movement( const aim_context& ctx ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto velocity = ctx.velocity;

		if ( shared_ctx.weapon_type == cstypes::weapon_type::sniper && !ctx.is_scoped )
		{
			return false;
		}

		if ( ctx.on_ground )
		{
			const auto speed_2d = velocity.length_2d( );
			if ( speed_2d <= 0.1f )
			{
				return false;
			}

			const auto inaccuracy_move = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_hash ) );
			const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );

			return speed_2d * inaccuracy_move > inaccuracy_stand;
		}

		if ( shared_ctx.weapon_type != cstypes::weapon_type::sniper )
		{
			return false;
		}

		if ( velocity.z > 140.0f )
		{
			return false;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );

		const auto inac_jump_initial = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
		const auto inac_jump_apex = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto shootable_threshold = inac_jump_apex + 0.001f;
		const auto early_threshold = inac_jump_initial * 0.55f + inac_jump_apex * 0.45f;
		const auto air_inaccuracy = g_shared.get_air_inaccuracy( velocity.z, inac_jump_initial, inac_jump_apex );

		if ( air_inaccuracy <= shootable_threshold || air_inaccuracy <= early_threshold )
		{
			return true;
		}

		auto sim_vz = velocity.z;
		auto ticks_to_shootable{ 0 };

		for ( auto i = 1; i <= 32; ++i )
		{
			sim_vz -= sv_gravity * cstypes::tick_interval;

			if ( g_shared.get_air_inaccuracy( sim_vz, inac_jump_initial, inac_jump_apex ) <= shootable_threshold )
			{
				ticks_to_shootable = i;
				break;
			}
		}

		if ( ticks_to_shootable == 0 )
		{
			return false;
		}

		const auto speed_2d = velocity.length_2d( );
		const auto max_speed = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		const auto accurate_threshold = max_speed * 0.34f;

		if ( speed_2d <= accurate_threshold )
		{
			return true;
		}

		auto sim_speed = speed_2d;
		auto ticks_to_stop{ 32 };

		for ( auto i = 1; i <= 32; ++i )
		{
			const auto drop = std::fmaxf( sim_speed, sv_stopspeed ) * sv_friction * cstypes::tick_interval;
			sim_speed -= drop;

			if ( sim_speed <= accurate_threshold )
			{
				ticks_to_stop = i;
				break;
			}
		}

		return ticks_to_shootable <= ticks_to_stop + 2;
	}

	float rage::get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const
	{
		if ( override_active )
		{
			return static_cast< float >( config.min_damage_override_value );
		}

		const auto base = static_cast< float >( config.min_damage );

		const auto hp = static_cast< float >( target_health );
		if ( hp < base )
		{
			return hp + 1.0f;
		}

		return base;
	}

	float rage::get_knife_damage( float raw, int armor, float armor_ratio ) const
	{
		if ( armor <= 0 )
		{
			return raw;
		}

		const auto ratio = armor_ratio * 0.5f;
		auto damage_to_health = raw * ratio;
		const auto damage_to_armor = ( raw - damage_to_health ) * 0.5f;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = raw - static_cast< float >( armor ) * 2.0f;
		}

		return std::max( 0.0f, std::floorf( damage_to_health ) );
	}

	systems::tracing::result rage::trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * range;
		const int filter_extras[ ]{ 0, 15 };

		for ( const auto extra : filter_extras )
		{
			const auto filter = extra == 0 ? systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4 ) : systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4, 15 );
			auto result = systems::g_tracing.trace( origin, end, filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}

			for ( auto radius = 2.0f; radius <= 4.0f; radius += 2.0f )
			{
				const auto sweep_end = end - forward * radius;
				result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, filter );

				if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
				{
					return result;
				}
			}
		}

		systems::tracing::result miss{};
		miss.fraction = 1.0f;
		miss.hit_entity = 0;
		return miss;
	}

	systems::tracing::result rage::trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * reach;
		const auto knife_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4 );
		auto result = systems::g_tracing.trace( origin, end, knife_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		const auto weapon_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4, 15 );
		result = systems::g_tracing.trace( origin, end, weapon_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		for ( auto radius = 14.0f; radius > 0.0f; radius -= 3.0f )
		{
			const auto sweep_end = end - forward * radius;
			result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, weapon_filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}
		}

		result.fraction = 1.0f;
		result.hit_entity = 0;
		return result;
	}

	void rage::update_penetration_crosshair( const systems::local::snapshot& local )
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		const auto& ctx = g_shared.ctx( );

		if ( !cfg.enabled.value || !ctx.valid || !local.is_alive || local.team < 2
			|| !local.pawn || !ctx.weapon
			|| ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->m_penetration_crosshair_state.store( penetration_crosshair_state::unavailable, std::memory_order_relaxed );
			return;
		}

		// get_shoot_position() can be zero before prediction has populated the
		// weapon-services shoot history. The crosshair needs the current eye now.
		const auto eye_pos = g_shared.get_eye_position( local.pawn );
		auto view_angles = systems::g_input.get_view_angles( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		view_angles.x += aim_punch.x;
		view_angles.y += aim_punch.y;

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		auto pen_damage{ 0.0f };
		const auto can_pen = g_shared.pen( ).can( eye_pos, forward, pen_damage, local );
		this->m_penetration_crosshair_state.store(
			can_pen ? penetration_crosshair_state::penetrable : penetration_crosshair_state::blocked,
			std::memory_order_relaxed );
	}

	void rage::draw_penetration_crosshair( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto state = this->m_penetration_crosshair_state.load( std::memory_order_relaxed );
		const auto local = systems::g_local.get( );
		if ( state == penetration_crosshair_state::unavailable || !local.is_alive || systems::g_local.is_in_cinematic( ) )
		{
			return;
		}

		const auto can_pen = state == penetration_crosshair_state::penetrable;

		const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
		const auto& outline = can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;
		const auto [ screen_w, screen_h ] = xdraw::viewport_size( );
		const auto cx = std::floorf( static_cast< float >( screen_w ) * 0.5f );
		const auto cy = std::floorf( static_cast< float >( screen_h ) * 0.5f );
		constexpr auto half_size{ 3.0f };
		constexpr auto outline_size{ 1.0f };

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( outline.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ outline.value.r, outline.value.g, outline.value.b, glow_a };

			glow.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
				( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, glow_col );
		}

		draw_list.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
			( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, outline );
		draw_list.rect_filled( cx - half_size, cy - half_size, half_size * 2.0f, half_size * 2.0f, fill );
	}

	template<typename ConfigType>
	void rage::draw_visualize_aimbot( xdraw::draw_list& draw_list, const ConfigType& config, const systems::local::snapshot& local ) const
	{
		// Visualize aimbot hitboxes on players in field of view
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		if ( players.empty( ) )
		{
			return;
		}

		// Get selected hitboxes from multicombo - it's a bools type, convert to int.
		// The count has to come from the array itself: the ragebot group carries six
		// hitboxes, the legitbot group five, and this template is instantiated for
		// both, so a hardcoded six reads past the end of the smaller one.
		constexpr auto viz_count = sizeof( config.visualize_hitboxes.values ) / sizeof( config.visualize_hitboxes.values[ 0 ] );

		int selected_hitboxes = 0;
		for ( int i = 0; i < 6 && i < viz_count; ++i )
		{
			if ( config.visualize_hitboxes[ i ] )
			{
				selected_hitboxes |= ( 1 << i );
			}
		}

		if ( selected_hitboxes == 0 )
		{
			return; // No hitboxes selected
		}

		const auto eye_position = g_shared.get_shoot_position( );
		if ( eye_position.length_sqr( ) < 1.0f )
		{
			return;
		}

		// Color mapping for hitboxes
		constexpr xdraw::color hitbox_colors[] = {
			{ 255, 60,  60,  200 },  // head - red
			{ 255, 140, 60,  180 },  // torso - orange
			{ 255, 220, 60,  180 },  // stomach - yellow
			{ 60,  140, 255, 170 },  // arms - blue
			{ 80,  255, 120, 170 },  // legs - green
			{ 180, 80,  255, 170 },  // paws - purple
		};

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

			const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !game_scene_node )
			{
				continue;
			}

			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
			if ( hitbox_set.count <= 0 )
			{
				continue;
			}

			const auto model_state = memory::read<std::uintptr_t>( game_scene_node + 0x170 );
			if ( !model_state )
			{
				continue;
			}

			const auto bone_count = memory::read<std::uint32_t>( model_state + 0xA8 );
			const auto bone_data = memory::read<std::uintptr_t>( model_state + 0x80 );
			if ( !bone_data || bone_count <= 0 || bone_count > 256 )
			{
				continue;
			}

			// Draw hitboxes based on selection
			for ( const auto& entry : hitbox_set )
			{
				// Map hitbox to category (0=head, 1=torso, 2=stomach, 3=arms, 4=legs, 5=paws)
				int category = -1;
				if ( entry.index == 0 ) category = 0; // head
				else if ( entry.index >= 4 && entry.index <= 6 ) category = 1; // chest/torso
				else if ( entry.index >= 2 && entry.index <= 3 ) category = 2; // stomach
				else if ( entry.index >= 13 && entry.index <= 18 ) category = 3; // arms
				else if ( entry.index >= 7 && entry.index <= 12 ) category = 4; // legs
				else if ( entry.index >= 19 && entry.index <= 26 ) category = 5; // paws

				if ( category < 0 || category > 5 )
				{
					continue;
				}

				// Check if this hitbox category is selected
				if ( !( selected_hitboxes & ( 1 << category ) ) )
				{
					continue;
				}

				if ( entry.bone < 0 || entry.bone >= static_cast<int>( bone_count ) )
				{
					continue;
				}

				const auto bone_ptr = bone_data + static_cast<std::size_t>( entry.bone ) * 0x20;
				const auto bone_pos = memory::read<math::vector3>( bone_ptr );
				const auto bone_rot = memory::read<math::quaternion>( bone_ptr + 0x10 );

				if ( bone_pos.length_sqr( ) < 1.0f )
				{
					continue;
				}

				// Draw hitbox based on shape
				if ( entry.radius > 0.001f )
				{
					// Capsule hitbox
					const auto capsule_start = bone_rot.rotate_vector( entry.mins ) + bone_pos;
					const auto capsule_end = bone_rot.rotate_vector( entry.maxs ) + bone_pos;

					const auto screen_start = systems::g_view.project( capsule_start );
					const auto screen_end = systems::g_view.project( capsule_end );

					if ( systems::g_view.projection_valid( screen_start ) && systems::g_view.projection_valid( screen_end ) )
					{
						// Draw line with thickness representing radius
						const auto distance_to_player = ( capsule_start - eye_position ).length( );
						const auto screen_radius = ( entry.radius * 1000.0f ) / std::max( distance_to_player, 100.0f );
						
						draw_list.line( screen_start.x, screen_start.y, screen_end.x, screen_end.y, hitbox_colors[ category ], std::max( 2.0f, screen_radius ) );
						draw_list.circle_filled( screen_start.x, screen_start.y, std::max( 2.5f, screen_radius * 0.5f ), hitbox_colors[ category ] );
						draw_list.circle_filled( screen_end.x, screen_end.y, std::max( 2.5f, screen_radius * 0.5f ), hitbox_colors[ category ] );
					}
				}
				else
				{
					// Box hitbox - draw as wireframe
					const auto center = bone_rot.rotate_vector( ( entry.mins + entry.maxs ) * 0.5f ) + bone_pos;
					const auto screen_center = systems::g_view.project( center );

					if ( systems::g_view.projection_valid( screen_center ) )
					{
						const auto distance_to_player = ( center - eye_position ).length( );
						const auto box_size = ( entry.maxs - entry.mins ).length( );
						const auto screen_size = ( box_size * 500.0f ) / std::max( distance_to_player, 100.0f );
						
						draw_list.rect( screen_center.x - screen_size * 0.5f, screen_center.y - screen_size * 0.5f, screen_size, screen_size, hitbox_colors[ category ], 1.5f );
					}
				}
			}
		}
	}

	// Explicit template instantiations
	template void rage::draw_visualize_aimbot<settings::combat::ragebot::weapon_group>( xdraw::draw_list&, const settings::combat::ragebot::weapon_group&, const systems::local::snapshot& ) const;
	template void rage::draw_visualize_aimbot<settings::combat::legitbot::weapon_group>( xdraw::draw_list&, const settings::combat::legitbot::weapon_group&, const systems::local::snapshot& ) const;

} // namespace features::combat
