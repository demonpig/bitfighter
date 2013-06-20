//-----------------------------------------------------------------------------------
//
// Bitfighter - A multiplayer vector graphics space game
// Based on Zap demo released for Torque Network Library by GarageGames.com
//
// Derivative work copyright (C) 2008-2009 Chris Eykamp
// Original work copyright (C) 2004 GarageGames.com, Inc.
// Other code copyright as noted
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful (and fun!),
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//------------------------------------------------------------------------------------

#ifndef _LUAGAME_H_
#define _LUAGAME_H_


#include "LuaScriptRunner.h"
#include "LoadoutTracker.h"
#include "shipItems.h"     // For module defs

namespace Zap
{

class LuaGameInfo : public LuaBase
{
public:
   explicit LuaGameInfo(lua_State *L);     // Constructor
   virtual ~LuaGameInfo();                 // Destructor

   S32 lua_getGameType(lua_State *L);
   S32 lua_getGameTypeName(lua_State *L);
   S32 lua_getFlagCount(lua_State *L);
   S32 lua_getWinningScore(lua_State *L);
   S32 lua_getGameTimeTotal(lua_State *L);
   S32 lua_getGameTimeRemaining(lua_State *L);
   S32 lua_getLeadingScore(lua_State *L);
   S32 lua_getLeadingTeam(lua_State *L);
   S32 lua_getTeamCount(lua_State *L);

   S32 lua_getLevelName(lua_State *L);
   S32 lua_getGridSize(lua_State *L);
   S32 lua_isTeamGame(lua_State *L);

   S32 lua_getEventScore(lua_State *L);

   S32 lua_getPlayers(lua_State *L);       // Return table of all players/bots in game
   S32 lua_isNexusOpen(lua_State *L);
   S32 lua_getNexusTimeLeft(lua_State *L);

   S32 lua_getTeam(lua_State *L);

   ///// Lua interface
   // Top level Lua methods
   LUAW_DECLARE_CLASS_CUSTOM_CONSTRUCTOR(LuaGameInfo);

   static const char *luaClassName;
   static const luaL_reg luaMethods[];
   static const LuaFunctionProfile functionArgs[];
};


};

#endif

