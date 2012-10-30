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

#ifndef _TEAM_INFO_H_
#define _TEAM_INFO_H_

#include "luaGameInfo.h"      // For LuaObject def
#include "Spawn.h"            // For FlagSpawn def
#include "lineEditor.h"
#include "tnl.h"


namespace Zap
{


static const S32 TEAM_NEUTRAL = -1;
static const S32 TEAM_HOSTILE = -2;
static const S32 NO_TEAM = -3;      // Not exposed to lua, not used in level files, only used internally
static const S32 NONE = -1;


static const S32 MAX_NAME_LEN = 256;

class AbstractTeam
{
private:
   Color mColor;

public:
   AbstractTeam();           // Constructor
   virtual ~AbstractTeam();  // Destructor

   static const S32 MAX_TEAM_NAME_LENGTH = 32;

   void setColor(F32 r, F32 g, F32 b);
   void setColor(const Color &color);

   const Color *getColor() const;

   virtual void setName(const char *name) = 0;
   virtual StringTableEntry getName() = 0;

   bool processArguments(S32 argc, const char **argv);          // Read team info from level line
   string toString();

   void alterRed(F32 amt);
   void alterGreen(F32 amt);
   void alterBlue(F32 amt);
};


////////////////////////////////////////
////////////////////////////////////////

struct TeamInfo
{
   Color color;
   string name;
};


////////////////////////////////////////
////////////////////////////////////////

// Class for managing teams in the game
class Team : public AbstractTeam
{  
private:
   StringTableEntry mName;

   static S32 mNextId;    // Keep track of next available id for a new team

   S32 mPlayerCount;      // Number of human players --> Needs to be computed before use, not dynamically tracked (see countTeamPlayers())
   S32 mBotCount;         // Number of robot players --> Needs to be computed before use, not dynamically tracked

   S32 mScore;
   F32 mRating; 

   Vector<Point> mItemSpawnPoints;
   Vector<FlagSpawn *> mFlagSpawns;    // List of places for team flags to spawn

public:
   Team();              // Constructor
   virtual ~Team();     // Destructor

   S32 mId;             // Only used for tracking teams after sorting; has no inherent meaning

   void setName(const char *name);
   void setName(StringTableEntry name);

   S32 getSpawnPointCount() const;
   Point getSpawnPoint(S32 index) const;
   void addSpawnPoint(Point point);

   void addFlagSpawn(FlagSpawn *flagSpawn);
   const Vector<FlagSpawn *> *getFlagSpawns() const;
   void removeFlagSpawn(FlagSpawn *flagSpawn);
  
   StringTableEntry getName();

   S32 getScore();
   void setScore(S32 score);
   void addScore(S32 score);

   F32 getRating();
   void addRating(F32 rating);     // For summing ratings of all players on a team

   void clearStats();

   // Players & bots on each team:
   // Note that these values need to be precalulated before they are ready for use;
   // they are not dynamically updated!
   S32 getPlayerCount();      // Get number of human players on team
   S32 getBotCount();         // Get number of bots on team
   S32 getPlayerBotCount();   // Get total number of players/bots on team

   void incrementPlayerCount();
   void incrementBotCount();
};


////////////////////////////////////////
////////////////////////////////////////

// Class for managing teams in the editor
class EditorTeam : public AbstractTeam
{
private:
   LineEditor mNameEditor;

public:
   EditorTeam();           // Constructor
   virtual ~EditorTeam();  // Destructor

   LineEditor *getLineEditor();
   void setName(const char *name);
   StringTableEntry getName();  // Wrap in STE to make signatures match
};


////////////////////////////////////////
////////////////////////////////////////

class LuaTeamInfo : public LuaObject
{

private:
   S32 mTeamIndex;                // Robots could potentially be on neutral or hostile team; maybe observer player could too
   Team *mTeam;

public:
   LuaTeamInfo(lua_State *L);     // Lua constructor
   LuaTeamInfo(Team *team);       // C++ constructor

   ~LuaTeamInfo();                // Destructor

   static const char className[];

   static Lunar<LuaTeamInfo>::RegType methods[];

   S32 getName(lua_State *L);
   S32 getIndex(lua_State *L);
   S32 getPlayerCount(lua_State *L);
   S32 getScore(lua_State *L);
   S32 getPlayers(lua_State *L);
};


////////////////////////////////////////
////////////////////////////////////////

class TeamManager
{
private:
   Vector<AbstractTeam *> mTeams;
   Vector<S32> mTeamHasFlagList;      // Track which team (or teams) have the flag

public:
   ~TeamManager();      // Destructor

   const Color *getTeamColor(S32 index);
   S32 getTeamCount();

   AbstractTeam *getTeam(S32 teamIndex);

   void removeTeam(S32 teamIndex);
   void addTeam(AbstractTeam *team);
   void addTeam(AbstractTeam *team, S32 index);
   void replaceTeam(AbstractTeam *team, S32 index);
   void clearTeams();

   // Access to mTeamHasFlagList
   bool getTeamHasFlag(S32 teamIndex) const;
   void setTeamHasFlag(S32 teamIndex, bool hasFlag);
   void clearTeamHasFlagList();
};


};

#endif


