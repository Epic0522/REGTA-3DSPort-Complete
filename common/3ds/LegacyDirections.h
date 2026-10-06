#pragma once

// Script directions use the Circle Pad without changing analog movement.
// Frontend navigation can also read the physical D-pad; both sources share
// one edge so a held direction never advances the menu twice.
namespace LegacyDirections3DS {
enum Direction { Up, Down, Left, Right };
template<class State>
inline bool Held(const State &state, Direction direction, bool menu = false)
{
	const int threshold = 32;
	switch(direction){
	case Up: return state.LeftStickY < -threshold || (menu && state.DPadUp);
	case Down: return state.LeftStickY > threshold || (menu && state.DPadDown);
	case Left: return state.LeftStickX < -threshold || (menu && state.DPadLeft);
	case Right: return state.LeftStickX > threshold || (menu && state.DPadRight);
	}
	return false;
}
template<class State>
inline bool JustDown(const State &now, const State &old, Direction direction, bool menu)
{
	return Held(now, direction, menu) && !Held(old, direction, menu);
}
template<class State>
inline bool JustUp(const State &now, const State &old, Direction direction, bool menu)
{
	return !Held(now, direction, menu) && Held(old, direction, menu);
}
}
