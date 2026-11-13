#pragma once
// The kinds of water body (lesson 35). Pure: no graphics headers, so the table in WaterTypeUi.h can be tested on its own.
//
// A type is a starting point and a promise about what matters: an OCEAN is big swells rolling in with the wind, a LAKE is
// short wind chop that hardly breaks, a POND barely moves, and a RIVER is a current: everything on it is carried downstream.
// CUSTOM is a blank slate that shows every setting.
// Values are saved in scene files by name: only ever ADD to the end.
enum class WaterBodyType : int { Ocean = 0, Lake = 1, River = 2, Pond = 3, Custom = 4 };
