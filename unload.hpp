#pragma once

namespace unload {

	// Remembered from DllMain so the eject thread can free the right image.
	void set_module( HMODULE module );

	// Records the eject request. Safe from any thread; only the first one wins.
	void request( );

	[[nodiscard]] bool requested( );

	// True once owned game state has been reverted and every hook is removed.
	[[nodiscard]] bool teardown_complete( );

	// Runs the teardown and schedules the eject. Must be called from the render
	// thread (inside the Present detour) so the game state reverts on the thread
	// that owns it, and so the module outlives the final Present call.
	void run( );

} // namespace unload
