#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>

namespace features::changer {

	namespace detail {

		// Hashing the class name of a scene-node owner is the single most dangerous
		// thing the weapon changers do. The owner pointers come out of a scene graph
		// the engine rebuilds while we are looking at it, and entities::get_schema_name
		// only fault-checks the pointer load -- it hands back whatever string pointer
		// it finds, which fnv1a::runtime_hash would then walk until it hit a NUL. One
		// stale node was enough to send that walk into unmapped memory and crash the
		// game on the frame the view model was replaced (switching a skinned knife).
		//
		// This variant bounds the read and fault-checks every byte, so a stale owner
		// can only ever produce a wrong hash, never a bad read.
		[[nodiscard]] inline std::uint32_t hash_class_name( std::uintptr_t entity )
		{
			if ( !entity )
			{
				return 0;
			}

			const auto identity = memory::safe_read<std::uintptr_t>( entity + 0x10 ).value_or( 0 );
			if ( !identity )
			{
				return 0;
			}

			const auto entity_class = memory::safe_read<std::uintptr_t>( identity + 0x8 ).value_or( 0 );
			if ( !entity_class )
			{
				return 0;
			}

			// CEntityClass owns the current SchemaClassInfo pointer at 0x58.
			const auto class_info = memory::safe_read<std::uintptr_t>( entity_class + 0x58 ).value_or( 0 );
			if ( !class_info )
			{
				return 0;
			}

			const auto name_ptr = memory::safe_read<std::uintptr_t>( class_info + 0x8 ).value_or( 0 );
			if ( !name_ptr )
			{
				return 0;
			}

			constexpr std::uint32_t k_max_name{ 64 };

			std::uint32_t hash{ 2166136261u };
			for ( std::uint32_t i = 0; i < k_max_name; ++i )
			{
				const auto c = memory::safe_read<char>( name_ptr + i );
				if ( !c || *c == '\0' )
				{
					break;
				}

				hash ^= static_cast< std::uint32_t >( static_cast< unsigned char >( *c ) );
				hash *= 16777619u;
			}

			return hash;
		}

		// Walks the arms' scene-node children looking for the first-person weapon.
		// Every hop is fault-checked and the walk is bounded, so a half-rebuilt scene
		// graph can only make it give up, never dereference a dangling node.
		[[nodiscard]] inline std::uintptr_t find_hud_model_weapon( std::uintptr_t pawn )
		{
			constexpr std::uint32_t k_max_nodes{ 64 };

			const auto arms_handle = memory::safe_read<std::uint32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_hHudModelArms"_hash ) ).value_or( 0 );
			if ( !arms_handle )
			{
				return 0;
			}

			const auto arms = systems::g_entities.lookup( arms_handle );
			if ( !arms )
			{
				return 0;
			}

			const auto arms_scene_node = memory::safe_read<std::uintptr_t>( arms + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
			if ( !arms_scene_node )
			{
				return 0;
			}

			auto child = memory::safe_read<std::uintptr_t>( arms_scene_node + SCHEMA( "CGameSceneNode", "m_pChild"_hash ) ).value_or( 0 );

			for ( std::uint32_t visited = 0; child && visited < k_max_nodes; ++visited )
			{
				const auto owner = memory::safe_read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pOwner"_hash ) ).value_or( 0 );
				if ( owner && hash_class_name( owner ) == "C_CS2HudModelWeapon"_hash )
				{
					return owner;
				}

				child = memory::safe_read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pNextSibling"_hash ) ).value_or( 0 );
			}

			return 0;
		}

	} // namespace detail

	class econ_item_system
	{
	public:
		enum class item_category : std::uint8_t
		{
			gun,
			knife,
			glove,
			agent,
			other
		};

		struct paint_kit
		{
			int id{};
			std::string name{};
			std::string desc_token{};
			std::string name_token{};
			std::string localized_name{};
			float wear_min{};
			float wear_max{};
			bool legacy_model{};
			std::uint8_t rarity{};
		};

		struct item_def
		{
			std::int16_t def_index{};
			std::string item_class{};
			std::string name{};
			std::string localized_name{};
			std::string model_player{};
			std::string image_inventory{};
			int loadout_slot{};
			std::uint32_t used_by_classes{};
			item_category category{};
			std::uint8_t rarity{};

			[[nodiscard]] int team( ) const
			{
				if ( ( this->used_by_classes & 0xc ) == 0xc )
				{
					return 0;
				}

				if ( this->used_by_classes & 4 )
				{
					return 2;
				}

				if ( this->used_by_classes & 8 )
				{
					return 3;
				}

				return 0;
			}
		};

		struct skin_entry
		{
			std::int16_t def_index{};
			int paint_kit_id{};
		};

		struct skin_image
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
			int width{};
			int height{};
		};

		[[nodiscard]] bool initialize( );

		[[nodiscard]] const std::vector<paint_kit>& paint_kits( ) const { return this->m_paint_kits; }
		[[nodiscard]] const std::vector<item_def>& item_defs( ) const { return this->m_item_defs; }

		[[nodiscard]] const std::vector<const item_def*>& knives( ) const { return this->m_knives; }
		[[nodiscard]] const std::vector<const item_def*>& gloves( ) const { return this->m_gloves; }
		[[nodiscard]] const std::vector<const item_def*>& agents( ) const { return this->m_agents; }
		[[nodiscard]] const std::vector<const item_def*>& guns( ) const { return this->m_guns; }
		[[nodiscard]] const std::vector<skin_entry>& skins( ) const { return this->m_skins; }

		[[nodiscard]] const item_def* find_def( std::int16_t def_index ) const;
		[[nodiscard]] const paint_kit* find_paint_kit( int id ) const;
		[[nodiscard]] const skin_image* get_skin_image( const std::string& image_inventory );
		[[nodiscard]] const skin_image* get_skin_image( std::int16_t def_index, int paint_kit_id );

		[[nodiscard]] int combined_rarity( std::int16_t def_index, int paint_kit_id ) const;

		void flush_skin_images( );

	private:
		enum class image_state : std::uint8_t
		{
			idle,
			loading,
			decoded,
			ready,
			failed
		};

		struct image_entry
		{
			skin_image image{};
			std::atomic<image_state> state{ image_state::idle };
			std::vector<std::vector<std::uint8_t>> mip_buffers{};
			std::uint32_t width{};
			std::uint32_t height{};
			DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
		};

		bool parse_item_defs( std::uintptr_t schema );
		bool parse_paint_kits( std::uintptr_t schema );
		void build_indices( );
		void resolve_localized_names( );
		bool build_vpk_index( );
		void build_skin_index( );

		void request_decode( const std::string& image_inventory );
		bool finalize_texture( image_entry& entry );

		[[nodiscard]] item_category classify( const char* item_class, int loadout_slot );
		[[nodiscard]] std::vector<std::byte> read_vpk( const std::string& path );
		[[nodiscard]] bool decode_vtex( std::span<const std::byte> data, image_entry& out );
		[[nodiscard]] std::string build_skin_image_path( const item_def* def, const paint_kit* pk ) const;

		std::vector<paint_kit> m_paint_kits{};
		std::vector<item_def> m_item_defs{};

		std::vector<const item_def*> m_knives{};
		std::vector<const item_def*> m_gloves{};
		std::vector<const item_def*> m_agents{};
		std::vector<const item_def*> m_guns{};
		std::vector<skin_entry> m_skins{};

		std::unordered_map<std::int16_t, std::size_t> m_def_index_map{};
		std::unordered_map<int, std::size_t> m_paint_kit_map{};

		struct vpk_file_entry
		{
			std::uint16_t archive_index{};
			std::uint32_t offset{};
			std::uint32_t length{};
		};

		std::unordered_map<std::string, vpk_file_entry> m_vpk_index{};
		bool m_vpk_indexed{};
		std::filesystem::path m_vpk_directory{};

		std::unordered_map<std::string, std::unique_ptr<image_entry>> m_image_cache{};
		std::mutex m_image_mutex{};

		std::unordered_map<std::uint16_t, std::ifstream> m_archive_handles{};
		std::mutex m_vpk_mutex{};
	};

	// Catalog of the third-party player models that ship with the cheat. They are not
	// part of CS2's item schema, so a model is applied by overwriting the local
	// pawn's model outright. Models are listed whether or not their files are on
	// disk, so the menu can show a picture and say what the player is missing.
	class custom_models
	{
	public:
		struct model
		{
			std::string id{};            // stable key stored in configs
			std::string name{};          // display name, e.g. "Ghostface"
			std::string source{};        // where the model comes from, e.g. "Scream"
			std::string model_path{};    // in-game model path handed to the engine
			std::string preview{};       // preview image path (may not be shipped)
			// A weapon model (the knife) rather than a player model. It is listed on
			// the same page, but it is written to the first-person weapon entity
			// instead of the pawn, so the menu has to route it somewhere else.
			bool weapon{ false };
			// False only when the compiled model was positively not found in the game
			// files. An uninstalled model is never applied: the engine cannot resolve a
			// path that is not there and draws its error model instead. Stays true when
			// the game directory itself is unknown.
			bool installed{ true };
		};

		struct preview_image
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
			int width{};
			int height{};
		};

		// Builds the catalog and marks which models are installed. Safe to call once
		// from any thread during startup.
		bool initialize( );

		[[nodiscard]] const std::vector<model>& models( ) const { return this->m_models; }
		[[nodiscard]] const model* find( const std::string& id ) const;

		// True when a model the catalog knows about was positively not found in the
		// game files. A path the catalog never saw is trusted: it may have been
		// installed by hand, and refusing it would block a working model.
		[[nodiscard]] bool model_missing( const std::string& model_path ) const;

		// Render thread only. Uploads the preview texture on first use; returns null
		// until it is ready (or when the asset is missing) so cards can fall back.
		[[nodiscard]] const preview_image* get_preview( const std::string& asset_path );

		// Registers a model path with the engine's resource system before anything
		// asks for it. SetModel resolves its path through that system, and a model
		// that was never registered there resolves to nothing -- the engine then
		// substitutes its error model. The registration lives on the level, not on
		// the session, so the call is made on every apply rather than once: a map
		// change drops the engine's precache list and every path has to be handed
		// over again.
		static bool precache_model( std::string_view model_path );

		// True when the resource system already holds a resource for the path, so a
		// write can go out without the engine falling back to its error model. Read
		// only to decide whether an apply can be brought forward: a probe that cannot
		// answer reports ready, so nothing ever waits on it.
		[[nodiscard]] static bool model_ready( std::string_view model_path );

		// True when the entity's currently loaded model resource really is
		// `model_path`. Reads the name the engine resolved into the resource, so the
		// error model can never be mistaken for a successful apply.
		[[nodiscard]] static bool model_matches( std::uintptr_t entity, std::string_view model_path );

	private:
		void build( );
		[[nodiscard]] std::filesystem::path assets_root( );

		std::vector<model> m_models{};

		std::unordered_map<std::string, std::unique_ptr<preview_image>> m_previews{};
		std::mutex m_preview_mutex{};

		std::filesystem::path m_assets_root{};
		bool m_initialized{ false };
	};

	class agents
	{
	public:
		void on_frame_stage_notify( );

		// Drops the cached apply state so a model picked in the menu is written on
		// the next frame instead of waiting for a team or weapon change.
		void force_invalidate( )
		{
			this->m_overridden = false;
			this->m_applied_handle = 0;
			this->m_applied_def = 0;
			this->m_applied_custom.clear( );
			this->m_failed_custom.clear( );
			this->m_custom_pending = false;
			this->m_custom_requests = 0;
			this->m_custom_last_request = {};
			// A freshly picked model gets the settle pass again: the one frame the
			// engine is asked to rebuild the pawn's mesh groups on.
			this->m_custom_settled = false;
		}

	private:
		void cycle_weapon_owners( std::uintptr_t pawn );

		std::string m_original_model{};
		std::uintptr_t m_tracked_pawn{};
		std::uintptr_t m_applied_handle{};
		std::int16_t m_applied_def{};
		// Model path currently written to the pawn, so a switch between a schema
		// agent and a custom agent is detected even when the definition is unchanged.
		std::string m_applied_custom{};
		// A custom agent model that the engine could not resolve is remembered by
		// path, so the pawn falls back to the schema agent instead of being left
		// showing the error model, and so a later attempt does not repeat the wait.
		std::string m_failed_custom{};
		bool m_custom_pending{};
		std::chrono::steady_clock::time_point m_custom_started{};
		// How many times the pending path has been written and when the last write
		// happened. Bounded so a model that never resolves cannot be re-written every
		// frame, which is what raced the resource system and left the error model up.
		int m_custom_requests{};
		std::chrono::steady_clock::time_point m_custom_last_request{};
		// Set once the engine has been told to rebuild the pawn's mesh groups for a
		// model it resolved. The rebuild is what makes a custom agent survive the
		// game's own cosmetic refresh instead of being replaced by the error model.
		bool m_custom_settled{};
		bool m_overridden{};
		int m_tracked_team{};
	};

	class gloves
	{
	public:
		void on_frame_stage_notify( );

	private:
		struct original_state
		{
			std::uint16_t def_index{};
			std::uint64_t item_id{};
			std::uint32_t id_high{};
			std::uint32_t id_low{};
			std::uint32_t account_id{};
			bool restore_custom_material{};
			bool initialized{};
			bool disallow_soc{};
			bool captured{};
		};

		struct attribute_state
		{
			float value{};
			bool present{};
		};

		[[nodiscard]] bool capture_original( std::uintptr_t item_view );
		[[nodiscard]] bool read_paint_attributes( std::uintptr_t item_view, std::array<attribute_state, 3>& attributes ) const;
		[[nodiscard]] bool restore_paint_attributes( std::uintptr_t item_view ) const;
		[[nodiscard]] bool paint_attributes_match( std::uintptr_t item_view, const settings::changer::applied_skin& skin ) const;
		void apply( std::uintptr_t pawn, std::uintptr_t item_view, int team, const econ_item_system::item_def& def, const settings::changer::applied_skin& skin, std::uint32_t account_id );
		void restore( std::uintptr_t pawn, std::uintptr_t item_view, int team );
		void refresh( std::uintptr_t pawn, std::uintptr_t item_view, int team ) const;
		void reset( );

		original_state m_original{};
		std::array<attribute_state, 3> m_original_attributes{};
		std::uintptr_t m_tracked_pawn{};
		bool m_overridden{};
	};

	class guns
	{
	public:
		void on_frame_stage_notify( );
		
		// Force invalidate cache to reapply skins immediately
		void force_invalidate_cache( )
		{
			this->m_applied_weapons.clear( );
		}

	private:
		struct applied_weapon
		{
			int paint_kit{};
		};
		void apply( std::uintptr_t weapon, std::uintptr_t iv, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const settings::changer::applied_skin* skin, std::uint32_t account_id );
		void rebuild_paint( std::uintptr_t weapon, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const econ_item_system::paint_kit* pk );
		void update_view_model( std::uintptr_t pawn, const econ_item_system::paint_kit* pk );
		void clear_hud_icon( std::uintptr_t iv );
		void schedule_hud_clear( std::uintptr_t iv );
		void process_hud_clear( );

		std::uint32_t m_last_active_handle{};
		std::uintptr_t m_tracked_pawn{};
		std::unordered_map<std::uint32_t, applied_weapon> m_applied_weapons{};
		std::uintptr_t m_pending_hud_iv{};
		std::chrono::steady_clock::time_point m_hud_clear_time{};
	};

	class knives
	{
	public:
		void on_frame_stage_notify( );
		
		// Force invalidate to reapply skin immediately
		void force_invalidate( )
		{
			this->m_original = {};
			this->m_overridden = false;
			this->m_pending_paint_retry = false;
			this->m_pending_paint_weapon = 0;
			this->m_last_model_change = {};
			this->m_custom_pending = false;
			this->m_custom_requests = 0;
			this->m_custom_last_request = {};
			this->m_custom_failed_path.clear( );
		}

	private:
		struct original_state
		{
			std::uint16_t def_index{};
			std::uint32_t id_high{};
			std::uint32_t id_low{};
			std::uint32_t account_id{};
			bool initialized{};
			int paint_kit{};
			int seed{};
			float wear{};
			int stattrak{};
			bool captured{};
		};

		void capture_original( std::uintptr_t weapon, std::uintptr_t iv );
		void apply( std::uintptr_t weapon, std::uintptr_t iv, const econ_item_system::item_def* def, const settings::changer::applied_skin* skin, std::uint32_t account_id, std::uintptr_t active_weapon, std::uintptr_t pawn, bool change_model );
		void restore( std::uintptr_t weapon, std::uintptr_t iv, std::uintptr_t active_weapon, std::uintptr_t pawn );
		void update_model( std::uintptr_t weapon, std::uintptr_t iv, std::uint16_t def_index );
		void update_view_model( std::uintptr_t pawn, const econ_item_system::paint_kit* pk );
		void apply_custom_model( std::uintptr_t pawn, std::uintptr_t active_weapon );
		void rebuild_paint( std::uintptr_t weapon, std::uintptr_t active_weapon, std::uintptr_t pawn, const econ_item_system::paint_kit* pk );
		void clear_hud_icon( std::uintptr_t iv );
		void schedule_hud_clear( std::uintptr_t iv );
		void process_hud_clear( );

		original_state m_original{};
		std::uint32_t m_last_active_handle{};
		std::uintptr_t m_tracked_pawn{};
		// The entity the captured original belongs to. A different knife means the
		// snapshot is stale and must not be restored onto it.
		std::uintptr_t m_tracked_weapon{};
		bool m_overridden{};
		std::uintptr_t m_pending_hud_iv{};
		std::chrono::steady_clock::time_point m_hud_clear_time{};
		std::chrono::steady_clock::time_point m_last_model_change{};
		// When the composite material was null during rebuild_paint (round start /
		// first spawn), retry the paint-only pass on subsequent frames until it
		// becomes available, so the skin shows without needing a team switch.
		bool m_pending_paint_retry{};
		std::uintptr_t m_pending_paint_weapon{};

		// Custom knife model state. The first-person weapon entity renders whatever
		// is held, so the write is bounded the same way the agent path bounds its
		// own: a few requests a second apart, then give up rather than keep
		// re-pointing an entity the resource system is still streaming into.
		bool m_custom_pending{};
		int m_custom_requests{};
		std::chrono::steady_clock::time_point m_custom_started{};
		std::chrono::steady_clock::time_point m_custom_last_request{};
		// Set when a custom model never resolved, so it is not retried every frame
		// while the player keeps a broken selection configured.
		std::string m_custom_failed_path{};
	};

} // namespace features::changer
