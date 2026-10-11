#pragma once

#include <shared_mutex>

#include <utilities/math/math.hpp>
#include <external/config.hpp>

namespace settings {

	struct combat
	{
		struct ragebot
		{
			// Six weapon groups plus one shared "global" group (index 6) that can
			// drive every weapon at once.
			static constexpr auto k_group_count{ 7u };
			static constexpr auto k_global_group{ 6u };

			xui::setting enabled{ true, {}, "enabled", "ragebot" };

			// When set, every weapon reads the single global group instead of its
			// own -- so one config drives them all.
			xui::setting use_global{ false, {}, "use global config", "ragebot" };

			// Shared target-selection options. These are not per weapon group:
			// they describe how a target is picked, not how a weapon is fired.
			struct general_settings
			{
				// A safe point is one that hits the same hitbox on both sides of the
				// enemy's jitter, so it stays true no matter how the resolver lands.
				xui::setting prefer_safe_point{ false, {}, "prefer safe point", "ragebot" };

				// Resolver: the recorded lagcomp pose is what the enemy's client
				// rendered, which for a jitter or flick anti-aim is one of two or
				// three discrete server-side poses. The ragebot also scans the same
				// hitboxes rotated onto the other desync extremes so a target that
				// flickers between them is still hittable. Rotated points carry a
				// score penalty, so the recorded pose still wins whenever it works.
				xui::setting resolver{ true, {}, "resolver", "ragebot" };
				config::val<int> resolver_desync{ 58, "ragebot", "resolver desync" };
			} general{};

			// Lethal body shots outrank a lethal headshot when both are available.
			xui::setting prefer_body_lethal{ false, {}, "prefer body if lethal", "ragebot" };
			// Weight targets by threat / distance / HP instead of pure damage.
			xui::setting target_priority{ true, {}, "target priority", "ragebot" };
			// Remember which way each enemy was missed and adapt (safe points,
			// raised hitchance, fresher records).
			xui::setting miss_memory{ true, {}, "miss memory", "ragebot" };
			// Hold a non-lethal shot briefly when the enemy is about to walk into
			// a lethal one.
			xui::setting shot_timing{ false, {}, "shot timing", "ragebot" };
			config::val<int> shot_timing_ticks{ 3, "ragebot", "shot timing ticks" };
			// Print ragebot shot/hold diagnostics to the console.
			xui::setting shot_log{ false, {}, "shot log", "ragebot" };

			struct weapon_group
			{
				xui::setting silent{ true, {}, "silent", "ragebot" };
				xui::setting no_spread{ false, {}, "no spread", "ragebot" };
				
				// No spread advanced options
				config::val<int> multipoint_scale{ 75, "ragebot", "multipoint scale" };
				config::val<int> spread_samples{ 128, "ragebot", "spread samples" };
				xui::setting nospread_min_damage_override{ false, {}, "no spread min damage override", "ragebot" };
				config::val<int> nospread_min_damage{ 15, "ragebot", "no spread min damage" };
				xui::setting nospread_max_damage_cap{ false, {}, "no spread max damage cap", "ragebot" };
				config::val<int> nospread_max_damage{ 100, "ragebot", "no spread max damage" };
				
				xui::setting pixel_wallbang{ false, {}, "pixel wallbang", "ragebot" };
				
				xui::setting doubletap{ false, {}, "doubletap", "ragebot" };
				xui::setting force_shot_air{ false, {}, "force shot in air", "ragebot" };
				xui::setting force_shot{ false, {}, "force shot on ground", "ragebot" };

				xui::setting body_aim{ false, {}, "body aim", "ragebot" };
				xui::setting debug_multipoints{ false, {}, "debug multipoints", "ragebot" };
				// Shrink the multipoint ring until it fits inside the current spread
				// cone, so points are only placed where the bullet can actually land.
				xui::setting dynamic_pointscale{ false, {}, "dynamic point scale", "ragebot" };

				xui::setting auto_stop{ true, {}, "auto stop", "ragebot" };
				xui::setting early_stop{ true, {}, "early stop", "ragebot" };
				xui::setting air_stop{ false, {}, "air auto stop", "ragebot" };
				xui::setting between_shots{ false, {}, "move between shots", "ragebot" };
				
				xui::setting auto_scope{ false, {}, "auto scope", "ragebot" };
				config::val<int> auto_scope_delay{ 0, "ragebot", "auto scope delay" };

				config::val<float> max_fov{ 180.0f };

				config::val<int> hitchance{ 80 };
				config::val<int> min_damage{ 101 };

				config::val<int> min_damage_override_value{ 11 };
				xui::setting min_damage_override{ false, {}, "min damage override", "ragebot" };

				config::val<int> hitchance_override_value{ 75 };
				xui::setting hitchance_override{ false, {}, "hit chance override", "ragebot" };

			config::val<float> pointscale{ 85.0f };

			config::bools<6> hitboxes{ { true, true, true, true, true, true } };
			
			// Visualize aimbot - show hitboxes where you can shoot
			xui::setting visualize_aimbot{ false, {}, "visualize aimbot", "ragebot" };
			config::bools<6> visualize_hitboxes{ { true, true, true, false, false, false } }; // head, chest, stomach, arms, legs, paws

			void init( std::string_view cat )
			{
				const auto s = std::string( cat );

				this->silent.category = s;
				this->no_spread.category = s;
				this->nospread_min_damage_override.category = s;
				this->nospread_max_damage_cap.category = s;
				this->pixel_wallbang.category = s;
				this->doubletap.category = s;
				this->force_shot_air.category = s;
				this->force_shot.category = s;
				this->body_aim.category = s;
				this->debug_multipoints.category = s;
				this->dynamic_pointscale.category = s;
				this->auto_stop.category = s;
				this->early_stop.category = s;
				this->air_stop.category = s;
				this->between_shots.category = s;
				this->auto_scope.category = s;
				this->min_damage_override.category = s;
				this->hitchance_override.category = s;

				this->max_fov.reg( s, "max fov" );
				this->hitchance.reg( s, "hit chance" );
				this->min_damage.reg( s, "min damage" );
				this->min_damage_override_value.reg( s, "min damage override value" );
				this->hitchance_override_value.reg( s, "hit chance override value" );
				this->pointscale.reg( s, "point scale" );
				this->hitboxes.reg( s, "hitboxes" );
				this->auto_scope_delay.reg( s, "auto scope delay" );
				this->visualize_aimbot.category = s;
				this->visualize_hitboxes.reg( s, "visualize hitboxes" );
			}

				void set_default_binds( )
				{
					this->force_shot_air.bind = { .key = VK_XBUTTON1, .mode = xui::bind_mode::hold_on };
					this->min_damage_override.bind = { .key = VK_XBUTTON2, .mode = xui::bind_mode::hold_on };
					this->hitchance_override.bind = { .key = VK_SPACE, .mode = xui::bind_mode::hold_on };
				}
			};

			std::array<weapon_group, k_group_count> groups{};

			ragebot( )
			{
				constexpr const char* weapon_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "lmg", "global" };

				for ( std::uint32_t i = 0; i < k_group_count; ++i )
				{
					this->groups[ i ].init( std::string( "ragebot - " ) + weapon_names[ i ] );
				}

				this->groups[ 0 ].set_default_binds( );
				this->groups[ 4 ].set_default_binds( );
			}

			weapon_group& get_group( std::uint32_t weapon_type )
			{
				if ( this->use_global.value )
				{
					return this->groups[ k_global_group ];
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < 6u ? idx : 2 ];
			}

			const weapon_group& get_group( std::uint32_t weapon_type ) const
			{
				if ( this->use_global.value )
				{
					return this->groups[ k_global_group ];
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < 6u ? idx : 2 ];
			}
		} m_ragebot{};

		struct legitbot
		{
			static constexpr auto k_group_count{ 6u };

			struct weapon_group
			{
				xui::setting aimbot{ false, {}, "aimbot", "legitbot" };
				config::val<float> fov{ 5.0f };
				config::val<int> smooth{ 5 };
				config::bools<5> hitboxes{ { true, false, false, false, false } };

				xui::setting silent_aim{ false, {}, "silent aimbot", "legitbot" };
				config::val<float> silent_fov{ 10.0f };

				// Cancels the weapon's own spread so the bullet follows the crosshair
				// exactly, including mid-jump and while moving. Same correction the
				// ragebot applies, and it keeps working when aim and trigger are off.
				xui::setting no_spread{ false, {}, "no spread", "legitbot" };

				xui::setting silent_visualize_fov{ true, {}, "silent visualize fov", "legitbot" };
				config::col silent_fov_color{ { 255, 100, 100, 150 } };

				xui::setting rcs{ true, {}, "recoil control", "legitbot" };
				config::val<int> rcs_min{ 95 };
				config::val<int> rcs_max{ 105 };

				xui::setting standalone_rcs{ false, {}, "standalone rcs", "legitbot" };
				config::val<int> standalone_rcs_strength{ 100 };
				config::val<int> standalone_rcs_min{ 95 };
				config::val<int> standalone_rcs_max{ 105 };

				xui::setting triggerbot{ false, {}, "triggerbot", "legitbot" };
				config::val<int> trigger_delay{ 5 };
				config::val<int> trigger_hitchance{ 80 };
				xui::setting trigger_head_only{ false, {}, "trigger head only", "legitbot" };
				xui::setting give_me_your_seed{ false, {}, "trigger seed mode", "legitbot" };

				xui::setting autowall{ true, {}, "autowall", "legitbot" };
				config::val<int> min_damage{ 101 };

				xui::setting visualize_fov{ true, {}, "visualize fov", "legitbot" };
				config::col fov_color{ { 255, 255, 255, 150 } };
				
				// Visualize aimbot - show hitboxes where you can shoot
				xui::setting visualize_aimbot{ false, {}, "visualize aimbot", "legitbot" };
				config::bools<5> visualize_hitboxes{ { true, true, true, false, false } }; // head, neck, torso, arms, legs

				void init( std::string_view cat )
				{
					const auto s = std::string( cat );

					this->aimbot.category = s;
					this->silent_aim.category = s;
					this->no_spread.category = s;
					this->silent_visualize_fov.category = s;
					this->rcs.category = s;
					this->standalone_rcs.category = s;
					this->triggerbot.category = s;
					this->trigger_head_only.category = s;
					this->give_me_your_seed.category = s;
					this->autowall.category = s;
					this->visualize_fov.category = s;
					this->visualize_aimbot.category = s;

					this->fov.reg( s, "fov" );
					this->smooth.reg( s, "smooth" );
					this->hitboxes.reg( s, "hitboxes" );
					this->silent_fov.reg( s, "silent fov" );
					this->silent_fov_color.reg( s, "silent fov color" );
					this->rcs_min.reg( s, "rcs min" );
					this->rcs_max.reg( s, "rcs max" );
					this->standalone_rcs_strength.reg( s, "standalone rcs strength" );
					this->standalone_rcs_min.reg( s, "standalone rcs min" );
					this->standalone_rcs_max.reg( s, "standalone rcs max" );
					this->trigger_delay.reg( s, "trigger delay" );
					this->trigger_hitchance.reg( s, "trigger hitchance" );
					this->min_damage.reg( s, "min damage" );
					this->fov_color.reg( s, "fov color" );
					this->visualize_hitboxes.reg( s, "visualize hitboxes" );
				}
			};

			xui::setting enabled{ false, {}, "enabled", "legitbot" };
			std::array<weapon_group, k_group_count> groups{};

			legitbot( )
			{
				constexpr const char* weapon_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "lmg" };

				for ( auto i = 0u; i < k_group_count; ++i )
				{
					this->groups[ i ].init( std::string( "legitbot - " ) + weapon_names[ i ] );
				}
			}

			weapon_group& get_group( std::uint32_t weapon_type )
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}

			const weapon_group& get_group( std::uint32_t weapon_type ) const
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}
		} m_legitbot{};

		struct antiaim
		{
			enum class pitch_mode : std::uint8_t
			{
				none,
				down,
				up
			};

			enum class spin_direction : std::uint8_t
			{
				right,
				left
			};

			xui::setting enabled{ true, {}, "anti aim", "anti aim" };
			config::enm<pitch_mode> pitch{ pitch_mode::down, "anti aim", "pitch" };
			xui::setting auto_yaw_adjust{true, {}, "correct yaw to compensate for the models inherit sideways roll", "anti aim"};
			xui::setting manual_left{ false, { 'Z', xui::bind_mode::toggle }, "force left", "anti aim" };
			xui::setting manual_right{ false, { 'C', xui::bind_mode::toggle }, "force right", "anti aim" };
			xui::setting hide_shots{ true, {}, "hide onshot", "anti aim" };
			xui::setting avoid_backstab{ true, {}, "avoid backstab", "anti aim" };
			xui::setting movement_correction{ true, {}, "movement correction", "anti aim" };

			// Back-to-enemy: when an enemy gets close, spin the body around so the
			// head points away from them and can't be shot. This replaces having to
			// manually hold force left / force right in close fights.
			xui::setting near_enemy_turn{ true, {}, "near enemy turn", "anti aim" };
			config::val<float> near_enemy_range{ 550.0f, "anti aim", "near enemy range" };

			xui::setting spinbot{ false, {}, "spinbot", "anti aim" };
			config::val<float> spinbot_speed{ 45.0f, "anti aim", "spinbot speed" };
			config::enm<spin_direction> spinbot_direction{ spin_direction::right, "anti aim", "spinbot direction" };

			xui::setting jitter{ false, {}, "jitter", "anti aim" };
			config::val<float> jitter_speed{ 20.0f, "anti aim", "jitter speed" };
			config::val<float> jitter_angle{ 90.0f, "anti aim", "jitter angle" };
			xui::setting jitter_yaw{ false, {}, "jitter yaw", "anti aim" };
			config::val<float> jitter_yaw_angle{ 0.0f, "anti aim", "jitter yaw angle" };

			xui::setting one_tick_aa{ false, {}, "1 tick aa", "anti aim" };
			config::val<int> one_tick_aa_ticks{ 1, "anti aim", "1 tick aa ticks" };

			// Freestand: picks the anti-aim side automatically instead of making the
			// user hold force left / force right. The side with the least enemy
			// exposure wins, so a corner hides the body on its own.
			xui::setting freestand{ false, {}, "free stand", "anti aim" };
			config::val<float> freestand_range{ 1800.0f, "anti aim", "freestand range" };
			xui::setting freestand_visualize{ false, {}, "freestand visualize", "anti aim" };

			// Direction indicator: an arc drawn in screen space around the player
			// pointing where the anti-aim yaw faces.
			xui::setting direction_indicator{ true, {}, "direction indicator", "anti aim" };
			config::col direction_indicator_color{ { 173, 192, 255, 220 }, "anti aim", "direction indicator color" };
			config::val<float> direction_indicator_radius{ 34.0f, "anti aim", "direction indicator radius" };
			config::val<float> direction_indicator_offset{ 0.0f, "anti aim", "direction indicator offset" };
			config::val<float> direction_indicator_opacity{ 100.0f, "anti aim", "direction indicator opacity" };
			config::val<float> direction_indicator_outline{ 1.6f, "anti aim", "direction indicator outline" };
			xui::setting direction_indicator_pulse{ false, {}, "direction indicator pulse", "anti aim" };
			config::val<float> direction_indicator_pulse_speed{ 1.2f, "anti aim", "direction indicator pulse speed" };
			xui::setting direction_indicator_fake{ false, {}, "direction indicator fake yaw", "anti aim" };
			config::col direction_indicator_fake_color{ { 255, 120, 120, 220 }, "anti aim", "direction indicator fake color" };
			xui::setting direction_indicator_glow{ true, {}, "direction indicator glow", "anti aim" };
			config::val<float> direction_indicator_glow_strength{ 0.55f, "anti aim", "direction indicator glow strength" };

			antiaim( )
			{
				this->manual_left.bind.excludes = &this->manual_right;
				this->manual_right.bind.excludes = &this->manual_left;

				this->freestand_visualize.category = "anti aim";
				this->freestand_range.reg( "anti aim", "freestand range" );
				this->near_enemy_range.reg( "anti aim", "near enemy range" );
			}
		} m_antiaim{};

		struct quickpeek
		{
			xui::setting enabled{ false, { 'V', xui::bind_mode::hold_on }, "quick peek", "peek assistance" };
			config::col color{ { 173, 192, 255, 255 }, "peek assistance", "quick peek color" };
			config::col retrack_color{ { 255, 171, 234, 255 }, "peek assistance", "retracting color" };
		} m_quickpeek{};

		struct duckpeek
		{
			xui::setting enabled{ false, { VK_LMENU, xui::bind_mode::hold_on }, "duck peek", "peek assistance" };
		} m_duckpeek{};

		struct lagcomp_settings
		{
			// Master switch for shooting at rewound poses. With this off the
			// ragebot only ever scans the newest record, which is the behaviour of
			// a ragebot without backtracking at all.
			xui::setting backtrack{ true, {}, "backtrack", "ragebot" };
			config::val<int> max_backtrack_ticks{ 12, "ragebot", "max backtrack ticks" };

			// How many poses inside the window are tested. Off keeps the three
			// spaced samples (newest, middle, oldest); on lets the mode pick 2-5.
			xui::setting scan_records_enabled{ false, {}, "scan records", "ragebot" };
			config::val<int> scan_records_mode{ 1, "ragebot", "scan records mode" };
			xui::setting extrapolation{ true, {}, "extrapolation", "ragebot" };
			config::val<int> max_extrapolate_ticks{ 8, "ragebot", "max extrapolate ticks" };

			// Draws the rewound poses the ragebot is willing to shoot at, with the
			// tick delta of each. Purely visual; it never changes what gets scanned.
			xui::setting visualize_backtrack{ false, {}, "visualize backtrack", "ragebot" };
			config::col backtrack_color{ { 255, 171, 234, 200 }, "ragebot", "backtrack color" };
			xui::setting backtrack_fade{ true, {}, "backtrack fade", "ragebot" };
		} m_lagcomp{};

		struct knifebot
		{
			// Knifebot stays out of the menu; the melee path is disabled in rage.cpp.
			xui::setting enabled{ false, {}, "knifebot", "other 'bots'" };
			config::val<float> max_fov{ 180.0f, "knifebot", "max fov" };
		} m_knifebot{};

		struct zeusbot
		{
			xui::setting enabled{ true, {}, "zeusbot", "other 'bots'" };
			xui::setting drop_after{ true, {}, "drop after", "zeusbot" };
			config::val<float> max_fov{ 180.0f, "zeusbot", "max fov" };
		} m_zeusbot{};

		struct autos
		{
			xui::setting revolver{ true, {}, "auto revolver", "autos" };
			xui::setting scope{ true, {}, "auto scope", "autos" };
		} m_autos{};

		struct hitbox_override
		{
			xui::setting enabled{ false, {}, "force hitboxes", "ragebot hitboxes" };
			config::bools<6> hitboxes{ { false, false, false, false, false, false } }; // head, neck, torso, arms, legs, paws
			
			void init()
			{
				this->hitboxes.reg( "ragebot hitboxes", "forced hitboxes" );
			}
		} m_hitbox_override{};

		struct penetration_crosshair
		{
			xui::setting enabled{ false, {}, "penetration crosshair", "pen crosshair" };
			config::col can_penetrate_fill{ { 173, 192, 255, 120 }, "pen crosshair", "can penetrate fill" };
			config::col can_penetrate_outline{ { 173, 192, 255, 210 }, "pen crosshair", "can penetrate outline" };
			config::col blocked_fill{ { 252, 217, 240, 80 }, "pen crosshair", "blocked fill" };
			config::col blocked_outline{ { 252, 217, 240, 160 }, "pen crosshair", "blocked outline" };
			xui::setting glow{ true, {}, "glow", "pen crosshair" };
			config::val<float> glow_strength{ 1.0f, "pen crosshair", "glow strength" };

			// Shape and geometry. 0 dot, 1 square, 2 cross, 3 circle.
			config::val<int> style{ 1, "pen crosshair", "style" };
			config::val<float> size{ 3.0f, "pen crosshair", "size" };
			config::val<float> thickness{ 1.5f, "pen crosshair", "thickness" };
			config::val<float> gap{ 2.0f, "pen crosshair", "gap" };
			config::val<float> outline_thickness{ 1.0f, "pen crosshair", "outline thickness" };
			xui::setting outline{ true, {}, "outline", "pen crosshair" };
			xui::setting center_dot{ true, {}, "center dot", "pen crosshair" };
			config::val<float> dot_size{ 1.5f, "pen crosshair", "dot size" };
		} m_penetration_crosshair{};
	};

	struct esp
	{			enum class cham_ids : std::uint8_t
			{
				liquid, metallic, matte, flat, bloom, outlines, glow, electric, distortion, hologram, pearl, glass,
				liquid_ignorez, matte_ignorez, flat_ignorez, bloom_ignorez, outlines_ignorez, glow_ignorez, distortion_ignorez, hologram_ignorez, glass_ignorez,
				count
			};

			// How many materials the menu lists. The ")iz" variants are chosen
			// automatically by each layer's occluded toggle, so they are kept out of
			// the dropdown instead of cluttering it with duplicates.
			static constexpr auto k_visible_cham_materials{ static_cast< int >( cham_ids::liquid_ignorez ) };

		struct chams_layer
		{
			xui::setting enabled{ false, {}, "chams layer", "chams" };
			config::col color{ { 255, 255, 255, 255 } };
			config::enm<cham_ids> material{ cham_ids::matte };
			xui::setting occluded{ true, {}, "occluded", "chams" };

			void rename( std::string_view cat, std::string_view layer_name )
			{
				const auto s = std::string( cat );
				this->enabled.name = std::string( layer_name );
				this->enabled.category = s;
				this->occluded.name = std::string( layer_name ) + " occluded";
				this->occluded.category = s;
			}

			void init( std::string_view cat, std::string_view layer_name )
			{
				const auto s = std::string( cat );
				this->rename( cat, layer_name );
				this->color.reg( s, std::string( layer_name ) + " color" );
				this->material.reg( s, std::string( layer_name ) + " material" );
			}

		};

		struct chams_config
		{
			xui::setting enabled{ false, {}, "chams", "chams" };
			chams_layer primary{};
			chams_layer secondary{};
			chams_layer overlay{};
		};

		struct glow_target
		{
			xui::setting enabled{ false, {}, "glow", "glow" };
			config::col color{ { 173, 192, 255, 75 } };

			void init( std::string_view cat, std::string_view color_name = "color" )
			{
				this->color.reg( cat, color_name );
			}
		};

		struct player
		{
			struct overlay
			{
				xui::setting enabled{ true, {}, "esp overlay", "esp" };

				struct box
				{
					enum class style_type : std::uint8_t { full, cornered };

					xui::setting enabled{};
					config::enm<style_type> style{ style_type::cornered };
					xui::setting fill{};
					xui::setting outline{};
					config::val<float> corner_length{ 10.0f };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 255, 171, 234, 255 } };

					box( ) = default;

					explicit box( const std::string& prefix )
						: enabled{ false, {}, "bounding box", prefix + " box" }
						, fill{ true, {}, "fill", prefix + " box" }
						, outline{ true, {}, "outline", prefix + " box" }
					{
						const auto cat = prefix + " box";
						this->style.reg( cat, "style" );
						this->corner_length.reg( cat, "corner length" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				struct skeleton
				{
					enum class mode : std::uint8_t { normal, backtrack };

					xui::setting enabled{};
					config::enm<mode> type{ mode::backtrack };
					config::val<float> thickness{ 1.5f };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 220, 225, 240, 255 } };

					skeleton( ) = default;

					explicit skeleton( const std::string& prefix )
						: enabled{ false, {}, "skeleton", prefix + " skeleton" }
					{
						const auto cat = prefix + " skeleton";
						this->type.reg( cat, "mode" );
						this->thickness.reg( cat, "thickness" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				struct health_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::left };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 173, 192, 255, 255 } };
					config::col low_color{ { 130, 145, 200, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					health_bar( ) = default;

					explicit health_bar( const std::string& prefix )
						: enabled{ true, {}, "health bar", prefix + " health" }
						, outline_setting{ true, {}, "outline", prefix + " health" }
						, gradient{ true, {}, "gradient", prefix + " health" }
						, show_value{ true, {}, "show value", prefix + " health" }
						, glow{ true, {}, "glow", prefix + " health" }
					{
						const auto cat = prefix + " health";
						this->position.reg( cat, "position" );
						this->full_color.reg( cat, "full color" );
						this->low_color.reg( cat, "low color" );
						this->background_color.reg( cat, "background" );
						this->outline_color.reg( cat, "outline color" );
						this->text_color.reg( cat, "text color" );
						this->glow_color.reg( cat, "glow color" );
						this->glow_strength.reg( cat, "glow strength" );
					}
				};

				struct ammo_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::bottom };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 255, 171, 234, 255 } };
					config::col low_color{ { 255, 210, 244, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					ammo_bar( ) = default;

					explicit ammo_bar( const std::string& prefix )
						: enabled{ true, {}, "ammo bar", prefix + " ammo" }
						, outline_setting{ true, {}, "outline", prefix + " ammo" }
						, gradient{ true, {}, "gradient", prefix + " ammo" }
						, show_value{ false, {}, "show value", prefix + " ammo" }
						, glow{ true, {}, "glow", prefix + " ammo" }
					{
						const auto cat = prefix + " ammo";
						this->position.reg( cat, "position" );
						this->full_color.reg( cat, "full color" );
						this->low_color.reg( cat, "low color" );
						this->background_color.reg( cat, "background" );
						this->outline_color.reg( cat, "outline color" );
						this->text_color.reg( cat, "text color" );
						this->glow_color.reg( cat, "glow color" );
						this->glow_strength.reg( cat, "glow strength" );
					}
				};

				struct info_flags
				{
					enum flag : std::uint8_t
					{
						money = 0, armor, kit, scoped, defusing, flashed, ping, distance, count
					};

					xui::setting enabled{};
					config::bools<count> flags{ { false, false, false, true, true, true, true, false } };

					config::col money_color{ { 160, 210, 140, 255 } };
					config::col armor_color{ { 220, 225, 240, 255 } };
					config::col kit_color{ { 173, 192, 255, 255 } };
					config::col scoped_color{ { 220, 225, 240, 255 } };
					config::col defusing_color{ { 173, 192, 255, 255 } };
					config::col flashed_color{ { 240, 230, 170, 255 } };
					config::col distance_color{ { 185, 190, 205, 255 } };

					info_flags( ) = default;

					explicit info_flags( const std::string& prefix ) : enabled{ true, {}, "info flags", prefix + " flags" }
					{
						const auto cat = prefix + " flags";
						this->flags.reg( cat, "flags" );
						this->money_color.reg( cat, "money color" );
						this->armor_color.reg( cat, "armor color" );
						this->kit_color.reg( cat, "kit color" );
						this->scoped_color.reg( cat, "scoped color" );
						this->defusing_color.reg( cat, "defusing color" );
						this->flashed_color.reg( cat, "flashed color" );
						this->distance_color.reg( cat, "distance color" );
					}

					[[nodiscard]] bool has( flag f ) const { return this->flags[ f ]; }
				};

				struct name
				{
					xui::setting enabled{};
					config::col color{ { 255, 255, 255, 225 } };

					name( ) = default;

					explicit name( const std::string& prefix ) : enabled{ true, {}, "name", prefix + " name" }
					{
						this->color.reg( prefix + " name", "color" );
					}
				};

				struct weapon
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					xui::setting enabled{};
					config::enm<display_type> display{ display_type::text_and_icon };

					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					weapon( ) = default;

					explicit weapon( const std::string& prefix ) : enabled{ true, {}, "weapon", prefix + " weapon" }
					{
						const auto cat = prefix + " weapon";
						this->display.reg( cat, "display" );
						this->text_color.reg( cat, "text color" );
						this->icon_color.reg( cat, "icon color" );
					}
				};

				struct oof_arrow
				{
					xui::setting enabled{};
					xui::setting glow{};
					config::val<float> width{ 20.0f };
					config::val<float> height{ 15.0f };
					config::val<float> radius_x{ 200.0f };
					config::val<float> radius_y{ 200.0f };
					config::val<float> glow_strength{ 1.0f };

					config::col visible_color{ { 255, 171, 234, 255 } };
					config::col occluded_color{ { 173, 192, 255, 255 } };

					oof_arrow( ) = default;

					explicit oof_arrow( const std::string& prefix ) : enabled{ true, {}, "oof arrow", prefix + " oof" }, glow{ true, {}, "glow", prefix + " oof" }
					{
						const auto cat = prefix + " oof";
						this->width.reg( cat, "width" );
						this->height.reg( cat, "height" );
						this->radius_x.reg( cat, "radius x" );
						this->radius_y.reg( cat, "radius y" );
						this->glow_strength.reg( cat, "glow strength" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				box m_box{};
				skeleton m_skeleton{};
				health_bar m_health_bar{};
				ammo_bar m_ammo_bar{};
				info_flags m_info_flags{};
				name m_name{};
				weapon m_weapon{};
				oof_arrow m_oof_arrow{};

				struct sound_circles
				{
					xui::setting enabled{};
					xui::setting fade{};
					xui::setting ripple{};
					config::col color{ { 255, 182, 193, 200 } };
					config::col fade_color{ { 255, 182, 193, 0 } };
					config::col ripple_color{ { 255, 182, 193, 150 } };
					config::val<float> size{ 100.0f };
					config::val<float> duration{ 1.5f };

					sound_circles( ) = default;

					explicit sound_circles( const std::string& prefix )
						: enabled{ true, {}, "sound circles", prefix + " sound circles" }
						, fade{ true, {}, "fade", prefix + " sound circles" }
						, ripple{ true, {}, "ripple", prefix + " sound circles" }
					{
						const auto cat = prefix + " sound circles";
						this->color.reg( cat, "color" );
						this->fade_color.reg( cat, "fade color" );
						this->ripple_color.reg( cat, "ripple color" );
						this->size.reg( cat, "size" );
						this->duration.reg( cat, "duration" );
					}
				};

				sound_circles m_sound_circles{};

				overlay( ) = default;

				explicit overlay( const char* prefix, bool enabled_default = true )
					: overlay{ std::string{ prefix }, enabled_default }
				{
				}

				explicit overlay( const std::string& prefix, bool enabled_default = true )
					: enabled{ enabled_default, {}, "esp overlay", prefix }
					, m_box{ prefix }
					, m_skeleton{ prefix }
					, m_health_bar{ prefix }
					, m_ammo_bar{ prefix }
					, m_info_flags{ prefix }
					, m_name{ prefix }
					, m_weapon{ prefix }
					, m_oof_arrow{ prefix }
					, m_sound_circles{ prefix }
				{
				}
			};

			std::array<overlay, 2> m_overlay{ { overlay{ "esp enemy" }, overlay{ "esp team", false } } };

			struct chams
			{
				chams_config enemy
				{
					.enabled = { true, {}, "chams", "chams enemy" },
					.primary = {.enabled = { true, {}, "primary layer", "chams enemy" }, .color = { { 173, 192, 255, 150 }, "chams enemy", "primary color" }, .material = { cham_ids::flat, "chams enemy", "primary material" } },
					.secondary = {.enabled = { true, {}, "secondary layer", "chams enemy" }, .color = { { 255, 208, 243, 118 }, "chams enemy", "secondary color" }, .material = { cham_ids::flat_ignorez, "chams enemy", "secondary material" } }
				};
				chams_config enemy_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams enemy ragdoll" } };
				chams_config team{ .enabled = { false, {}, "chams", "chams team" } };
				chams_config team_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams team ragdoll" } };
				chams_config local
				{
					.enabled = { true, {}, "chams", "chams local" },
					.overlay = {.enabled = { true, {}, "overlay layer", "chams local" }, .color = { { 173, 192, 255, 175 }, "chams local", "overlay color" }, .material = { cham_ids::outlines, "chams local", "overlay material" } }
				};
				chams_config local_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams local ragdoll" } };

				chams_config backtrack
				{
					.enabled = { false, {}, "backtrack chams", "chams backtrack" },
					.primary = {.enabled = { false, {}, "primary layer", "chams backtrack" }, .color = { { 173, 192, 255, 25 }, "chams backtrack", "primary color" }, .material = { cham_ids::flat, "chams backtrack", "primary material" } },
					.secondary = {.enabled = { false, {}, "secondary layer", "chams backtrack" }, .color = { { 173, 192, 255, 255 }, "chams backtrack", "secondary color" }, .material = { cham_ids::outlines, "chams backtrack", "secondary material" } }
				};

				chams_config onshot
				{
					.enabled = { false, {}, "onshot chams",    "chams onshot" },
					.primary = {.enabled = { true,  {}, "primary layer",   "chams onshot" }, .color = { { 255, 100, 100, 200 }, "chams onshot", "primary color" }, .material = { cham_ids::flat, "chams onshot", "primary material" } },
					.secondary = {.enabled = { false, {}, "secondary layer", "chams onshot" }, .color = { { 255, 100, 100, 100 }, "chams onshot", "secondary color" }, .material = { cham_ids::flat_ignorez, "chams onshot", "secondary material" } },
					.overlay = {.enabled = { false, {}, "overlay layer",   "chams onshot" }, .color = { { 255, 100, 100, 255 }, "chams onshot", "overlay color" }, .material = { cham_ids::outlines, "chams onshot", "overlay material" } },
				};
				config::val<float> onshot_fade_time {0.8f, "chams onshot", "fade time"};

				chams( )
				{
					// Unique config keys per layer. Without this every layer's enabled
					// and occluded flag lands on one shared key and configs clobber
					// each other - chams that never save, and viewmodel chams that
					// only appear after a manual toggle.
					const auto fix = []( chams_config& c, const char* cat )
						{
							c.primary.rename( cat, "primary layer" );
							c.secondary.rename( cat, "secondary layer" );
							c.overlay.rename( cat, "overlay layer" );
						};

					fix( enemy, "chams enemy" );
					fix( enemy_ragdoll, "chams enemy ragdoll" );
					fix( team, "chams team" );
					fix( team_ragdoll, "chams team ragdoll" );
					fix( local, "chams local" );
					fix( local_ragdoll, "chams local ragdoll" );
					fix( backtrack, "chams backtrack" );
					fix( onshot, "chams onshot" );
				}
			} m_chams{};

			struct glow
			{
				glow_target enemy{ .enabled = { true, {}, "glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "color" } };
				glow_target enemy_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "ragdoll color" } };
				glow_target team{ .enabled = { true, {}, "glow", "glow team" }, .color = { { 225, 225, 225, 40 }, "glow team", "color" } };
				glow_target team_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow team" }, .color = { { 173, 192, 255, 40 }, "glow team", "ragdoll color" } };
				glow_target local{ .enabled = { false, {}, "glow", "glow local" }, .color = { { 252, 217, 240, 50 }, "glow local", "color" } };
				glow_target local_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow local" }, .color = { { 173, 192, 255, 40 }, "glow local", "ragdoll color" } };
		} m_glow{};

	} m_player{};

		struct viewmodel
		{
			chams_config weapon
			{
				.enabled = { true, {}, "weapon chams", "viewmodel" },
				.overlay = {.enabled = { true, {}, "overlay layer", "viewmodel weapon" }, .color = { { 217, 173, 202, 175 }, "viewmodel weapon", "overlay color" }, .material = { cham_ids::glow, "viewmodel weapon", "overlay material" } }
			};				chams_config arms
				{
					.enabled = { true, {}, "arms chams", "viewmodel" },
					.primary = {.enabled = { false, {}, "primary layer", "viewmodel arms" }, .color = { { 173, 192, 255, 255 }, "viewmodel arms", "primary color" }, .material = { cham_ids::outlines, "viewmodel arms", "primary material" } },
					.overlay = {.enabled = { true, {}, "overlay layer", "viewmodel arms" }, .color = { { 173, 192, 255, 255 }, "viewmodel arms", "overlay color" }, .material = { cham_ids::outlines, "viewmodel arms", "overlay material" } }
				};

				viewmodel( )
				{
					weapon.primary.rename( "viewmodel weapon", "primary layer" );
					weapon.secondary.rename( "viewmodel weapon", "secondary layer" );
					weapon.overlay.rename( "viewmodel weapon", "overlay layer" );
					arms.primary.rename( "viewmodel arms", "primary layer" );
					arms.secondary.rename( "viewmodel arms", "secondary layer" );
					arms.overlay.rename( "viewmodel arms", "overlay layer" );
				}
			} m_viewmodel{};

		struct local_alpha
		{
			xui::setting enabled{ true, {}, "lower opacity", "chams local" };
			config::val<float> opacity{ 0.5f, "chams local", "opacity" };
			xui::setting only_scoped{ true, {}, "only when scoped", "chams local" };
		} m_local_alpha{};

		struct item
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "utility" };

			struct overlay
			{
				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::icon };
					config::val<float> max_distance{ 50.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init( std::string_view cat )
					{
						const auto s = std::string( cat );
						this->display.reg( s, "display" );
						this->max_distance.reg( s, "max distance" );
						this->text_color.reg( s, "text color" );
						this->icon_color.reg( s, "icon color" );
					}
				};

				xui::setting enabled{ true, {}, "item esp", "esp items" };
				xui::setting pistol{ false, {}, "pistol", "esp items" };
				xui::setting smg{ false, {}, "smg", "esp items" };
				xui::setting rifle{ false, {}, "rifle", "esp items" };
				xui::setting shotgun{ false, {}, "shotgun", "esp items" };
				xui::setting sniper{ true, {}, "sniper", "esp items" };
				xui::setting utility{ true, {}, "utility", "esp items" };

				std::array<group, k_group_count> groups{};

				overlay( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						this->groups[ i ].init( std::string( "esp items - " ) + k_group_names[ i ] );
					}

					this->groups[ 4 ].display = group::display_type::text_and_icon;
					this->groups[ 4 ].max_distance = 100.0f;
					this->groups[ 5 ].display = group::display_type::text_and_icon;
					this->groups[ 5 ].max_distance = 100.0f;
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				group& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const group& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_overlay{};

			struct chams
			{
				xui::setting enabled{ true, {}, "item chams", "chams items" };
				xui::setting pistol{ false, {}, "pistol", "chams items" };
				xui::setting smg{ false, {}, "smg", "chams items" };
				xui::setting rifle{ false, {}, "rifle", "chams items" };
				xui::setting shotgun{ false, {}, "shotgun", "chams items" };
				xui::setting sniper{ true, {}, "sniper", "chams items" };
				xui::setting utility{ true, {}, "utility", "chams items" };

				std::array<chams_config, k_group_count> groups{};

				chams( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						const auto cat = std::string( "chams items - " ) + k_group_names[ i ];
						this->groups[ i ].primary.init( cat, "primary layer" );
						this->groups[ i ].secondary.init( cat, "secondary layer" );
					}

					this->groups[ 4 ].primary.enabled.value = true;
					this->groups[ 4 ].primary.color = { 173, 192, 255, 255 };
					this->groups[ 4 ].primary.material = cham_ids::flat;

					this->groups[ 5 ].primary.enabled.value = true;
					this->groups[ 5 ].primary.color = { 173, 192, 255, 255 };
					this->groups[ 5 ].primary.material = cham_ids::flat;
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				chams_config& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const chams_config& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_chams{};

			struct glow
			{
				xui::setting enabled{ true, {}, "item glow", "glow items" };
				xui::setting pistol{ false, {}, "pistol", "glow items" };
				xui::setting smg{ false, {}, "smg", "glow items" };
				xui::setting rifle{ false, {}, "rifle", "glow items" };
				xui::setting shotgun{ false, {}, "shotgun", "glow items" };
				xui::setting sniper{ true, {}, "sniper", "glow items" };
				xui::setting utility{ true, {}, "utility", "glow items" };

				std::array<glow_target, k_group_count> groups{};

				glow( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						const auto cat = std::string( "glow items - " ) + k_group_names[ i ];
						this->groups[ i ].init( cat );
					}

					this->groups[ 4 ].color = { 173, 192, 255, 50 };
					this->groups[ 5 ].color = { 173, 192, 255, 50 };
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				glow_target& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const glow_target& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_glow{};
		} m_item{};

		struct projectile
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[ ]{ "he grenade", "flashbang", "smoke", "molotov", "decoy", "inferno" };

			struct overlay
			{
				struct infernos
				{
					config::col fill_color{ { 173, 192, 255, 50 }, "esp inferno", "fill color" };
					config::col outline_color{ { 255, 171, 234, 150 }, "esp inferno", "outline color" };
					config::val<float> outline_thickness{ 1.5f, "esp inferno", "outline thickness" };
					xui::setting glow{ true, {}, "glow", "esp inferno" };
					config::val<float> glow_strength{ 0.55f, "esp inferno", "glow strength" };
				} m_infernos{};

				struct indicator
				{
					static constexpr auto k_group_count{ 3u };
					static constexpr const char* k_group_names[ ]{ "he grenade", "molotov", "inferno" };

					struct group
					{
						xui::setting enabled{ true, {}, "", "" };
						config::col arc_color{ { 173, 192, 255, 225 } };
						config::col icon_color{ { 255, 255, 255, 225 } };
						config::col background_color{ { 0, 0, 0, 175 } };
						xui::setting glow{ true, {}, "", "" };
						config::val<float> glow_strength{ 1.0f };

						void init( std::string_view cat )
						{
							const auto s = std::string( cat );
							this->enabled.name = std::string( cat );
							this->arc_color.reg( s, "arc color" );
							this->icon_color.reg( s, "icon color" );
							this->background_color.reg( s, "background color" );
							this->glow_strength.reg( s, "glow strength" );
						}
					};

					std::array<group, k_group_count> groups{};

					indicator( )
					{
						for ( auto i = 0u; i < k_group_count; ++i )
						{
							this->groups[ i ].init( std::string( "esp indicator - " ) + k_group_names[ i ] );
						}

						this->groups[ 2 ].arc_color = { 255, 171, 234, 225 };
					}

					group& get_group( std::uint32_t id )
					{
						return this->groups[ id < k_group_count ? id : 0 ];
					}

					const group& get_group( std::uint32_t id ) const
					{
						return this->groups[ id < k_group_count ? id : 0 ];
					}
				} m_indicator{};

				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::text_and_icon };
					config::val<float> max_distance{ 100.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init( std::string_view cat )
					{
						const auto s = std::string( cat );
						this->display.reg( s, "display" );
						this->max_distance.reg( s, "max distance" );
						this->text_color.reg( s, "text color" );
						this->icon_color.reg( s, "icon color" );
					}
				};

				xui::setting enabled{ true, {}, "projectile esp", "esp projectiles" };
				xui::setting he_grenade{ true, {}, "he grenade", "esp projectiles" };
				xui::setting flashbang{ true, {}, "flashbang", "esp projectiles" };
				xui::setting smoke{ true, {}, "smoke", "esp projectiles" };
				xui::setting molotov{ true, {}, "molotov", "esp projectiles" };
				xui::setting decoy{ true, {}, "decoy", "esp projectiles" };
				xui::setting inferno{ true, {}, "inferno", "esp projectiles" };

				std::array<group, 5> groups{};

				overlay( )
				{
					for ( auto i = 0u; i < 5u; ++i )
					{
						this->groups[ i ].init( std::string( "esp projectiles - " ) + k_group_names[ i ] );
					}
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->he_grenade;
					case 1: return this->flashbang;
					case 2: return this->smoke;
					case 3: return this->molotov;
					case 4: return this->decoy;
					case 5: return this->inferno;
					default: return this->he_grenade;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->he_grenade.value;
					case 1: return this->flashbang.value;
					case 2: return this->smoke.value;
					case 3: return this->molotov.value;
					case 4: return this->decoy.value;
					case 5: return this->inferno.value;
					default: return false;
					}
				}

				group& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < 5 ? group_id : 0 ];
				}

				const group& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < 5 ? group_id : 0 ];
				}
			} m_overlay{};

			struct tracers
			{

			} m_tracers{};
		} m_projectile{};

		struct other
		{
			// The bomb timer and spectator list toggles lived here. Neither was ever
			// wired to a draw path, so they only lied to the user and are gone.
		} m_other{};
	};

	struct changer
	{
		struct applied_skin
		{
			int paint_kit_id{};
			float wear{ 0.01f };
			int seed{};
			bool stattrak{};
			// Id of the custom model applied to this weapon (empty = stock model).
			std::string custom_model{};

			bool operator==( const applied_skin& ) const = default;
		};

		struct skin_map_field : config::custom_field
		{
			std::unordered_map<std::int16_t, applied_skin> data{};

			// The skin map is read from the game thread (frame stage notify) and
			// mutated from the render thread (menu, config load/import). Structural
			// changes rehash the map and invalidate any iterator the reader holds,
			// so every structural access must go through this lock.
			mutable std::shared_mutex mtx{};

			nlohmann::json serialize( ) const override
			{
				std::shared_lock lock( this->mtx );

				auto j = nlohmann::json::object( );
				for ( const auto& [def, s] : data )
				{
					j[ std::to_string( def ) ] = nlohmann::json
					{
						{"p", s.paint_kit_id},
						{"w", s.wear},
						{"s", s.seed},
						{"t", s.stattrak},
						{"cm", s.custom_model}
					};
				}

				return j;
			}

			void deserialize( const nlohmann::json& j ) override
			{
				std::unique_lock lock( this->mtx );

				data.clear( );

				if ( !j.is_object( ) )
				{
					return;
				}

				for ( auto it = j.begin( ); it != j.end( ); ++it )
				{
					try
					{
						const auto def = static_cast< std::int16_t >( std::stoi( it.key( ) ) );
						auto& s = data[ def ];
						s.paint_kit_id = it.value( ).value( "p", 0 );
						s.wear = it.value( ).value( "w", 0.01f );
						s.seed = it.value( ).value( "s", 0 );
						s.stattrak = it.value( ).value( "t", false );
						s.custom_model = it.value( ).value( "cm", std::string{} );
					}
					catch ( ... ) {}
				}
			}
		};

		struct agent_selection_field : config::custom_field
		{
			std::int16_t ct_def{};
			std::int16_t t_def{};
			// Custom agent model paths overriding the econ selections above. Empty
			// means the econ definition (or no agent) is used for that team.
			std::string ct_model{};
			std::string t_model{};
			// Custom weapon (knife) model written to the first-person weapon. It is
			// listed on the same models page as the player models because that is
			// where a custom model belongs, but only one can be worn at a time.
			std::string weapon_model{};

			nlohmann::json serialize( ) const override
			{
				return nlohmann::json
				{
					{ "ct", ct_def },
					{ "t", t_def },
					{ "ctm", ct_model },
					{ "tm", t_model },
					{ "wm", weapon_model }
				};
			}

			void deserialize( const nlohmann::json& j ) override
			{
				if ( !j.is_object( ) )
				{
					return;
				}

				ct_def = j.value( "ct", static_cast< std::int16_t >( 0 ) );
				t_def = j.value( "t", static_cast< std::int16_t >( 0 ) );
				ct_model = j.value( "ctm", std::string{} );
				t_model = j.value( "tm", std::string{} );
				weapon_model = j.value( "wm", std::string{} );
			}
		};

		skin_map_field skins{};
		agent_selection_field agents{};

		changer( )
		{
			config::detail::register_field( { .key = config::detail::make_key( "changer", "applied skins" ), .type = config::field_type::custom, .ptr = &skins, .count = 1 } );
			config::detail::register_field( { .key = config::detail::make_key( "changer", "agents" ), .type = config::field_type::custom, .ptr = &agents, .count = 1 } );
		}
	};

	struct misc
	{
		struct scoreboard_weapons
		{
			xui::setting enabled{ false, {}, "scoreboard weapons", "misc" };
			config::col color{ { 255, 255, 0, 255 }, "misc", "scoreboard weapons color" };
		} m_scoreboard_weapons{};

		struct chat_spammer
		{
			xui::setting enabled{ false, {}, "chat spammer", "chat spammer" };
			config::str message{ "kitty.wtf on top", "chat spammer", "message" };
			config::val<float> delay{ 3.0f, "chat spammer", "delay" };
			xui::setting on_timer{ true, {}, "on timer", "chat spammer" };
			xui::setting on_kill{ false, {}, "on kill", "chat spammer" };
			xui::setting on_death{ false, {}, "on death", "chat spammer" };
		} m_chat_spammer{};

		struct name_changer
		{
			xui::setting clantag{ false, {}, "clantag", "name changer" };
			xui::setting override_name{ false, {}, "override name", "name changer" };
			config::str name{ "Player", "name changer", "name" };
		} m_name_changer{};

		struct projectile_trajectory
		{
			xui::setting enabled{ true, {}, "projectile trajectory", "trajectory" };
			xui::setting straight_throw{ true, {}, "straight throw", "trajectory" };

			config::col held_color{ { 173, 192, 255, 255 }, "trajectory", "held color" };
			config::col thrown_color{ { 220, 225, 240, 255 }, "trajectory", "thrown color" };
			config::col will_deal_damage_held_color{ { 252, 217, 240, 255 }, "trajectory", "will damage held color" };
			config::col will_deal_damage_thrown_color{ { 252, 217, 240, 255 }, "trajectory", "will damage thrown color" };

			xui::setting glow{ true, {}, "glow", "trajectory" };
			config::val<float> glow_strength{ 1.0f, "trajectory", "glow strength" };
		} m_projectile_trajectory{};

		struct impacts
		{
			// Embedded sounds come after custom so every value a config could have
			// saved before stays valid.
			enum class sound_type : int { shop_click, home_click, bell, killcard, bullet_casing, coin_pickup, item_drop, popcan, key_press, custom, meow, meow2, rust_headshot, anime_ahh, sniper };
			enum class marker_type : int { classic, damage, both };
			enum class bullet_impact_type : int { overlay, sparks, both };
			enum class death_effect_type : int { default_fade, sparks };

			xui::setting hit_log{ true, {}, "hit logs", "impacts" };
			config::val<float> hit_log_duration{ 3.5f, "impacts", "hit log duration" };
			xui::setting console_log{ true, {}, "console logs", "impacts" };
			xui::setting chat_log{ false, {}, "chat logs", "impacts" };

			xui::setting miss_log{ true, {}, "miss logs", "impacts" };
			config::val<float> miss_log_duration{ 4.5f, "impacts", "miss log duration" };

			xui::setting hit_sound{ true, {}, "hit sound", "impacts" };
			config::enm<sound_type> hit_sound_type{ sound_type::rust_headshot, "impacts", "hit sound type" };
			config::val<float> hit_sound_volume{ 25.0f, "impacts", "hit sound volume" };
			config::str custom_hit_sound{ "hit.wav", "impacts", "custom hit sound" };

			xui::setting death_sound{ true, {}, "death sound", "impacts" };
			config::enm<sound_type> death_sound_type{ sound_type::anime_ahh, "impacts", "death sound type" };
			config::val<float> death_sound_volume{ 100.0f, "impacts", "death sound volume" };
			config::str custom_death_sound{ "kill.wav", "impacts", "custom death sound" };

			xui::setting hit_effect{ true, {}, "hit effect", "impacts" };
			config::col hit_effect_color{ { 173, 192, 255, 255 }, "impacts", "hit effect color" };
			config::val<float> hit_effect_duration{ 0.75f, "impacts", "hit effect duration" };
			config::val<float> hit_effect_strength{ 60.0f, "impacts", "hit effect strength" };

			xui::setting death_effect{ true, {}, "death effect", "impacts" };
			config::enm<death_effect_type> death_effect_type_select{ death_effect_type::default_fade, "impacts", "death effect type" };
			config::col death_effect_color{ { 173, 192, 255, 255 }, "impacts", "death effect color" };

			xui::setting bullet_impact_effect{ true, {}, "bullet impacts", "impacts" };
			config::enm<bullet_impact_type> bullet_impact_effect_type{ bullet_impact_type::overlay, "impacts", "bullet impact type" };
			config::col bullet_impact_effect_fill_color{ { 173, 192, 255, 85 }, "impacts", "bullet impact fill color" };
			config::col bullet_impact_effect_edge_color{ { 173, 192, 255, 255 }, "impacts", "bullet impact edge color" };
			config::col bullet_impact_effect_color_spark{ { 173, 192, 255, 255 }, "impacts", "bullet impact spark color" };
			config::val<float> bullet_impact_effect_duration{ 2.5f, "impacts", "bullet impact duration" };
			xui::setting bullet_impact_effect_glow{ true, {}, "glow", "bullet impacts" };
			config::val<float> bullet_impact_effect_glow_strength{ 1.0f, "bullet impacts", "glow strength" };

			xui::setting bullet_tracers{ false, {}, "bullet tracers", "impacts" };
			config::col bullet_tracer_color{ { 173, 192, 255, 255 }, "impacts", "bullet tracer color" };
			config::val<float> bullet_tracer_duration{ 0.5f, "impacts", "bullet tracer duration" };

			xui::setting hit_marker{ true, {}, "hit marker", "impacts" };
			config::enm<marker_type> hit_marker_type{ marker_type::classic, "impacts", "hit marker type" };
			config::val<float> hit_marker_duration{ 2.5f, "impacts", "hit marker duration" };
			config::col hit_marker_color{ { 255, 255, 255, 255 }, "impacts", "hit marker color" };
			xui::setting hit_marker_glow{ true, {}, "glow", "hit marker" };
			config::val<float> hit_marker_glow_strength{ 1.0f, "hit marker", "glow strength" };
			
			// Shot sound (plays when YOU fire a weapon)
			xui::setting shot_sound{ true, {}, "shot sound", "impacts" };
			config::enm<sound_type> shot_sound_type{ sound_type::sniper, "impacts", "shot sound type" };
			config::val<float> shot_sound_volume{ 25.0f, "impacts", "shot sound volume" };
			config::str custom_shot_sound{ "shot.wav", "impacts", "custom shot sound" };

			// The game's vanilla fire sound is left untouched: silencing it used to
			// hold the master `volume` convar at zero and could strand the game at
			// low volume, so that mechanism was removed.
		} m_impacts{};

		struct removals
		{
			xui::setting crosshair{ true, {}, "remove crosshair", "removals" };
			xui::setting scope{ true, {}, "remove scope", "removals" };
			xui::setting skybox_fog{ true, {}, "remove skybox fog", "removals" };
			xui::setting overhead{ true, {}, "remove overhead", "removals" };
			xui::setting legs{ true, {}, "remove legs", "removals" };
			xui::setting skybox_3d{ true, {}, "remove 3d skybox", "removals" };
			xui::setting recoil{ true, {}, "remove recoil", "removals" };
			xui::setting decals{ true, {}, "remove decals", "removals" };
			xui::setting smoke{ true, {}, "remove smoke", "removals" };
			config::val<float> flash_alpha{ 25.0f, "removals", "flash alpha" };
		} m_removals{};

		struct camera
		{
			xui::setting change_fov{ true, {}, "custom fov", "camera" };
			config::val<float> fov{ 115.0f, "camera", "fov" };

			xui::setting scoped_fov_override{ false, {}, "scoped fov override", "camera" };
			config::val<float> scoped_fov{ 40.0f, "camera", "scoped fov" };

			xui::setting thirdperson{ true, { VK_MBUTTON, xui::bind_mode::toggle }, "thirdperson", "camera" };
			config::val<float> thirdperson_distance{ 85.0f, "camera", "thirdperson distance" };
			config::val<float> thirdperson_hull_size{ 12.0f, "camera", "thirdperson hull size" };

			xui::setting change_aspect_ratio{ false, {}, "custom aspect ratio", "camera" };
			config::val<float> aspect_ratio{ 1.333f, "camera", "aspect ratio" };
		} m_camera{};

		struct viewmodel_adjust
		{
			xui::setting enabled{ false, {}, "viewmodel adjust", "viewmodel" };
			config::val<float> offset_x{ 0.0f, "viewmodel", "offset x" };
			config::val<float> offset_y{ 0.0f, "viewmodel", "offset y" };
			config::val<float> offset_z{ 0.0f, "viewmodel", "offset z" };
			config::val<float> fov{ 68.0f, "viewmodel", "viewmodel fov" };
			config::val<float> sway{ 1.0f, "viewmodel", "sway" };
			config::val<float> bob_lower{ 0.005f, "viewmodel", "bob lower" };
			config::val<float> bob_lat{ 0.1f, "viewmodel", "bob lat" };
			config::val<float> bob_vert{ 0.1f, "viewmodel", "bob vert" };
			config::val<float> bob_cycle{ 0.8f, "viewmodel", "bob cycle" };
			config::val<float> shift{ 0.0f, "viewmodel", "shift" };
			config::val<float> recoil_scale{ 1.0f, "viewmodel", "recoil scale" };
		} m_viewmodel_adjust{};

		struct hud
		{
			struct crosshair
			{
				xui::setting enabled{ true, {}, "crosshair overlay", "crosshair" };
				config::val<float> size{ 1.0f, "crosshair", "size" };
				config::val<float> outline{ 1.0f, "crosshair", "outline" };
				config::col color{ { 173, 192, 255, 255 }, "crosshair", "color" };
				config::col outline_color{ { 15, 15, 25, 200 }, "crosshair", "outline color" };
			} m_crosshair{};

			struct scope
			{
				xui::setting enabled{ true, {}, "scope overlay", "scope overlay" };
				config::val<float> line_length{ 125.0f, "scope overlay", "line length" };
				config::val<float> gap{ 8.0f, "scope overlay", "gap" };
				config::val<float> thickness{ 0.5f, "scope overlay", "thickness" };
				config::val<float> anim_speed{ 10.0f, "scope overlay", "anim speed" };
				config::col color{ { 173, 192, 255, 255 }, "scope overlay", "color" };
				xui::setting fade_in{ true, {}, "fade in", "scope overlay" };
				xui::setting fade_out{ false, {}, "fade out", "scope overlay" };

				xui::setting glow{ true, {}, "glow", "scope overlay" };
				config::val<float> glow_strength{ 1.0f, "scope overlay", "glow strength" };
			} m_scope{};

			struct hat
			{
				enum class hat_type : std::uint8_t { kasa, bucket };

				xui::setting enabled{ false, {}, "hat", "hat" };
				config::enm<hat_type> type{ hat_type::kasa, "hat", "type" };
				config::col color{ { 255, 171, 234, 160 }, "hat", "color" };
				config::col secondary_color{ { 173, 192, 255, 160 }, "hat", "secondary color" };
				xui::setting glow{ true, {}, "glow", "hat" };
				config::val<float> glow_strength{ 1.0f, "hat", "glow strength" };
			} m_hat{};

			struct velocity
			{
				xui::setting counter{ false, {}, "velocity counter", "velocity hud" };
				xui::setting chart{ false, {}, "velocity chart", "velocity hud" };
				config::col color{ { 173, 192, 255, 255 }, "velocity hud", "color" };
				config::val<float> bottom_offset{ 80.0f, "velocity hud", "bottom offset" };
				config::val<float> chart_width{ 200.0f, "velocity hud", "chart width" };
				config::val<float> chart_height{ 44.0f, "velocity hud", "chart height" };
			} m_velocity{};

			struct mascot
			{
				enum class mascot_style : std::uint8_t { kitty, boykisser2 };

				xui::setting enabled{ true, {}, "mascot", "mascot" };
				config::enm<mascot_style> style{ mascot_style::kitty, "mascot", "mascot style" };
			} m_mascot{};

		} m_hud{};

		struct post_process
		{
			struct chromatic_aberration
			{
				xui::setting enabled{ false, {}, "chromatic aberration", "post process" };
				config::val<float> intensity{ 0.003f, "post process", "chromatic aberration intensity" };
			} m_chromatic_aberration{};
		} m_post_process{};

		struct dlight
		{
			xui::setting enabled{ false, {}, "dynamic light", "misc" };
			config::col color{ { 255, 255, 255, 255 }, "dlight", "color" };
			config::val<float> radius{ 300.0f, "dlight", "radius" };
			config::val<float> z_offset{ 2.0f, "dlight", "z offset" };
		} m_dlight{};

		struct autobuy
		{
			xui::setting enabled{ true, {}, "auto buy", "autobuy" };
			config::val<int> primary_weapon{ 3, "autobuy", "primary weapon" };
			config::val<int> secondary_weapon{ 3, "autobuy", "secondary weapon" };
			xui::setting armor{ true, {}, "armor", "autobuy" };
			xui::setting defuser{ true, {}, "defuser", "autobuy" };
			xui::setting taser{ true, {}, "taser", "autobuy" };
			config::bools<5> grenades{ { true, true, true, false, false }, "autobuy", "grenades" };
		} m_autobuy{};

		xui::setting preserve_killfeed{ true, {}, "preserve killfeed", "misc" };
		xui::setting reveal_radar{ true, {}, "reveal radar", "misc" };
		xui::setting disable_game_logs{ true, {}, "disable game logs", "misc" };
		xui::setting auto_accept{ false, {}, "auto accept", "misc" };
		config::val<int> menu_key{ VK_DELETE, "misc", "menu key" };

		struct watermark_cfg
		{
			xui::setting enabled   { true, {}, "watermark",       "watermark" };
			xui::setting show_fps  { true, {}, "show fps",        "watermark" };
			xui::setting show_cpu  { true, {}, "show cpu",        "watermark" };
			xui::setting show_ping { true, {}, "show ping",       "watermark" };
			xui::setting show_name { true, {}, "show name",       "watermark" };
			xui::setting show_hs   { true, {}, "show hs%",        "watermark" };
			xui::setting show_kdr  { true, {}, "show kdr",        "watermark" };
			xui::setting show_user { false, {}, "show user",      "watermark" };
		} m_watermark{};

		struct widgets_cfg
		{
			enum class style : std::uint8_t { modern, classic, neo, glass };

			config::enm<style> widget_style{ style::modern, "widgets", "style" };

			struct glass_cfg
			{
				config::col text_color{ { 235, 238, 248, 255 }, "glass widget", "text color" };
				config::col icon_color{ { 173, 192, 255, 255 }, "glass widget", "icon color" };
				xui::setting per_stat_icon_colors{ false, {}, "per stat icon colors", "glass widget" };
				config::col logo_icon_color{ { 173, 192, 255, 255 }, "glass widget", "logo icon color" };
				config::col fps_icon_color{ { 173, 192, 255, 255 }, "glass widget", "fps icon color" };
				config::col ping_icon_color{ { 173, 192, 255, 255 }, "glass widget", "ping icon color" };
				config::col time_icon_color{ { 173, 192, 255, 255 }, "glass widget", "time icon color" };
				config::col vel_icon_color{ { 173, 192, 255, 255 }, "glass widget", "velocity icon color" };
				config::col warn_text_color{ { 255, 92, 92, 255 }, "glass widget", "warn text color" };
				config::col warn_icon_color{ { 255, 92, 92, 255 }, "glass widget", "warn icon color" };
				config::val<int> ping_warn_threshold{ 80, "glass widget", "ping warn threshold" };
				config::col bg_color{ { 12, 14, 20, 155 }, "glass widget", "background color" };
				config::col shadow_color{ { 0, 0, 0, 255 }, "glass widget", "shadow color" };
				config::col avatar_ring_color{ { 255, 255, 255, 40 }, "glass widget", "avatar ring color" };
				config::val<float> blur_strength{ 1.0f, "glass widget", "blur strength" };
				config::val<float> shadow_strength{ 1.4f, "glass widget", "shadow strength" };
				config::val<float> shadow_spread{ 1.2f, "glass widget", "shadow spread" };
				config::val<float> icon_size{ 15.0f, "glass widget", "icon size" };
				config::val<float> pill_height{ 32.0f, "glass widget", "pill height" };
				config::val<float> section_gap{ 16.0f, "glass widget", "section gap" };
				config::val<float> pad_x{ 14.0f, "glass widget", "padding x" };
				xui::setting show_avatar{ true, {}, "show avatar", "glass widget" };
			} m_glass{};
		} m_widgets{};
	};

	struct movement
	{
		xui::setting bhop{ true, {}, "bhop", "movement" };
		xui::setting airstrafe{ true, {}, "airstrafe", "movement" };
		xui::setting airstrafe_fully_directional{ true, {}, "fully directional", "movement - airstrafe" };
		xui::setting jumpbug{ true, {}, "jumpbug", "movement" };
		xui::setting fastladder{ true, {}, "fastladder", "movement" };
		xui::setting slowwalk{ false, { 'P', xui::bind_mode::hold_on}, "slowwalk", "movement" };
		config::val<float> slowwalk_speed{ 33.0f, "movement", "slowwalk speed" };
		xui::setting air_duck{ false, {}, "air duck", "movement" }; // auto-crouch when falling off edges

		struct test_strafer
		{
			xui::setting enabled{ false, {}, "test strafer", "movement" };
		} m_test_strafer{};

		struct subtick_strafe
		{
			xui::setting enabled{ false, {}, "subtick strafe", "movement" };
		} m_subtick_strafe{};

		struct velocity_debug
		{
			xui::setting enabled{ false, {}, "velocity debug", "movement" };
			xui::setting reset_on_land{ true, {}, "reset peak on land", "movement - velocity debug" };
		} m_velocity_debug{};
	};

	struct world
	{
		struct weather
		{
			enum class weather_type : std::uint8_t { snow, rain, stars };

			xui::setting enabled{ true, {}, "weather", "weather" };
			config::enm<weather_type> type{ weather_type::snow, "weather", "type" };
			config::col color{ { 117, 120, 142, 144 }, "weather", "color" };
			config::val<float> particle_amount{ 1.0f, "weather", "particle amount" };
			config::val<float> fall_speed{ 1.0f, "weather", "fall speed" };

			xui::setting fog_enabled{ true, {}, "fog", "weather" };
			config::val<float> fog_density{ 0.5f, "weather", "fog density" };
			config::val<float> fog_anisotropy{ 0.5f, "weather", "fog anisotropy" };
			config::val<float> fog_draw_distance{ 8000.0f, "weather", "fog draw distance" };
			config::col fog_color{ { 160, 175, 210, 255 }, "weather", "fog color" };

			xui::setting wetness{ false, {}, "wetness", "weather" };
			config::val<float> wetness_density{ 1.8f, "weather", "wetness density" };
			config::val<float> wetness_speed{ 0.8f, "weather", "wetness speed" };

			xui::setting wind{ true, {}, "wind", "weather" };
			config::val<float> wind_strength{ 3.0f, "weather", "wind strength" };
			config::val<float> wind_direction{ 0.0f, "weather", "wind direction" };
			config::val<float> wind_turbulence{ 1.0f, "weather", "wind turbulence" };
		} m_weather{};

		struct scene
		{
			struct skyboxing
			{
				xui::setting custom_skybox{ true, {}, "skybox material", "scene" };
				config::val<int> selected_skybox{ 0, "scene", "selected skybox" };

				xui::setting custom_color{ true, {}, "skybox color", "scene" };
				config::col skybox_color{ { 249, 103, 206, 255 }, "scene", "skybox color value" };
				config::col cloud_color{ { 173, 192, 255, 0 }, "scene", "cloud color" };
				config::col sun_color{ { 173, 192, 255, 0 }, "scene", "sun color" };
			};

			skyboxing skybox{};

			xui::setting lighting{ true, {}, "lighting", "scene" };
			config::col lighting_color{ { 173, 192, 255, 255 }, "scene", "lighting color" };
			config::val<float> lighting_intensity{ 0.85f, "scene", "lighting intensity" };
			config::vec3 lighting_rotation{ { -0.9f, 0.3f, 0.2f }, "scene", "lighting rotation" };

			xui::setting world_setting{ true, {}, "world color", "scene" };
			config::col world_color{ { 115, 125, 160, 255 }, "scene", "world color value" };
			

			config::col penetratable_color{ { 173, 255, 192, 120 }, "scene", "penetratable color" };
			xui::setting penetratable_glow{ true, {}, "penetratable glow", "scene" };

			xui::setting bloom{ true, {}, "bloom", "scene" };
			config::val<float> bloom_value{ 2.0f, "scene", "bloom value" };

			xui::setting gamma{ true, {}, "gamma", "scene" };
			config::val<float> gamma_value{ 2.2f, "scene", "gamma value" };

			xui::setting dof{ true, {}, "depth of field", "scene" };
			config::val<float> dof_near_blurry{ 0.0f, "scene", "dof near blurry" };
			config::val<float> dof_near_crisp{ 5.0f, "scene", "dof near crisp" };
			config::val<float> dof_far_crisp{ 600.0f, "scene", "dof far crisp" };
			config::val<float> dof_far_blurry{ 1400.0f, "scene", "dof far blurry" };

			xui::setting ambient{ true, {}, "ambient", "scene" };
			config::col ambient_color{ { 233, 145, 255, 255 }, "scene", "ambient color" };
			config::val<float> ambient_intensity{ 1.1f, "scene", "ambient intensity" };
		} m_scene{};
	};

	struct menu_theme
	{
		config::col accent_color{ { 255, 255, 255, 255 }, "menu theme", "accent color" };  // White default

		// How see-through the menu chrome is. 0 = fully solid, 100 = invisible, so the
		// slider now means what it says -- it used to be fed straight into the alpha
		// as an opacity, which made "0%" an unusable fully transparent menu.
		config::val<float> transparency{ 85.0f, "menu theme", "transparency" };

		enum class watermark_pos : std::uint8_t
		{
			top_left, top_middle, top_right, bottom_left, bottom_middle, bottom_right
		};
		config::enm<watermark_pos> watermark_position{ watermark_pos::top_left, "menu theme", "watermark position" };

		// HUD element roundness settings (lower = more boxy, higher = more round)
		config::val<float> keybinds_roundness{ 5.0f, "menu theme", "keybinds roundness" };
		config::val<float> spectators_roundness{ 5.0f, "menu theme", "spectators roundness" };
		config::val<float> watermark_roundness{ 5.0f, "menu theme", "watermark roundness" };
		config::val<float> hitlogs_roundness{ 5.0f, "menu theme", "hitlogs roundness" };

		// HUD element colors
		config::col keybinds_color{ { 255, 255, 255, 120 }, "menu theme", "keybinds color" };
		config::col spectators_color{ { 255, 255, 255, 120 }, "menu theme", "spectators color" };
		config::col watermark_hud_color{ { 255, 255, 255, 120 }, "menu theme", "watermark color" };
		config::col hitlogs_color{ { 255, 255, 255, 120 }, "menu theme", "hitlogs color" };

		// Per-element opacity, 0 = invisible and 100 = fully drawn. This is applied on
		// top of the colour's own alpha so both can be tuned without fighting each
		// other -- the old single alpha baked into the colour made it impossible to
		// lighten a panel without also lightening its text.
		config::val<float> keybinds_opacity{ 19.0f, "menu theme", "keybinds opacity" };
		config::val<float> spectators_opacity{ 19.0f, "menu theme", "spectators opacity" };
		config::val<float> watermark_opacity{ 19.0f, "menu theme", "watermark opacity" };
		config::val<float> hitlogs_opacity{ 19.0f, "menu theme", "hitlogs opacity" };

		// Sidebar chrome. Separate colour + opacity so it can be tuned independently
		// of the card / window background.
		config::col sidebar_color{ { 0, 0, 0, 255 }, "menu theme", "sidebar color" };
		config::val<float> sidebar_transparency{ 100.0f, "menu theme", "sidebar transparency" };
	};

	inline combat g_combat{};
	inline esp g_esp{};
	inline changer g_changer{};
	inline misc g_misc{};
	inline movement g_movement{};
	inline world g_world{};
	inline menu_theme g_menu_theme{};

	inline void finalize_binds( )
	{
		auto& aa = g_combat.m_antiaim;

		// Movement correction must stay enabled: anti-aim rewrites the view angles,
		// and without the counter-rotation every strafe keys off the wrong basis.
		// Older configs saved it disabled, so re-enable it whenever settings are
		// re-applied (load / import / reset).
		aa.movement_correction.value = true;

		aa.manual_left.bind.excludes = &aa.manual_right;
		aa.manual_right.bind.excludes = &aa.manual_left;

		if ( aa.manual_left.value && aa.manual_right.value )
		{
			aa.manual_right.value = false;
			aa.manual_right.bind.active = false;
		}
	}

} // namespace settings
