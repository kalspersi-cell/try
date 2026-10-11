#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	namespace detail {

		// How many of the most recent records feed the velocity / turn estimate.
		constexpr std::size_t k_velocity_window{ 4 };

		// A player cannot realistically rotate faster than this on the server.
		// Anything above it comes from a stale or noisy sample, not a real turn.
		constexpr float k_max_turn_per_tick{ 14.0f };

		// Below this the player is effectively standing still and there is
		// nothing worth predicting.
		constexpr float k_min_speed{ 5.0f };

		// Weight given to the record-derived velocity versus the replicated one.
		constexpr float k_record_velocity_weight{ 0.65f };

		// Fraction of the gap between the extrapolated speed and the replicated
		// (authoritative) speed that is closed per predicted tick. Holding a frozen
		// magnitude for up to sixteen ticks drifts badly on a target that is braking
		// or accelerating, and the aim point overshoots in exactly the window where
		// the shot matters. Nudging the magnitude back toward the live value keeps
		// long horizons honest without hard-snapping on one noisy sample.
		constexpr float k_speed_correction_per_tick{ 0.25f };

		inline float wrap_degrees( float angle )
		{
			while ( angle > 180.0f ) angle -= 360.0f;
			while ( angle < -180.0f ) angle += 360.0f;
			return angle;
		}

		inline float vector_direction( const math::vector3& v )
		{
			if ( v.x == 0.0f && v.y == 0.0f )
			{
				return 0.0f;
			}

			return std::atan2f( v.y, v.x ) * ( 180.0f / 3.14159265f );
		}

	} // namespace detail

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		if ( !addresses::globals::game_trace_manager )
		{
			return;
		}

		const auto sv_gravity = CONVAR ( "sv_gravity" )->get<float>( );

		if ( data.flags & cstypes::entity_flags::on_ground )
		{
			data.velocity.z = 0.0f;
		}
		else
		{
			data.velocity.z -= sv_gravity * cstypes::tick_interval;
		}

		const auto move_end = data.origin + data.velocity * cstypes::tick_interval;

		auto trace_result = systems::g_tracing.trace_hull(
			data.origin, move_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		if ( trace_result.fraction != 1.0f )
		{
			for ( auto i = 0; i < 2; ++i )
			{
				const auto dot = data.velocity.dot( trace_result.normal );
				data.velocity -= trace_result.normal * dot;

				const auto adjust = data.velocity.dot( trace_result.normal );
				if ( adjust < 0.0f )
				{
					data.velocity -= trace_result.normal * adjust;
				}

				const auto remaining_fraction = 1.0f - trace_result.fraction;
				const auto clip_end = trace_result.end_pos + data.velocity * ( cstypes::tick_interval * remaining_fraction );

				trace_result = systems::g_tracing.trace_hull(
					trace_result.end_pos, clip_end,
					data.obb_mins, data.obb_maxs,
					skip_entity, 0x1c3003, 4
				);

				if ( trace_result.fraction == 1.0f )
				{
					break;
				}
			}
		}

		// The final trace may start at a collision point. Its end position is the
		// clipped destination even when that follow-up trace completes fully.
		data.origin = trace_result.end_pos;

		const auto ground_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 2.0f };
		const auto ground_trace = systems::g_tracing.trace_hull(
			data.origin, ground_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		data.flags &= ~cstypes::entity_flags::on_ground;

		if ( ground_trace.fraction != 1.0f && ground_trace.normal.z > 0.7f )
		{
			data.flags |= cstypes::entity_flags::on_ground;
		}
	}

	math::vector3 shared::lagcomp::predict_position_physics( std::uintptr_t pawn, const math::vector3& origin, const math::vector3& velocity, std::uint32_t flags, const math::vector3& obb_mins, const math::vector3& obb_maxs, int ticks )
	{
		if ( ticks <= 0 )
		{
			return {};
		}

		extrapolation_data data{};
		data.origin = origin;
		data.velocity = velocity;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.flags = flags;
		data.sim_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );

		for ( auto i = 0; i < ticks; ++i )
		{
			data.sim_time += cstypes::tick_interval;
			this->predict_movement( data, pawn );
		}

		return data.origin - origin;
	}

	std::optional<shared::lagcomp::record> shared::lagcomp::extrapolate( std::uintptr_t pawn )
	{
		if ( !settings::g_combat.m_lagcomp.extrapolation.value )
		{
			return std::nullopt;
		}

		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return std::nullopt;
		}

		const auto& latest = it->second.front( );
		if ( !latest.is_valid( ) )
		{
			return std::nullopt;
		}

		// Never build a prediction on top of another prediction. Chaining them
		// compounds error and pushes the aim point further away every frame.
		if ( latest.extrapolated )
		{
			return std::nullopt;
		}

		const auto net_client = addresses::globals::network_client_service;
		if ( !net_client )
		{
			return std::nullopt;
		}

		const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
		if ( !tick_state )
		{
			return std::nullopt;
		}

		const auto server_tick = memory::read<int>( tick_state + 892 );
		const auto delta_ticks = server_tick - latest.tick;

		if ( delta_ticks <= 0 )
		{
			return std::nullopt;
		}

		const auto max_extrap = std::clamp( settings::g_combat.m_lagcomp.max_extrapolate_ticks.value, 1, 16 );
		const auto ticks_to_extrapolate = std::max( 1, std::min( delta_ticks, max_extrap ) );

		// ------------------------------------------------------------------
		// Velocity: blend the replicated velocity with the one implied by the
		// recorded positions over a short window. The replicated value is exact
		// but is read from the live pawn (already a frame ahead of the record),
		// while the record-derived value is stable but lags accelerating
		// targets. Mixing the two keeps both straight-line and curving movement
		// close to the truth without reacting to a single noisy sample.
		// ------------------------------------------------------------------
		const auto replicated_velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		auto velocity = replicated_velocity;

		float turn_per_tick{ 0.0f };

		if ( it->second.size( ) > 1 )
		{
			const auto window = std::min<std::size_t>( it->second.size( ) - 1, detail::k_velocity_window );
			const auto& oldest = it->second[ window ];

			if ( oldest.valid )
			{
				const auto elapsed = latest.simulation_time - oldest.simulation_time;

				if ( elapsed > 0.0f )
				{
					const auto derived_velocity = ( latest.origin - oldest.origin ) * ( 1.0f / elapsed );

					if ( derived_velocity.length_2d( ) > 0.1f )
					{
						// Only the horizontal component is blended; the recorded
						// delta is unreliable vertically (teleports, stairs).
						velocity.x = replicated_velocity.x + ( derived_velocity.x - replicated_velocity.x ) * detail::k_record_velocity_weight;
						velocity.y = replicated_velocity.y + ( derived_velocity.y - replicated_velocity.y ) * detail::k_record_velocity_weight;
					}

					// Angular velocity: accumulate the signed heading change across every
					// consecutive pair inside the window, then divide by the window span.
					// Differencing only the two endpoints (what this used to do) swung
					// between zero and the clamp on a single noisy sample, so a target
					// that was turning steadily was repeatedly treated as not turning at
					// all. Summing the segments keeps the same units -- degrees per tick
					// -- while averaging the sample noise out.
					float total_turn{ 0.0f };
					for ( std::size_t i = 0; i + 2 <= window; ++i )
					{
						const auto& newer = it->second[ i ];
						const auto& middle = it->second[ i + 1 ];
						const auto& older_segment = it->second[ i + 2 ];

						if ( !newer.valid || !middle.valid || !older_segment.valid )
						{
							continue;
						}

						const auto newer_direction = detail::vector_direction( newer.origin - middle.origin );
						const auto older_direction = detail::vector_direction( middle.origin - older_segment.origin );
						total_turn += detail::wrap_degrees( newer_direction - older_direction );
					}

					turn_per_tick = ( total_turn / elapsed ) * cstypes::tick_interval;
				}
			}
		}

		// Reject an obviously broken sample instead of aborting the whole
		// prediction: clamp the turn rate to what the server could produce.
		turn_per_tick = std::clamp( turn_per_tick, -detail::k_max_turn_per_tick, detail::k_max_turn_per_tick );

		auto direction = detail::vector_direction( velocity );
		auto speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

		const auto replicated_speed = std::sqrtf( replicated_velocity.x * replicated_velocity.x + replicated_velocity.y * replicated_velocity.y );

		// If the blended velocity collapsed (target stood still over the window)
		// fall back to the recorded displacement so we still make progress.
		if ( speed < detail::k_min_speed && it->second.size( ) > 1 )
		{
			const auto& next = it->second[ 1 ];
			if ( next.valid )
			{
				const auto dt = latest.simulation_time - next.simulation_time;
				if ( dt > 0.0f )
				{
					const auto fallback = ( latest.origin - next.origin ) * ( 1.0f / dt );
					speed = std::sqrtf( fallback.x * fallback.x + fallback.y * fallback.y );

					if ( speed >= detail::k_min_speed )
					{
						direction = detail::vector_direction( fallback );
						velocity.x = fallback.x;
						velocity.y = fallback.y;
					}
				}
			}
		}

		if ( speed < detail::k_min_speed )
		{
			return std::nullopt;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return std::nullopt;
		}

		const auto collision = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		math::vector3 obb_mins{}, obb_maxs{};

		if ( collision )
		{
			obb_mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			obb_maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
		}
		else
		{
			obb_mins = { -16.0f, -16.0f, 0.0f };
			obb_maxs = { 16.0f, 16.0f, 72.0f };
		}

		const auto flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );

		extrapolation_data data{};
		data.origin = latest.origin;
		data.velocity = velocity;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.flags = flags;
		data.sim_time = latest.simulation_time;
		data.direction = direction;

		// Advance the direction only as far as the server could have; the
		// per-tick clamp prevents a single noisy record from spinning the
		// extrapolated origin away from the target. The heading change is also
		// accumulated so the pose itself can be turned below.
		float total_turn{ 0.0f };

		for ( auto i = 0; i < ticks_to_extrapolate; ++i )
		{
			data.direction = detail::wrap_degrees( data.direction + turn_per_tick );
			total_turn += turn_per_tick;

			// Pull the magnitude part of the way back to the replicated speed each
			// tick. The direction keeps the record-derived heading; only the length
			// is corrected, so a braking target stops being predicted at full sprint
			// all the way through the extrapolation window.
			speed = std::fmaxf( speed + ( replicated_speed - speed ) * detail::k_speed_correction_per_tick, 0.0f );

			const auto rad = data.direction * ( 3.14159265f / 180.0f );
			data.velocity.x = std::cosf( rad ) * speed;
			data.velocity.y = std::sinf( rad ) * speed;

			data.sim_time += cstypes::tick_interval;

			this->predict_movement( data, pawn );
		}

		const auto origin_delta = data.origin - latest.origin;

		if ( origin_delta.length_sqr( ) < 0.01f )
		{
			return std::nullopt;
		}

		record extrap_record = latest;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		extrap_record.tick = cstypes::time_to_ticks( data.sim_time );
		extrap_record.extrapolated = true;

		// Move the pose to the predicted origin, and turn it by the heading change
		// on the way. Translating alone left a turning target's head and shoulders
		// behind its predicted position -- which is precisely where an anti-aim
		// keeps the head, so a strafing target was being aimed off the body for the
		// whole extrapolation window. Rotating first, then translating, keeps the
		// head on the arc the body is actually travelling.
		const auto turn_rad = total_turn * ( 3.14159265f / 180.0f );
		const auto turn_cos = std::cosf( turn_rad );
		const auto turn_sin = std::sinf( turn_rad );
		const auto turning = std::fabsf( total_turn ) > 0.01f;

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			auto& position = extrap_record.bones[ i ].position;

			if ( turning )
			{
				const auto dx = position.x - latest.origin.x;
				const auto dy = position.y - latest.origin.y;

				position.x = latest.origin.x + dx * turn_cos - dy * turn_sin;
				position.y = latest.origin.y + dx * turn_sin + dy * turn_cos;
			}

			position.x += origin_delta.x;
			position.y += origin_delta.y;
			position.z += origin_delta.z;
		}

		return extrap_record;
	}

} // namespace features::combat
