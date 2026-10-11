#include <pch/pch.hpp>
#include <filesystem>
#include <utilities/game_path.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/features.hpp>
#include "preview_data.hpp"

namespace features::changer {

	namespace {

		// The directory the running module was loaded from. Used only to locate the
		// bundled preview images, which ship beside the cheat rather than inside it.
		[[nodiscard]] std::filesystem::path module_directory( )
		{
			HMODULE module{ nullptr };
			if ( !GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast< LPCWSTR >( &module_directory ), &module ) )
			{
				return {};
			}

			wchar_t buffer[ 32768 ]{};
			const auto length = GetModuleFileNameW( module, buffer, 32768 );
			return ( length > 0 && length < 32768 )
				? std::filesystem::path( buffer, buffer + length ).parent_path( )
				: std::filesystem::path{};
		}

		[[nodiscard]] std::string lowercase( std::string value )
		{
			for ( auto& c : value )
			{
				if ( c >= 'A' && c <= 'Z' )
				{
					c = static_cast< char >( c + 32 );
				}
			}

			return value;
		}

		struct model_seed
		{
			const char* id;
			const char* name;
			const char* source;
			const char* model_path;
			const char* preview;
			// A weapon model is written to the first-person weapon instead of the pawn.
			bool weapon;
		};

		// The model paths are the ones compiled into the .vmdl_c files themselves: a
		// model only loads when its file sits at the path it was built with, and its
		// materials are resolved relative to the same folder. Adding another one is a
		// matter of dropping its folder into custom_content at the path it was built
		// for and adding a single row here.
		//
		// The preview name is the key the generator embeds a picture under: a
		// `preview.png` inside the model's own folder (next to the .vmdl_c) is compiled
		// into the dll under "custom_models/<folder>.png", which is what lets people
		// who only have Kitty.dll see the tile.
		//
		// Three roots are shipped because the packs were compiled against different
		// ones: agent models live under the game's agents folder, player models under
		// characters (where the game keeps its own), and a weapon model under weapons.
		// The path is what decides where the installer has to put the folder and where
		// the engine will look for it, so it is copied from the compiled files rather
		// than chosen here. A row with weapon = true is written to the first-person
		// weapon instead of the pawn.
		constexpr model_seed k_model_seeds[]
		{
			{ "ghostface",    "Ghostface",      "Scream",          "agents/models/otakku/ghost_face/ghost_face.vmdl",             "custom_models/ghost_face.png",     false },
			{ "garnet",       "Garnet",         "Steven Universe", "agents/models/otakku/garnet_su/garnet_su.vmdl",                "custom_models/garnet_su.png",      false },
			{ "goku",         "Son Goku",       "Dragon Ball",     "agents/models/otakku/son_goku/son_goku.vmdl",                  "custom_models/son_goku.png",       false },
			{ "ravenpool",    "Ravenpool",      "Comics",          "agents/models/otakku/ravenpool/ravenpool.vmdl",                "custom_models/ravenpool.png",      false },
			{ "pumpkin",      "Pumpkin Knight", "Halloween",       "agents/models/otakku/pumpkin_knight/pumpkin_knight.vmdl",      "custom_models/pumpkin_knight.png", false },
			{ "sakura",       "Sakura Haruno",  "Naruto",          "agents/models/otakku/sakura_haruno/sakura_haruno.vmdl",        "custom_models/sakura_haruno.png",  false },
			{ "gman",         "G-Man",          "Half-Life",       "agents/models/otakku/gman/gman.vmdl",                          "custom_models/gman.png",           false },
			{ "homer",        "Homer Simpson",  "The Simpsons",    "agents/models/otakku/homer_simpson/homer_simpson.vmdl",        "custom_models/homer_simpson.png",  false },

			{ "masterchief",  "Master Chief",   "Halo",            "characters/models/kolka/master_chief/master_chief.vmdl",      "custom_models/master_chief.png",   false },
			{ "shinano",      "Shinano",        "Community",       "characters/models/shinano/shinano.vmdl",                      "custom_models/shinano.png",        false },
			{ "shinanovrc",   "Shinano VRC",    "Community",       "characters/models/shinano_vrc/shinano_vrc.vmdl",              "custom_models/shinano_vrc.png",    false },
			{ "shinki",       "Shinki",         "Community",       "characters/models/shinki/shinki.vmdl",                        "custom_models/shinki.png",         false },
			{ "miko",         "Miko",           "Community",       "characters/models/shiroi/miko.vmdl",                          "custom_models/miko.png",           false },
			{ "shiroi",       "Shiroi",         "Community",       "characters/models/shiroi/shiroi.vmdl",                        "custom_models/shiroi.png",         false },
			{ "shiroko",      "Shiroko",        "Community",       "characters/models/shiroko/shiroko.vmdl",                      "custom_models/shiroko.png",        false },
			{ "metatron",     "Metatron",       "Community",       "characters/models/stardust/metatron.vmdl",                    "custom_models/metatron.png",       false },
			{ "stardust",     "Stardust",       "Community",       "characters/models/stardust/stardust.vmdl",                    "custom_models/stardust.png",       false },
			{ "shrek",        "Shrek",          "Shrek",           "characters/models/stepanof/stalker_shrek/shrek.vmdl",         "custom_models/shrek.png",          false },
			{ "sunflower",    "Sunflower Fairy", "Community",      "characters/models/sunflower_fairy/sunflower_fairy.vmdl",      "custom_models/sunflower_fairy.png", false },
			{ "suomi",        "Suomi",          "Community",       "characters/models/suomi/suomi.vmdl",                          "custom_models/suomi.png",          false },
			{ "temimi",       "Temimi",         "Community",       "characters/models/temimi/temimi.vmdl",                        "custom_models/temimi.png",         false },
			{ "tiangongxin",  "Tiangongxin",    "Community",       "characters/models/tiangongxin/tiangongxin.vmdl",              "custom_models/tiangongxin.png",    false },
			{ "junko",        "Konno Junko",    "Community",       "characters/models/tianli/konno_junko/konno_junko.vmdl",       "custom_models/konno_junko.png",    false },
			{ "tiantong",     "Tiantong",       "Community",       "characters/models/tiantong/tiantong.vmdl",                    "custom_models/tiantong.png",       false },
			{ "jacksparrow",  "Jack Sparrow",   "Pirates",         "characters/wcsnik/fortnite/jack_sparrow/jack_sparrow.vmdl",   "custom_models/jack_sparrow.png",   false },
			{ "violinist",    "10e Violinist",  "Community",       "characters/models/nozb1/10e_violinist_player_model/10e_violinist_player_model.vmdl", "custom_models/10e_violinist.png", false },
			{ "kancolle",     "Akatsuki Class", "Kancolle",        "characters/models/toppi/kancolle/kancolle_akatsuki_class.vmdl", "custom_models/kancolle.png",     false },

			// Weapon models sit on the same page but are written to the first-person
			// weapon entity, and only while the player is actually holding a knife:
			// the same entity renders every weapon, so writing it blindly is what used
			// to leave a gun looking like a knife.
			{ "roseknife",    "Blooming Rose",  "Knife model",     "weapons/nozb1/knife/blooming_rose/blooming_rose_ag2.vmdl",    "custom_models/blooming_rose.png",  true },
		};

		// The resource system does not accept a bare string. It takes the engine's
		// own path buffer, and the last word of that buffer is a tag holding the
		// *reversed* extension of the resource being registered -- 'xetv' for a
		// .vtex, 'fcpv' for a .vpcf -- which is what tells it which loader to hang
		// the path off. Every other precache in this project builds its buffer with
		// the same helper and sets the same tag.
		//
		// A model used to be registered through a plain tier0 CBufferString instead,
		// which left that tag as whatever happened to sit past the string. The
		// resource system was then handed a path it could not type and registered
		// nothing, so SetModel resolved the path to no resource at all and the pawn
		// came back wearing the engine's error model -- which is where both the
		// switch and the post-config-load failure came from.
		using init_path_buffer_fn = void ( __fastcall* )( void*, const char* );
		using precache_resource_fn = void* ( __fastcall* )( void*, void*, const char* );

		// Mirrors the engine's resource path buffer. Only the parts its own precache
		// path touches are modelled: the inline string, and the type tag.
		struct resource_path_buffer
		{
			std::uint32_t unknown1{};
			std::uint32_t inline_capacity{ 0xc00000c8 };

			union
			{
				std::uintptr_t string_pointer;
				std::uint8_t inline_storage[ 0xc8 ];
			};

			std::uintptr_t unknown3{};
			// The resource type tag, stored the way the engine stores it: as the bytes
			// of the extension, so "vmdl" is written 'ldmv'.
			std::uintptr_t type_tag{};
		};

		static_assert( sizeof( resource_path_buffer ) == 0xe0,
			"resource_path_buffer must match the engine's resource path buffer" );

		constexpr std::uintptr_t k_model_type_tag{ static_cast< std::uintptr_t >( 'ldmv' ) };

		// Slot 79 on the resource system is its own binding lookup. The skybox loader
		// uses it to tell whether a texture it just precached is live yet, and the
		// same question can be asked of a model: a binding with no resource behind it
		// means the path is registered but the load has not landed.
		constexpr std::size_t k_resource_binding_slot{ 79 };

		std::uintptr_t g_init_path_buffer{};
		std::uintptr_t g_precache_resource{};

		// Resolved on first use and retried until every piece is in place. The
		// result is deliberately not latched: a call made before the modules are
		// located would otherwise disable model loading for the whole session.
		[[nodiscard]] bool resolve_precache_api( )
		{
			if ( !g_init_path_buffer )
			{
				g_init_path_buffer = PATTERN( patterns::init_particle_path_buffer );
			}

			if ( !g_precache_resource )
			{
				g_precache_resource = PATTERN( patterns::resource_system_precache );
			}

			return g_init_path_buffer && g_precache_resource && addresses::globals::resource_system;
		}

		// Builds the tagged path buffer the resource system expects. The helper owns
		// the string storage inside the buffer, so the caller's path only has to live
		// for the length of this call.
		void build_model_path_buffer( resource_path_buffer& buffer, const char* path )
		{
			memory::try_call<void>( g_init_path_buffer, &buffer, path );
			buffer.type_tag = k_model_type_tag;
		}

		// True only when the resource system already holds a resource for the path.
		// A resource system that cannot answer reports "not ready", so the caller
		// always falls back to its own retry timing rather than waiting forever.
		[[nodiscard]] bool resource_ready( const char* path )
		{
			resource_path_buffer buffer{};
			build_model_path_buffer( buffer, path );

			const auto binding = memory::try_call_vfunc<std::uintptr_t>(
				addresses::globals::resource_system, k_resource_binding_slot, &buffer, 0ll );

			if ( !binding )
			{
				return false;
			}

			return memory::safe_read<std::uintptr_t>( binding ).value_or( 0 ) != 0;
		}

	} // namespace

	bool custom_models::initialize( )
	{
		if ( this->m_initialized )
		{
			return true;
		}

		this->m_initialized = true;
		static_cast<void>( this->assets_root( ) );
		this->build( );

		const auto installed = std::count_if( this->m_models.begin( ), this->m_models.end( ),
			[ ]( const model& entry ) { return entry.installed; } );

		logging::console::print( "[custom_models] {}/{} player models installed", installed, this->m_models.size( ) );

		if ( installed < this->m_models.size( ) )
		{
			logging::console::print( "[custom_models] run install_custom_models.bat next to Kitty.dll to install the rest" );
		}

		return true;
	}

	const custom_models::model* custom_models::find( const std::string& id ) const
	{
		if ( id.empty( ) )
		{
			return nullptr;
		}

		for ( const auto& entry : this->m_models )
		{
			// A model that is not installed is reported as absent, so the changers
			// keep the player's own model instead of writing a path the engine cannot
			// resolve.
			if ( entry.installed && entry.id == id )
			{
				return &entry;
			}
		}

		return nullptr;
	}

	bool custom_models::model_missing( const std::string& model_path ) const
	{
		if ( model_path.empty( ) )
		{
			return false;
		}

		const auto key = lowercase( model_path );

		for ( const auto& entry : this->m_models )
		{
			if ( lowercase( entry.model_path ) == key )
			{
				return !entry.installed;
			}
		}

		return false;
	}

	bool custom_models::precache_model( std::string_view model_path )
	{
		if ( model_path.empty( ) || !resolve_precache_api( ) )
		{
			return false;
		}

		// Registered on every write rather than once per session. The engine's
		// precache list belongs to the level: it is dropped when the next map loads,
		// so a path registered from the menu is not registered for the match that
		// follows, and one registered on a previous map is not registered for this
		// one. Skipping the call because "it was already done once" is what left a
		// model unregistered -- and the pawn on the error model -- after a level
		// change. On the engine's side the call is a hash insert, and the apply paths
		// that reach it are already bounded by their own request loops.
		const std::string path( model_path );

		resource_path_buffer buffer{};
		build_model_path_buffer( buffer, path.c_str( ) );

		memory::try_call<void*>( g_precache_resource, addresses::globals::resource_system, &buffer, "" );

		return true;
	}

	bool custom_models::model_ready( std::string_view model_path )
	{
		// A probe that cannot run must never hold an apply back, so an unresolved
		// resource system answers "ready" and the caller's own timing decides.
		if ( model_path.empty( ) || !resolve_precache_api( ) )
		{
			return true;
		}

		const std::string path( model_path );
		return resource_ready( path.c_str( ) );
	}

	bool custom_models::model_matches( std::uintptr_t entity, std::string_view model_path )
	{
		if ( !entity || model_path.empty( ) )
		{
			return false;
		}

		const auto scene = memory::safe_read<std::uintptr_t>( entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( !scene )
		{
			return false;
		}

		const auto handle = memory::safe_read<std::uintptr_t>(
			scene + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + SCHEMA( "CModelState", "m_hModel"_hash ) ).value_or( 0 );
		if ( !handle )
		{
			return false;
		}

		// The handle points at the entity's model state, whose first member is the
		// loaded resource. The resource carries the name the engine resolved at +8,
		// which is the only place an applied model can be told apart from the error
		// model that stands in for a path the engine could not resolve.
		const auto resource = memory::safe_read<std::uintptr_t>( handle ).value_or( 0 );
		if ( !resource )
		{
			return false;
		}

		const auto name = memory::safe_read<std::uintptr_t>( resource + 0x8 ).value_or( 0 );
		if ( !name )
		{
			return false;
		}

		// Bounded and fault-checked byte walk, so a half-rebuilt resource can only
		// ever produce a miss, never run off the end of the string.
		std::string actual{};
		actual.reserve( model_path.size( ) + 32 );

		constexpr std::size_t k_max_name{ 512 };
		for ( std::size_t i = 0; i < k_max_name; ++i )
		{
			const auto value = memory::safe_read<char>( name + i );
			if ( !value || *value == '\0' )
			{
				break;
			}

			actual.push_back( ( *value >= 'A' && *value <= 'Z' ) ? static_cast< char >( *value + 32 ) : *value );
		}

		if ( actual.empty( ) )
		{
			return false;
		}

		// The engine hands the resource the same path it was given, modulo casing and
		// separators, and it may keep the compiled `_c` suffix. Both sides are folded
		// the same way before they are compared.
		const auto normalize = [ ]( std::string value )
			{
				for ( auto& c : value )
				{
					if ( c == '\\' )
					{
						c = '/';
					}
					else if ( c >= 'A' && c <= 'Z' )
					{
						c = static_cast< char >( c + 32 );
					}
				}

				if ( value.ends_with( "_c" ) )
				{
					value.resize( value.size( ) - 2 );
				}

				return value;
			};

		const auto resolved = normalize( std::move( actual ) );
		const auto wanted = normalize( std::string( model_path ) );

		return resolved == wanted || resolved.ends_with( wanted );
	}

	const custom_models::preview_image* custom_models::get_preview( const std::string& asset_path )
	{
		if ( asset_path.empty( ) )
		{
			return nullptr;
		}

		std::lock_guard lock( this->m_preview_mutex );

		const auto found = this->m_previews.find( asset_path );
		if ( found != this->m_previews.end( ) )
		{
			return found->second->srv ? found->second.get( ) : nullptr;
		}

		// Preview textures must be uploaded on the render thread, which is the only
		// place the menu calls this. A device that is not ready yet leaves the entry
		// untouched so the load is retried on a later frame instead of latching a
		// permanent failure.
		if ( !xdraw::device( ) )
		{
			return nullptr;
		}

		std::vector<std::byte> data{};

		// The copy compiled into the dll comes first: it is what lets the menu show
		// its pictures when somebody only has Kitty.dll.
		if ( const auto* blob = preview_data::find( asset_path ) )
		{
			const auto* begin = reinterpret_cast< const std::byte* >( blob->data );
			data.assign( begin, begin + blob->size );
		}
		else
		{
			// Anything left is a file: an absolute path is a picture sitting beside
			// the models in the game files, otherwise it is relative to the asset
			// folder shipped next to the dll.
			std::filesystem::path resolved( asset_path );
			if ( !resolved.is_absolute( ) )
			{
				if ( this->m_assets_root.empty( ) )
				{
					return nullptr;
				}

				resolved = this->m_assets_root / resolved;
			}

			std::ifstream file( resolved, std::ios::binary | std::ios::ate );
			if ( !file )
			{
				return nullptr;
			}

			const auto size = static_cast< std::streamsize >( file.tellg( ) );
			if ( size <= 0 || size > 32 * 1024 * 1024 )
			{
				return nullptr;
			}

			data.resize( static_cast< std::size_t >( size ) );
			file.seekg( 0 );
			file.read( reinterpret_cast< char* >( data.data( ) ), size );
		}

		if ( data.empty( ) )
		{
			return nullptr;
		}

		auto entry = std::make_unique<preview_image>( );

		int width{};
		int height{};
		if ( auto srv = xdraw::load_texture( std::span<const std::byte>( data.data( ), data.size( ) ), &width, &height ) )
		{
			entry->srv = std::move( srv );
			entry->width = width;
			entry->height = height;
		}

		const auto inserted = this->m_previews.emplace( asset_path, std::move( entry ) ).first;
		return inserted->second->srv ? inserted->second.get( ) : nullptr;
	}

	std::filesystem::path custom_models::assets_root( )
	{
		if ( !this->m_assets_root.empty( ) )
		{
			return this->m_assets_root;
		}

		const auto looks_right = [ ]( const std::filesystem::path& dir ) -> bool
			{
				std::error_code error{};
				return std::filesystem::is_directory( dir / L"custom_models", error )
					|| std::filesystem::is_directory( dir / L"models", error );
			};

		wchar_t configured[ 32768 ]{};
		const auto configured_length = GetEnvironmentVariableW( L"KITTY_ASSET_ROOT", configured, 32768 );
		if ( configured_length > 0 && configured_length < 32768 )
		{
			const std::filesystem::path candidate( configured, configured + configured_length );
			if ( looks_right( candidate ) )
			{
				this->m_assets_root = candidate;
				return this->m_assets_root;
			}
		}

		// The previews travel with the dll, so the folder it was loaded from comes
		// first and the walk upwards only covers builds that put the dll a level or
		// two below the assets.
		auto directory = module_directory( );
		for ( auto level = 0; level < 6 && !directory.empty( ); ++level )
		{
			for ( const auto& candidate : { directory, directory / L"assets", directory / L"kitty_assets", directory / L"preview_assets" } )
			{
				if ( looks_right( candidate ) )
				{
					this->m_assets_root = candidate;
					return this->m_assets_root;
				}
			}

			const auto parent = directory.parent_path( );
			if ( parent == directory )
			{
				break;
			}

			directory = parent;
		}

		return {};
	}

	void custom_models::build( )
	{
		this->m_models.reserve( sizeof( k_model_seeds ) / sizeof( k_model_seeds[ 0 ] ) );

		// The catalog is static, but whether a model can be used at all depends on
		// the player having the compiled file in the game's content directory. An
		// absent game directory means the check cannot run, so the models stay
		// selectable and the apply path finds out the hard way.
		const auto csgo = game_path::csgo_directory( );

		for ( const auto& seed : k_model_seeds )
		{
			model entry{};
			entry.id = seed.id;
			entry.name = seed.name;
			entry.source = seed.source;
			entry.model_path = seed.model_path;
			entry.preview = seed.preview;
			entry.weapon = seed.weapon;

			if ( csgo )
			{
				std::error_code error{};
				auto compiled = std::filesystem::path( seed.model_path );
				compiled.replace_extension( L".vmdl_c" );

				entry.installed = std::filesystem::is_regular_file( *csgo / compiled, error )
					|| std::filesystem::is_regular_file( *csgo / std::filesystem::path( seed.model_path ), error );
			}

			this->m_models.push_back( std::move( entry ) );
		}
	}

} // namespace features::changer
