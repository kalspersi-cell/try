#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/systems/systems.hpp>

namespace features::combat {

	// Calculate the optimal counter-strafe direction based on current velocity
	math::vector3 rage::calculate_counter_strafe_direction( const math::vector3& velocity ) const
	{
		if ( velocity.length_2d( ) < 0.1f )
		{
			return math::vector3( 0.f, 0.f, 0.f );
		}

		// Return exact opposite of velocity vector for precise counter-strafing
		auto counter_dir = velocity * -1.f;
		counter_dir.z = 0.f; // Zero out vertical component
		counter_dir = counter_dir.normalize( );

		return counter_dir;
	}

	// Predict how long it will take to come to a complete stop
	float rage::predict_stop_time( const math::vector3& velocity, float max_speed, const systems::local::snapshot& local ) const
	{
		const auto speed = velocity.length_2d( );
		if ( speed < 0.1f )
		{
			return 0.f;
		}

		// CS2 physics constants
		constexpr float sv_friction = 5.2f;
		constexpr float sv_stopspeed = 80.f;
		constexpr float tick_interval = 0.015625f; // 64 tick
		constexpr float default_friction = 1.0f;

		// Calculate friction deceleration (assuming on ground)
		const auto friction = sv_friction * default_friction;
		const auto control = speed < sv_stopspeed ? sv_stopspeed : speed;
		const auto drop = control * friction * tick_interval;

		// Estimate ticks needed to stop
		auto remaining_speed = speed;
		float ticks = 0.f;

		while ( remaining_speed > 0.1f && ticks < 20.f ) // Max 20 ticks sanity check
		{
			remaining_speed -= drop;
			if ( remaining_speed < 0.f )
			{
				remaining_speed = 0.f;
			}
			ticks += 1.f;
		}

		return ticks * tick_interval; // Convert to seconds
	}

	// Check if we can achieve max accuracy after predicted stop time
	bool rage::can_shoot_after_stop( const aim_context& ctx, float predicted_stop_time, const systems::local::snapshot& local ) const
	{
		// If we're already stopped, we can shoot
		if ( predicted_stop_time < 0.001f )
		{
			return true;
		}

		// Check if the predicted stop will put us within accurate threshold
		// This is used for early-stop prediction
		const auto predicted_velocity_2d = ctx.velocity.length_2d( ) * ( 1.f - ( predicted_stop_time / 0.078f ) );

		// Get inaccuracy at predicted velocity
		const auto predicted_inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, math::vector3( predicted_velocity_2d, 0.f, 0.f ) );

		// Check if we'll be accurate enough
		return g_shared.is_max_accuracy( predicted_inaccuracy );
	}

	// Determine if auto-stop should activate
	bool rage::should_auto_stop( const aim_context& ctx, const settings::combat::ragebot::weapon_group& config ) const
	{
		if ( !config.auto_stop.value )
		{
			return false;
		}

		const auto speed_2d = ctx.velocity.length_2d( );

		// If already at max accuracy, no need to stop
		if ( g_shared.is_max_accuracy( ctx.predicted_inaccuracy ) )
		{
			return false;
		}

		// If moving above accurate threshold, we need to stop
		if ( speed_2d > ctx.accurate_threshold )
		{
			return true;
		}

		return false;
	}

	// Check if player is currently at maximum weapon accuracy
	bool rage::is_at_max_accuracy( const aim_context& ctx, const systems::local::snapshot& local ) const
	{
		const auto speed_2d = ctx.velocity.length_2d( );

		// Check velocity threshold
		if ( speed_2d > 1.f )
		{
			return false;
		}

		// Check inaccuracy
		const auto current_inaccuracy = g_shared.get_inaccuracy( false );
		return g_shared.is_max_accuracy( current_inaccuracy );
	}

	// Apply counter-strafing to command
	void rage::apply_counter_strafe( systems::input::usercmd* cmd, const math::vector3& velocity, float max_speed ) const
	{
		const auto speed_2d = velocity.length_2d( );

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// Already stopped - zero out movement
		if ( speed_2d < 1.0f )
		{
			base->set_forwardmove( 0.f );
			base->set_leftmove( 0.f );
			return;
		}

		// ===== METHOD 1: COUNTER-STRAFE IMPULSE (FASTEST) =====
		// Get view direction vectors
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto yaw_rad = view_angles.y * ( 3.14159265359f / 180.f );
		
		// Forward and right vectors relative to view
		const auto view_forward = math::vector3( std::cos( yaw_rad ), std::sin( yaw_rad ), 0.f );
		const auto view_right = math::vector3( std::sin( yaw_rad ), -std::cos( yaw_rad ), 0.f );

		// Project current velocity onto view axes
		const auto forward_velocity = velocity.dot( view_forward );
		const auto side_velocity = velocity.dot( view_right );

		// Apply EXACT OPPOSITE impulse to instantly halt
		// This is the industry-standard counter-strafe formula
		auto counter_forward = -forward_velocity;
		auto counter_side = -side_velocity;

		// Clamp to max weapon speed to respect game limits
		const auto counter_magnitude = std::sqrt( counter_forward * counter_forward + counter_side * counter_side );
		if ( counter_magnitude > max_speed )
		{
			const auto scale = max_speed / counter_magnitude;
			counter_forward *= scale;
			counter_side *= scale;
		}

		base->set_forwardmove( counter_forward );
		base->set_leftmove( counter_side );
	}

	// Main auto-stop logic - called from ragebot when about to fire
	void rage::apply_auto_stop( systems::input::usercmd* cmd, const aim_context& ctx, const settings::combat::ragebot::weapon_group& config, const systems::local::snapshot& local )
	{
		if ( !config.auto_stop.value && !config.air_stop.value )
		{
			this->m_auto_stop_state = { };
			this->m_should_stop = false;
			return;
		}

		// Safety check: ensure we have valid local player and command
		if ( !local.pawn || !cmd )
		{
			this->m_auto_stop_state = { };
			this->m_should_stop = false;
			return;
		}

		const auto speed_2d = ctx.velocity.length_2d( );
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// Move between shots: only stop when we can shoot
		if ( config.between_shots.value )
		{
			const auto can_shoot = g_shared.can_shoot( cmd, local.controller, true );
			if ( !can_shoot )
			{
				this->m_auto_stop_state.should_stop = false;
				this->m_should_stop = false;
				return;
			}
		}

		// Air auto-stop is a slowwalk for the shot, not a stop. Air acceleration is
		// capped every tick, so horizontal speed can only be bled off gradually and
		// the shot has to be released the moment the measured cone fits -- which the
		// ragebot's hitchance gate does. All this branch does is keep the request
		// alive for the whole flight; airstrafe.cpp, later in the same create_move,
		// owns the actual counter-strafe because only it models the air
		// acceleration cap. The old code only raised the flag within 10 u/s of the
		// jump apex and then fell into the `!on_ground` early-out below on every
		// other tick, clearing m_should_stop before airstrafe could ever read it:
		// in the air nothing was ever braked.
		if ( !ctx.on_ground )
		{
			if ( !config.air_stop.value )
			{
				this->m_auto_stop_state.should_stop = false;
				this->m_auto_stop_state.is_stopping = false;
				this->m_should_stop = false;
				return;
			}

			if ( speed_2d <= ctx.accurate_threshold )
			{
				// Already slow enough for an accurate shot. Let go of the brake so
				// the player keeps flying instead of being pinned mid-air, and fire on
				// this tick's real accuracy.
				this->m_auto_stop_state.should_stop = false;
				this->m_auto_stop_state.is_stopping = false;
				this->m_should_stop = false;
				return;
			}

			this->m_auto_stop_state.should_stop = true;
			this->m_auto_stop_state.is_stopping = true;
			this->m_should_stop = true;
			return;
		}

		// The command-level counter-strafe in g_misc.autostop() is the accurate one: it
		// models friction and acceleration and pushes a sub-tick analog delta, and it runs
		// on the tick after we raise m_should_stop. Re-stamping this cruder velocity-mirror
		// impulse on top of it every tick overwrote that work, which is a large part of why
		// auto-stop never appeared to do anything. Only stamp the opening impulse.
		const auto already_braking = this->m_auto_stop_state.is_stopping;

		// Early stop prediction
		if ( config.early_stop.value && speed_2d > ctx.accurate_threshold )
		{
			const auto stop_time = this->predict_stop_time( ctx.velocity, ctx.weapon_max_speed, local );

			// Start stopping early (78ms braking window before shot)
			constexpr float early_stop_window = 0.078f;

			if ( stop_time > 0.f && stop_time <= early_stop_window )
			{
				// Check if stopping now will leave us accurate
				if ( this->can_shoot_after_stop( ctx, stop_time, local ) )
				{
					if ( !already_braking )
					{
						this->apply_counter_strafe( cmd, ctx.velocity, ctx.weapon_max_speed );
					}

					this->m_auto_stop_state.should_stop = true;
					this->m_auto_stop_state.is_stopping = true;
					this->m_auto_stop_state.predicted_stop_time = stop_time;
					this->m_should_stop = true;
					return;
				}
			}
		}

		// Standard auto-stop: moving too fast for accuracy
		if ( speed_2d > ctx.accurate_threshold )
		{
			if ( !already_braking )
			{
				this->apply_counter_strafe( cmd, ctx.velocity, ctx.weapon_max_speed );
			}

			this->m_auto_stop_state.should_stop = true;
			this->m_auto_stop_state.is_stopping = true;
			this->m_should_stop = true;
			return;
		}

		// Close to accurate, full stop
		if ( speed_2d > 1.f )
		{
			base->set_forwardmove( 0.f );
			base->set_leftmove( 0.f );
			this->m_auto_stop_state.should_stop = true;
			this->m_should_stop = true;
			return;
		}

		// At max accuracy - don't need to stop
		this->m_auto_stop_state.should_stop = false;
		this->m_auto_stop_state.is_stopping = false;
		this->m_should_stop = false;
	}

} // namespace features::combat
