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

#ifndef ZAP_DEDICATED
#  include "SDL_opengl.h"
#  include "UIMenuItems.h"
#  include "UIEditorMenus.h"
#  include "ClientGame.h"
#endif

#include "Spawn.h"
#include "game.h"

#include "stringUtils.h"         // For itos()
#include "gameObjectRender.h"    // For renderSquareItem(), renderFlag(), drawCircle()
#include "moveObject.h"          // For Circle, Asteroid class defs


namespace Zap
{

// Statics:
#ifndef ZAP_DEDICATED
   EditorAttributeMenuUI *AbstractSpawn::mAttributeMenuUI = NULL;
#endif

AbstractSpawn::AbstractSpawn(const Point &pos, S32 time)
{
   setPos(pos);
   setRespawnTime(time);
};


AbstractSpawn::AbstractSpawn(const AbstractSpawn &copy) : PointObject(copy), mTimer(copy.mTimer)
{
   mSpawnTime = copy.mSpawnTime;
}


AbstractSpawn::~AbstractSpawn()
{
   // Do nothing
}


void AbstractSpawn::setRespawnTime(S32 time)       // in seconds
{
   mSpawnTime = time;
   mTimer.setPeriod(time * 1000);
   mTimer.reset();
}


F32 AbstractSpawn::getEditorRadius(F32 currentScale)
{
   return 12;     // Constant size, regardless of zoom
}


bool AbstractSpawn::processArguments(S32 argc, const char **argv, Game *game)
{
   if(argc < 2)
      return false;

   Point pos;
   pos.read(argv);
   pos *= game->getGridSize();

   setPos(pos);

   S32 time = (argc > 2) ? atoi(argv[2]) : getDefaultRespawnTime();

   setRespawnTime(time);

   updateExtentInDatabase();

   return true;
}


string AbstractSpawn::toString(F32 gridSize) const
{
   // <<spawn class name>> <x> <y> <spawn timer>
   return string(getClassName()) + " " + geomToString(gridSize) + " " + itos(mSpawnTime);
}


bool AbstractSpawn::updateTimer(U32 deltaT)
{
   return mTimer.update(deltaT);
}


void AbstractSpawn::resetTimer()
{
   mTimer.reset();
}


U32 AbstractSpawn::getPeriod()
{
   return mTimer.getPeriod();
}


#ifndef ZAP_DEDICATED

EditorAttributeMenuUI *AbstractSpawn::getAttributeMenu()
{
   if(getDefaultRespawnTime() == -1)  // No editing RespawnTimer for Ship Spawn
      return NULL;

   if(!mAttributeMenuUI)
   {
      ClientGame *clientGame = static_cast<ClientGame *>(getGame());

      mAttributeMenuUI = new EditorAttributeMenuUI(clientGame);

      CounterMenuItem *menuItem = new CounterMenuItem("Spawn Timer:", 999, 1, 0, 1000, "secs", "Never spawns", 
                                                      "Time it takes for each item to be spawned");
      mAttributeMenuUI->addMenuItem(menuItem);

      // Add our standard save and exit option to the menu
      mAttributeMenuUI->addSaveAndQuitMenuItem();
   }

   return mAttributeMenuUI;
}


void AbstractSpawn::startEditingAttrs(EditorAttributeMenuUI *attributeMenu)
{
   attributeMenu->getMenuItem(0)->setIntValue(mSpawnTime);
}


void AbstractSpawn::doneEditingAttrs(EditorAttributeMenuUI *attributeMenu)
{
   mSpawnTime = attributeMenu->getMenuItem(0)->getIntValue();
}


// Render some attributes when item is selected but not being edited
string AbstractSpawn::getAttributeString()
{

   if(getDefaultRespawnTime() == -1)
      return "";

   if(mSpawnTime == 0)
      return "Disabled";
   else
      return "Spawn time: " + itos(mSpawnTime) + " sec" + ( mSpawnTime != 1 ? "s" : "");
}

#endif

////////////////////////////////////////
////////////////////////////////////////

// Constructor
Spawn::Spawn(const Point &pos) : AbstractSpawn(pos)
{
   mObjectTypeNumber = ShipSpawnTypeNumber;
}


// Destructor
Spawn::~Spawn()
{
   // Do nothing
}


Spawn *Spawn::clone() const
{
   return new Spawn(*this);
}


bool Spawn::processArguments(S32 argc, const char **argv, Game *game)
{
   if(argc < 3)
      return false;

   S32 teamIndex = atoi(argv[0]);
   setTeam(teamIndex);

   Parent::processArguments(argc - 1, argv + 1, game);

   return true;
}


string Spawn::toString(F32 gridSize) const
{
   // Spawn <team> <x> <y> 
   return string(getClassName()) + " " + itos(mTeam) + " " + geomToString(gridSize);
}


const char *Spawn::getEditorHelpString()
{
   return "Location where ships start.  At least one per team is required. [G]";
}


const char *Spawn::getPrettyNamePlural()
{
   return "Spawn Points";
}


const char *Spawn::getOnDockName()
{
   return "Spawn";
}


const char *Spawn::getOnScreenName()
{
   return "Spawn";
}


const char *Spawn::getClassName() const
{
   return "Spawn";
}


S32 Spawn::getDefaultRespawnTime()
{
   return -1;
}


void Spawn::renderEditor(F32 currentScale, bool snappingToWallCornersEnabled)
{
#ifndef ZAP_DEDICATED
   Point pos = getPos();

   glPushMatrix();
      glTranslatef(pos.x, pos.y, 0);
      glScalef(1/currentScale, 1/currentScale, 1);    // Make item draw at constant size, regardless of zoom
      renderSquareItem(Point(0,0), getColor(), 1, &Colors::white, 'S');
   glPopMatrix();
#endif
}


void Spawn::renderDock()
{
   renderEditor(1, false);
}


////////////////////////////////////////
////////////////////////////////////////

// Constructor
ItemSpawn::ItemSpawn(const Point &pos, S32 time) : Parent(pos, time)
{
   // Do nothing
}


////////////////////////////////////////
////////////////////////////////////////

// Constructor
AsteroidSpawn::AsteroidSpawn(const Point &pos, S32 time) : Parent(pos, time)
{
   mObjectTypeNumber = AsteroidSpawnTypeNumber;
}

// Destructor
AsteroidSpawn::~AsteroidSpawn()
{
   // Do nothing
}


AsteroidSpawn *AsteroidSpawn::clone() const
{
   return new AsteroidSpawn(*this);
}


const char *AsteroidSpawn::getEditorHelpString()
{
   return "Periodically spawns a new asteroid.";
}


const char *AsteroidSpawn::getPrettyNamePlural()
{
   return "Asteroid Spawn Points";
}


const char *AsteroidSpawn::getOnDockName()
{
   return "ASP";
}


const char *AsteroidSpawn::getOnScreenName()
{
   return "AsteroidSpawn";
}


const char *AsteroidSpawn::getClassName() const
{
   return "AsteroidSpawn";
}


S32 AsteroidSpawn::getDefaultRespawnTime()
{
   return DEFAULT_RESPAWN_TIME;
}


void AsteroidSpawn::spawn(Game *game, const Point &pos)
{
   Asteroid *asteroid = new Asteroid();   // Create a new asteroid

   F32 ang = TNL::Random::readF() * Float2Pi;

   asteroid->setPosAng(pos, ang);

   asteroid->addToGame(game, game->getGameObjDatabase());              // And add it to the list of game objects
}


static void renderAsteroidSpawn(const Point &pos)
{
#ifndef ZAP_DEDICATED
   F32 scale = 0.8f;
   static const Point p(0,0);

   glPushMatrix();
      glTranslatef(pos.x, pos.y, 0);
      glScalef(scale, scale, 1);
      renderAsteroid(p, 2, .1f);

      glColor(Colors::white);
      drawCircle(p, 13);
   glPopMatrix();  
#endif
}


void AsteroidSpawn::renderEditor(F32 currentScale, bool snappingToWallCornersEnabled)
{
#ifndef ZAP_DEDICATED
   Point pos = getPos();

   glPushMatrix();
      glTranslate(pos);
      glScale(1/currentScale);    // Make item draw at constant size, regardless of zoom
      renderAsteroidSpawn(Point(0,0));
   glPopMatrix();   
#endif
}


void AsteroidSpawn::renderDock()
{
   renderAsteroidSpawn(getPos());
}


////////////////////////////////////////
////////////////////////////////////////

// Constructor
CircleSpawn::CircleSpawn(const Point &pos, S32 time) : Parent(pos, time)
{
   mObjectTypeNumber = CircleSpawnTypeNumber;
}


CircleSpawn *CircleSpawn::clone() const
{
   return new CircleSpawn(*this);
}


const char *CircleSpawn::getEditorHelpString()
{
   return "Periodically spawns a new circle.";
}


const char *CircleSpawn::getPrettyNamePlural()
{
   return "Circle Spawn Points";
}


const char *CircleSpawn::getOnDockName()
{
   return "CSP";
}


const char *CircleSpawn::getOnScreenName()
{
   return "CircleSpawn";
}


const char *CircleSpawn::getClassName() const
{
   return "CircleSpawn";
}


S32 CircleSpawn::getDefaultRespawnTime()
{
   return DEFAULT_RESPAWN_TIME;
}


void CircleSpawn::spawn(Game *game, const Point &pos)
{
   for(S32 i = 0; i < 10; i++)
   {
      Circle *circle = new Circle();   // Create a new Circle
      F32 ang = TNL::Random::readF() * Float2Pi;

      circle->setPosAng(pos, ang);

      circle->addToGame(game, game->getGameObjDatabase());              // And add it to the list of game objects
   }
}


static void renderCircleSpawn(const Point &pos)
{
#ifndef ZAP_DEDICATED
   F32 scale = 0.8f;
   static const Point p(0,0);

   glPushMatrix();
      glTranslatef(pos.x, pos.y, 0);
      glScalef(scale, scale, 1);
      drawCircle(p, 8);

      glColor(Colors::white);
      drawCircle(p, 13);
   glPopMatrix();  
#endif
}


void CircleSpawn::renderEditor(F32 currentScale, bool snappingToWallCornersEnabled)
{
#ifndef ZAP_DEDICATED
   Point pos = getPos();

   glPushMatrix();
      glTranslate(pos);
      glScale(1/currentScale);    // Make item draw at constant size, regardless of zoom
      renderCircleSpawn(Point(0,0));
   glPopMatrix();   
#endif
}


void CircleSpawn::renderDock()
{
   renderCircleSpawn(getPos());
}


////////////////////////////////////////
////////////////////////////////////////

// Constructor
FlagSpawn::FlagSpawn(const Point &pos, S32 time) : AbstractSpawn(pos, time)
{
   mObjectTypeNumber = FlagSpawnTypeNumber;
}


FlagSpawn::~FlagSpawn()
{
   // Do nothing
}


FlagSpawn *FlagSpawn::clone() const
{
   return new FlagSpawn(*this);
}


bool FlagSpawn::updateTimer(S32 deltaT)
{
   return mTimer.update(deltaT);
}


void FlagSpawn::resetTimer()
{
   mTimer.reset();
}


const char *FlagSpawn::getEditorHelpString()
{
   return "Location where flags (or balls in Soccer) spawn after capture.";
}


const char *FlagSpawn::getPrettyNamePlural()
{
   return "Flag Spawn points";
}


const char *FlagSpawn::getOnDockName()
{
   return "FlagSpawn";
}


const char *FlagSpawn::getOnScreenName()
{
   return "FlagSpawn";
}


const char *FlagSpawn::getClassName() const
{
   return "FlagSpawn";
}


S32 FlagSpawn::getDefaultRespawnTime()
{
   return DEFAULT_RESPAWN_TIME;
}


void FlagSpawn::renderEditor(F32 currentScale, bool snappingToWallCornersEnabled)
{
#ifndef ZAP_DEDICATED
   Point pos = getPos();

   glPushMatrix();
      glTranslatef(pos.x + 1, pos.y, 0);
      glScalef(0.4f/currentScale, 0.4f/currentScale, 1);
      Color color = getColor();  // To avoid taking address of temporary
      renderFlag(0, 0, &color);

      glColor(Colors::white);
      drawCircle(-4, 0, 26);
   glPopMatrix();
#endif
}


void FlagSpawn::renderDock()
{
   renderEditor(1, false);
}


bool FlagSpawn::processArguments(S32 argc, const char **argv, Game *game)
{
   if(argc < 1)
      return false;

   mTeam = atoi(argv[0]);                                            // Read team
   return Parent::processArguments(argc - 1, argv + 1, game);        // then read the rest of the args
}


string FlagSpawn::toString(F32 gridSize) const
{
   // FlagSpawn <team> <x> <y> <spawn timer for nexus> -- squeezing in team number from AbstractSpawn::toString
   string str1 = Parent::toString(gridSize);
   size_t firstarg = str1.find(' ');
   return str1.substr(0, firstarg) + " " + itos(mTeam) + str1.substr(firstarg);
}


};    // namespace
