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
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//------------------------------------------------------------------------------------

#include "ClientGame.h"

#include "ChatHelper.h"
#include "masterConnection.h"
#include "gameNetInterface.h"
#include "IniFile.h"             // For CIniFile def

#include "barrier.h"
#include "gameType.h"
#include "UIManager.h"
#include "EditorTeam.h"

#include "LevelDatabaseRateThread.h"

#include "Colors.h"
#include "stringUtils.h"

#include <boost/shared_ptr.hpp>
#include <sys/stat.h>
#include <cmath>

using namespace TNL;

namespace Zap
{

// Our global ClientGame list
Vector<ClientGame *> gClientGames;


static bool hasHelpItemForObjects[TypesNumbers];

static void initializeHelpItemForObjects()
{
   static bool initialized = false;

   if(initialized)
      return;

   for(S32 i = 0; i < TypesNumbers; i++)
      hasHelpItemForObjects[i] = false;

   for(S32 i = 0; i < HelpItemCount; i++)
   {
      U8 objectType = HelpItemManager::getAssociatedObjectType(HelpItem(i));
      if(objectType != UnknownTypeNumber)
         hasHelpItemForObjects[objectType] = true;
   }

   initialized = true;
}


// Constructor
ClientGame::ClientGame(const Address &bindAddress, GameSettingsPtr settings, UIManager *uiManager) : Game(bindAddress, settings)
{
   //mUserInterfaceData = new UserInterfaceData();

   mRemoteLevelDownloadFilename = "downloaded.level";

   mUIManager = uiManager;                // Gets deleted in destructor
   mUIManager->setClientGame(this);       // Need to do this before we can use it

   mClientInfo = new FullClientInfo(this, NULL, mSettings->getPlayerName(), false);  // Will be deleted in destructor

   mScreenSaverTimer.reset(59 * 1000);    // Fire screen saver supression every 59 seconds

   for(S32 i = 0; i < JoystickAxesDirectionCount; i++)
      mJoystickInputs[i] = 0;

   initializeHelpItemForObjects();

   mShowAllObjectOutlines = false;        // Will only be changed in debug builds... in production code will never be true
}


// Destructor
ClientGame::~ClientGame()
{
   if(getConnectionToMaster()) // Prevents errors when ClientGame is gone too soon.
      getConnectionToMaster()->disconnect(NetConnection::ReasonSelfDisconnect, "");

   closeConnectionToGameServer();      // I just added this for good measure... not really sure it's needed
   cleanUp();                          // Among other things, will delete all teams

   //delete mUserInterfaceData;
   delete mUIManager; 
   delete mConnectionToServer.getPointer();
}


// Gets run when we join a game that we ourselves are hosting.  Is also used in tests for creating linked pairs of client/server games.
void ClientGame::joinLocalGame(GameNetInterface *remoteInterface)
{
   // Much of the time, this may seem pointless, but if we arrive here via the editor, we need to swap out the editor's team manager for
   // the one used by the game.  If we don't we'll clobber the editor's copy, and we'll get crashes in the team definition (F2) menu.
   setActiveTeamManager(&mTeamManager);

   mClientInfo->setRole(ClientInfo::RoleOwner);       // Local connection is always owner

   getUIManager()->activateGameUI();

   GameConnection *gameConnection = new GameConnection(this);
 
   setConnectionToServer(gameConnection);

   gameConnection->connectLocal(getNetInterface(), remoteInterface);

   // Note that gc and gameConnection aren't the same, nor are gc->getClientInfo() and mClientInfo the same.
   // I _think_ gc is the server view of the local connection, where as gameConnection is the client's view.
   // Likewise with the clientInfos.  A little confusing, as they really represent the same thing in a way.  But different.
   TNLAssert(dynamic_cast<GameConnection *>(gameConnection->getRemoteConnectionObject()), 
               "This should never be NULL here -- if it is, it means our connection to ourselves has failed for some reason");

   GameConnection *gc = static_cast<GameConnection *>(gameConnection->getRemoteConnectionObject()); 
   gc->onLocalConnection();
}


// Player has selected a game from the QueryServersUserInterface, and is ready to join
// Also get here when hosting a game
void ClientGame::joinRemoteGame(Address remoteAddress, bool isFromMaster)
{
   // Much of the time, this may seem pointless, but if we arrive here via the editor, we need to swap out the editor's team manager for
   // the one used by the game.  If we don't we'll clobber the editor's copy, and we'll get crashes in the team definition (F2) menu.
   // Do we need this for joining a remote game?
   setActiveTeamManager(&mTeamManager);    

   mClientInfo->setRole(ClientInfo::RoleNone);        // Start out with no permissions, server will upgrade if the proper pws are provided
   
   MasterServerConnection *connToMaster = getConnectionToMaster();

   bool useArrangedConnection = isFromMaster && connToMaster && connToMaster->getConnectionState() == NetConnection::Connected;

   getUIManager()->activateGameUserInterface();

   if(useArrangedConnection)  // Request arranged connection
      connToMaster->requestArrangedConnection(remoteAddress);

   else                       // Try a direct connection
   {
      GameConnection *gameConnection = new GameConnection(this);

      setConnectionToServer(gameConnection);

      // Connect to a remote server, but not via the master server
      gameConnection->connect(getNetInterface(), remoteAddress);  
   }
}


// Called when the local connection to game server is terminated for one reason or another
void ClientGame::closeConnectionToGameServer()
{
    // Cancel any in-progress attempts to connect
   if(getConnectionToMaster())
      getConnectionToMaster()->cancelArrangedConnectionAttempt();

   // Disconnect from game server
   if(getConnectionToServer())
      getConnectionToServer()->disconnect(NetConnection::ReasonSelfDisconnect, "");

   getUIManager()->disableLevelLoadDisplay(false);

   onGameReallyAndTrulyOver();  
}


void ClientGame::onConnectedToMaster()
{
   Parent::onConnectedToMaster();

   getUIManager()->onConnectedToMaster();

   // Clear old player list that might be there from client's lost connection to master while in game lobby
   Vector<StringTableEntry> emptyPlayerList;
   setPlayersInGlobalChat(emptyPlayerList);

   // Request ratings for current level if we don't already have them

   if(needsRating())
      mConnectionToMaster->c2mRequestLevelRating(getLevelDatabaseId());

   logprintf(LogConsumer::LogConnection, "Client established connection with Master Server");
}


bool ClientGame::isConnectedToServer() const
{
   return mConnectionToServer.isValid() && mConnectionToServer->getConnectionState() == NetConnection::Connected;
}


GameConnection *ClientGame::getConnectionToServer() const
{
   return mConnectionToServer;
}


void ClientGame::setConnectionToServer(GameConnection *connectionToServer)
{
   TNLAssert(connectionToServer, "Passing null connection.  Bah!");
   TNLAssert(mConnectionToServer.isNull(), "Error, a connection already exists here.");

   mConnectionToServer = connectionToServer;
   mConnectionToServer->setClientGame(this);
}


void ClientGame::startLoadingLevel(bool engineerEnabled)
{
   mObjectsLoaded = 0;                       // Reset item counter

   getUIManager()->startLoadingLevel(engineerEnabled);
}


void ClientGame::doneLoadingLevel()
{
   computeWorldObjectExtents();              // Make sure our world extents reflect all the objects we've loaded
   Barrier::prepareRenderingGeometry(this);  // Get walls ready to render

   getUIManager()->doneLoadingLevel();
}


ClientInfo *ClientGame::getClientInfo() const
{
   return mClientInfo;
}


ClientInfo *ClientGame::getLocalRemoteClientInfo() const
{
   return mLocalRemoteClientInfo;
}


void ClientGame::setSpawnDelayed(bool spawnDelayed)
{
   if(!spawnDelayed)
   {
      unsuspendGame();
      getUIManager()->clearSparks();
   }
}


void ClientGame::emitBlast(const Point &pos, U32 size)
{
   getUIManager()->emitBlast(pos, size);
}


void ClientGame::emitBurst(const Point &pos, const Point &scale, const Color &color1, const Color &color2)
{
   getUIManager()->emitBurst(pos, scale, color1, color2);
}


void ClientGame::emitDebrisChunk(const Vector<Point> &points, const Color &color, const Point &pos, const Point &vel, S32 ttl, F32 angle, F32 rotation)
{
   getUIManager()->emitDebrisChunk(points, color, pos, vel, ttl, angle, rotation);
}


void ClientGame::emitTextEffect(const string &text, const Color &color, const Point &pos) const
{
   getUIManager()->emitTextEffect(text, color, pos);
}


void ClientGame::emitSpark(const Point &pos, const Point &vel, const Color &color, S32 ttl, UI::SparkType sparkType)
{
   getUIManager()->emitSpark(pos, vel, color, ttl, sparkType);
}


void ClientGame::emitExplosion(const Point &pos, F32 size, const Color *colorArray, U32 numColors)
{
   getUIManager()->emitExplosion(pos, size, colorArray, numColors);
}


void ClientGame::emitTeleportInEffect(const Point &pos, U32 type)
{
   getUIManager()->emitTeleportInEffect(pos, type);
}


Color ShipExplosionColors[] = {
   Colors::red,  Color(0.9, 0.5, 0),  Colors::white,     Colors::yellow,
   Colors::red,  Color(0.8, 1.0, 0),  Color(1, 0.5, 0),  Colors::white,
   Colors::red,  Color(0.9, 0.5, 0),  Colors::white,     Colors::yellow,
};


void ClientGame::emitShipExplosion(const Point &pos)
 {
   playSoundEffect(SFXShipExplode, pos);

   F32 a = TNL::Random::readF() * 0.4f + 0.5f;
   F32 b = TNL::Random::readF() * 0.2f + 0.9f;

   F32 c = TNL::Random::readF() * 0.15f + 0.125f;
   F32 d = TNL::Random::readF() * 0.2f + 0.9f;

   emitExplosion(pos, 0.9f, ShipExplosionColors, ARRAYSIZE(ShipExplosionColors));
   emitBurst(pos, Point(a,c), Color(1,1,0.25), Colors::red);
   emitBurst(pos, Point(b,d), Colors::yellow, Color(0,0.75,0));
 }


SFXHandle ClientGame::playSoundEffect(U32 profileIndex, F32 gain) const
{
   return getUIManager()->playSoundEffect(profileIndex, gain);
}


SFXHandle ClientGame::playSoundEffect(U32 profileIndex, const Point &position) const
{
   return getUIManager()->playSoundEffect(profileIndex, position);
}


SFXHandle ClientGame::playSoundEffect(U32 profileIndex, const Point &position, const Point &velocity, F32 gain) const
{
   return getUIManager()->playSoundEffect(profileIndex, position, velocity, gain);
}


void ClientGame::playNextTrack() const
{
   getUIManager()->playNextTrack();
}


void ClientGame::playPrevTrack() const
{
   getUIManager()->playPrevTrack();
}


void ClientGame::queueVoiceChatBuffer(const SFXHandle &effect, const ByteBufferPtr &p) const
{
   getUIManager()->queueVoiceChatBuffer(effect, p);
}


// TODO: This should be moved to ship or UIManager or something... doesn't really belong here
void ClientGame::updateModuleSounds(const Point &pos, const Point &vel, const LoadoutTracker &loadout)
{
   const S32 moduleSFXs[ModuleCount] =
   {
      SFXShieldActive,
      SFXShipBoost,
      SFXNone,       // No more sensor
      SFXRepairActive,
      SFXUIBoop,     // Need better sound...
      SFXCloakActive,
      SFXNone,       // Armor... tough, but he don't say much
   };
   
   for(U32 i = 0; i < ModuleCount; i++)
   {
      if(loadout.isModulePrimaryActive(ShipModule(i)) && moduleSFXs[i] != SFXNone)
      {
         if(mModuleSound[i].isValid())
            getUIManager()->setMovementParams(mModuleSound[i], pos, vel);
         else
            mModuleSound[i] = playSoundEffect(moduleSFXs[i], pos, vel);
      }
      else
      {
         if(mModuleSound[i].isValid())
         {
            getUIManager()->stopSoundEffect(mModuleSound[i]);
            mModuleSound[i] = NULL;
         }
      }
   }
}


// User selected Switch Teams menu item
void ClientGame::switchTeams()
{
   if(!mGameType)    // I think these GameType checks are not needed
      return;

   // If there are only two teams, just switch teams and skip the rigamarole
   if(getTeamCount() == 2)
   {
      Ship *ship = getLocalPlayerShip();     // Returns player's ship, can return NULL...
      if(!ship)
         return;

      changeOwnTeam(1 - ship->getTeam());    // If two teams, team will either be 0 or 1, so "1 - team" will toggle
      getUIManager()->reactivateGameUI();    // Jump back into the game (this option takes place immediately)
   }
   else     // More than 2 teams, need to present menu to choose
      getUIManager()->showMenuToChangeTeamForPlayer(getPlayerName());  
}


// User has pressed a key, finished composing that most eloquent of chat messages, or taken some other action to undelay their spawn
void ClientGame::undelaySpawn()
{
   if(!isSpawnDelayed())                        // Already undelayed, nothing to do
      return;

   if(mClientInfo->getReturnToGameTime() > 0)   // Waiting for post /idle rejoin timer to wind down, nothing to do
      return;

   getConnectionToServer()->c2sPlayerSpawnUndelayed();
}


// Provide access to these annoying bools
bool ClientGame::isSpawnDelayed() const
{ 
   return getClientInfo()->isSpawnDelayed();
}


// Returns NONE if we are not leveling up
S32 ClientGame::getLevelThreshold(S32 val) const
{
   return NONE;      // Disabled for the time being...

   //switch(val)
   //{
   //   // This many games | Just achieved this level
   //   case 20:             return 1;         
   //   case 50:             return 2;
   //   case 100:            return 3;
   //   case 200:            return 4;
   //   case 500:            return 5;
   //   case 1000:           return 6;
   //   case 2000:           return 7;
   //   case 5000:           return 8;
   //   default:             return NONE;
   //}
}


// Tells the server to spawn delay us... server may incur a penalty when we unspawn
void ClientGame::requestSpawnDelayed(bool incursPenalty) const
{
   getConnectionToServer()->c2sPlayerRequestSpawnDelayed(incursPenalty);
}


U32 ClientGame::getReturnToGameDelay() const
{
   return mClientInfo->getReturnToGameTime();
}


bool ClientGame::inReturnToGameCountdown() const
{
   return getReturnToGameDelay() > 0;
}


string ClientGame::getPlayerName()     const { return mSettings->getPlayerName();     }
string ClientGame::getPlayerPassword() const { return mSettings->getPlayerPassword(); }


bool ClientGame::isLevelInDatabase() const
{
   return LevelDatabase::isLevelInDatabase(getLevelDatabaseId());
}


bool ClientGame::needsRating() const
{
   // We don't need ratings for levels not in the database
   return isLevelInDatabase() && (mPlayerLevelRating == UnknownRating || mTotalLevelRating == UnknownRating);
}


// static method
ClientGame::PersonalRating ClientGame::getNextRating(PersonalRating currentRating)
{
   if(currentRating == RatingGood)     return RatingBad;
   if(currentRating == RatingNeutral)  return RatingGood;
   if(currentRating == RatingBad)      return RatingNeutral;
   if(currentRating == Unrated)        return RatingNeutral;

   TNLAssert(false, "Expected valid rating here!");
   return RatingNeutral;
}


void ClientGame::gotTotalLevelRating(S16 rating)
{
   mTotalLevelRating = rating;
}


void ClientGame::gotPlayerLevelRating(S32 rating)
{
   mPlayerLevelRating = PersonalRating(rating);
}


bool ClientGame::canRateLevel() const
{

   if(!isLevelInDatabase())
   {
      displayErrorMessage("!!! Level is not in database, so it cannot be rated (upload via editor)");
      return false;
   }

   if(!getClientInfo()->isAuthenticated())
   {
      displayErrorMessage("!!! Only registered players can rate levels (register in forums)");
      return false;
   }

   return true;
}


// On the client, this is called when we are in the editor, or when we've just begun a new game and the server has sent
// us the latest 411 on the level we're about to play
void ClientGame::setLevelDatabaseId(U32 id)
{
   Parent::setLevelDatabaseId(id);  // (just does mLevelDatabaseId = id)

   // If we are in a game, and connected to master,then we can request that the master server send us the current level ratings.
   // If we are connected to a game server, then we are not in the editor (though we could be testing a level).
   if(mConnectionToMaster && mConnectionToMaster->isEstablished() && isConnectedToServer() && needsRating())
      mConnectionToMaster->c2mRequestLevelRating(id);
}


void ClientGame::userEnteredLoginCredentials(const string &name, const string &password, bool savePassword)
{
   getClientInfo()->setName(name);
   getSettings()->setLoginCredentials(name, password, savePassword);      // Saves to INI

   mNextMasterTryTime = 0;                   // Triggers connection attempt with master

   setReadyToConnectToMaster(true);

   seedRandomNumberGenerator(name);

   if(getConnectionToServer())               // Rename while in game server, if connected
      getConnectionToServer()->c2sRenameClient(name.c_str());
}


// When server corrects capitalization of name or similar
void ClientGame::correctPlayerName(const string &name)
{
   getClientInfo()->setName(name);
   getSettings()->updatePlayerName(name);    // Saves to INI
}


void ClientGame::displayShipDesignChangedMessage(const LoadoutTracker &loadout, const char *msgToShowIfLoadoutsAreTheSame)
{
   if(!getConnectionToServer())
      return;

   Ship *ship = getLocalPlayerShip();
   if(!ship)
      return;

   // If we're in a loadout zone, don't show any message -- new loadout will become active immediately, 
   // and we'll get a different msg from the server.  Avoids unnecessary messages.
   if(ship->isInZone(LoadoutZoneTypeNumber))
      return;

   if(getSettings()->getIniSettings()->mSettings.getVal<YesNo>("VerboseHelpMessages"))
   {
      if(ship->isLoadoutSameAsCurrent(loadout))
         displayErrorMessage(msgToShowIfLoadoutsAreTheSame);
      else
      {
         GameType *gt = getGameType();

         displaySuccessMessage("Ship design changed -- %s", 
                              gt->levelHasLoadoutZone() ? "enter Loadout Zone to activate changes" : 
                                                          "changes will be activated when you respawn");
      }
   }
}


// We might get here if a script ran amok in the editor...  nothing to do, really
void ClientGame::deleteLevelGen(LuaLevelGenerator *levelgen)
{
   // Do nothing
}


bool ClientGame::isServer() const
{
   return false;
}


bool hasRelatedHelpItem(U8 x)
{
   return hasHelpItemForObjects[x];
}


U32 prevTimeDelta = 0;

void ClientGame::idle(U32 timeDelta)
{
   Parent::idle(timeDelta);

   mNetInterface->checkIncomingPackets();

   checkConnectionToMaster(timeDelta);   // If no current connection to master, create (or recreate) one

   mClientInfo->updateReturnToGameTimer(timeDelta);

   mCurrentTime += timeDelta;

   computeWorldObjectExtents();

   Move *theMove = getUIManager()->getCurrentMove();       // Get move from keyboard input

   theMove->time = timeDelta + prevTimeDelta;

   if(mConnectionToServer.isValid())      // i.e. if we're connected to a game server
   {
      // Disable controls if we are going too fast (usually by being blasted around by a GoFast or mine or whatever)
      BfObject *localPlayerShip = getLocalPlayerShip();

      // Don't saturate server with moves...
      if(theMove->time < 6)     // Why 6?  Can this be related to some other factor?
         prevTimeDelta += timeDelta;
      else
      { 
         // Limited MaxPendingMoves only allows sending a few moves at a time. 
         // Changing MaxPendingMoves may break compatibility with old version server/client.
         // If running at 1000 FPS and 300 ping it will try to add more then MaxPendingMoves, losing control horribly.
         // Without the unlimited shield fix, the ship would also go super slow motion with over the limit MaxPendingMoves.
         // With 100 FPS limit, time is never less then 10 milliseconds. (1000 / millisecs = FPS), may be changed in .INI [Settings] MinClientDelay
         mConnectionToServer->addPendingMove(theMove);
         prevTimeDelta = 0;
      }
     
      theMove->time = timeDelta;
      theMove->prepare();           // Pack and unpack the move for consistent rounding errors

      const Vector<DatabaseObject *> *gameObjects = mGameObjDatabase->findObjects_fast();

      // Visit each game object, handling moves and running its idle method
      for(S32 i = gameObjects->size() - 1; i >= 0; i--)
      {
         BfObject *obj = static_cast<BfObject *>((*gameObjects)[i]);

         if(obj->isDeleted())
            continue;

         if(obj == localPlayerShip)
         {
            obj->setCurrentMove(*theMove);
            obj->idle(BfObject::ClientIdlingLocalShip);     // on client, object is our control object
         }
         else
         {
            Move m = obj->getCurrentMove();
            m.time = timeDelta;
            obj->setCurrentMove(m);
            obj->idle(BfObject::ClientIdlingNotLocalShip);  // on client, object is not our control object
         }
      }

      if(mGameType && !isSuspended())
         mGameType->idle(BfObject::ClientIdlingNotLocalShip, timeDelta);

      if(localPlayerShip)
         getUIManager()->setListenerParams(localPlayerShip->getPos(), localPlayerShip->getVel());


      // Check to see if there are any items near the ship we need to display help for
      if(getUIManager()->isShowingInGameHelp() && localPlayerShip != NULL)
      {
         static const S32 HelpSearchRadius = 200;
         Rect searchRect = Rect(localPlayerShip->getPos(), HelpSearchRadius);
         fillVector.clear();
         mGameObjDatabase->findObjects((TestFunc)hasRelatedHelpItem, fillVector, searchRect);

         if(getUIManager()->isShowingInGameHelp())
            for(S32 i = 0; i < fillVector.size(); i++)
            {
               BfObject *obj = static_cast<BfObject *>(fillVector[i]);
               addInlineHelpItem(obj->getObjectTypeNumber(), obj->getTeam(), localPlayerShip->getTeam());
            }
      }
   }

   processDeleteList(timeDelta);                         // Delete any objects marked for deletion
   
   mNetInterface->processConnections();                  // Pass updated ship info to the server

   if(mScreenSaverTimer.update(timeDelta))               // Attempt, vainly, I'm afraid, to suppress screensavers
   {
      supressScreensaver();
      mScreenSaverTimer.reset();
   }

   mUIManager->idle(timeDelta);
}



void ClientGame::gotServerListFromMaster(const Vector<IPAddress> &serverList)
{
   getUIManager()->gotServerListFromMaster(serverList);
}


// Message relayed through master -- global chat system
void ClientGame::gotGlobalChatMessage(const char *playerNick, const char *message, bool isPrivate)
{
   getUIManager()->gotGlobalChatMessage(playerNick, message, isPrivate, false, false);
}


// Message relayed through server -- normal chat system
void ClientGame::gotChatMessage(const StringTableEntry &clientName, const StringPtr &message, bool global)
{
   if(isOnMuteList(clientName.getString()))
      return;

   const Color *color = global ? &Colors::globalChatColor : &Colors::teamChatColor;
   getUIManager()->onChatMessageReceived(*color, "%s: %s", clientName.getString(), message.getString());

   addInlineHelpItem(HowToChatItem);
}


void ClientGame::gotChatPM(const StringTableEntry &fromName, const StringTableEntry &toName, const StringPtr &message)
{
   ClientInfo *fullClientInfo = getClientInfo();

   Color color = Colors::yellow;

   if(fullClientInfo->getName() == toName && toName == fromName)      // Message sent to self
      getUIManager()->onChatMessageReceived(color, "%s: %s", toName.getString(), message.getString());

   else if(fullClientInfo->getName() == toName)                       // To this player
      getUIManager()->onChatMessageReceived(color, "from %s: %s", fromName.getString(), message.getString());

   else if(fullClientInfo->getName() == fromName)                     // From this player
      getUIManager()->onChatMessageReceived(color, "to %s: %s", toName.getString(), message.getString());

   else  
      TNLAssert(false, "Should never get here... shouldn't be able to see PM that is not from or not to you"); 
}


void ClientGame::gotVoiceChat(const StringTableEntry &from, const ByteBufferPtr &voiceBuffer)
{
   ClientInfo *clientInfo = findClientInfo(from);

   if(!clientInfo)
      return;

   if(isOnVoiceMuteList(from.getString()))
      return;

   clientInfo->playVoiceChat(voiceBuffer);
}


void ClientGame::activatePlayerMenuUi()
{
   getUIManager()->showPlayerActionMenu(PlayerActionChangeTeam);
}


void ClientGame::renderBasicInterfaceOverlay() const
{
   getUIManager()->renderBasicInterfaceOverlay();
}


void ClientGame::gameTypeIsAboutToBeDeleted()
{
   // Quit EngineerHelper when level changes, or when current GameType gets removed
   quitEngineerHelper();
}


void ClientGame::setPlayersInGlobalChat(const Vector<StringTableEntry> &playerNicks)
{
   getUIManager()->setPlayersInGlobalChat(playerNicks);
}


void ClientGame::playerJoinedGlobalChat(const StringTableEntry &playerNick)
{
   getUIManager()->playerJoinedGlobalChat(playerNick);
}


void ClientGame::playerLeftGlobalChat(const StringTableEntry &playerNick)
{
   getUIManager()->playerLeftGlobalChat(playerNick);
}


void ClientGame::sendChat(bool isGlobal, const StringPtr &message)
{
   if(getGameType())
      getGameType()->c2sSendChat(isGlobal, message);
}


void ClientGame::sendChatSTE(bool global, const StringTableEntry &message) const
{
   if(getGameType())
      getGameType()->c2sSendChatSTE(global, message);;
}


void ClientGame::sendCommand(const StringTableEntry &cmd, const Vector<StringPtr> &args)
{
   if(getGameType())
      getGameType()->c2sSendCommand(cmd, args);
}


// A new player has just joined the game; if isLocalClient is true, that new player is us!
// ClientInfo will be a RemoteClientInfo
void ClientGame::onPlayerJoined(ClientInfo *clientInfo, bool isLocalClient, bool playAlert, bool showMessage)
{
   addToClientList(clientInfo);

   // Find which client is us (could return NULL if our clientInfo hasn't yet been sent)
   mLocalRemoteClientInfo = findClientInfo(mClientInfo->getName()); 

   // Testing 1-2-3
   if(isLocalClient)
   {
      // If this assert never trips, we can get rid of the mLocalRemoteClientInfo assignment above and remove the assert.
      // If it does trip, we can remove this block, with a note of some sort.
      TNLAssert(clientInfo == findClientInfo(mClientInfo->getName()), "Not the same??");  // Added 8-Sep-2013
      mLocalRemoteClientInfo = clientInfo;
   }

   if(isLocalClient)
   {
      // Added following assert 9/8/2013 -- if this never trips, we can delete it.  If it does trip, replace "getClientInfo()->getGamesPlayed()"
      // in the level = line with getLocalRemoteClientInfo()->getGamesPlayed(), though it would be good to understand why they might differ
      TNLAssert(getLocalRemoteClientInfo()->getGamesPlayed() == getClientInfo()->getGamesPlayed(), "Should be equal");

      S32 level = getLevelThreshold(getClientInfo()->getGamesPlayed());

      // True only if we are on a levelup threshold and we haven't already seen this message
      bool showLevelUpMessage = level != NONE && 
                                !mSettings->getUserSettings(getClientInfo()->getName().getString())->levelupItemsAlreadySeen[level];

      mClientInfo->setShowLevelUpMessage(level); 

      // We want to trigger the spawn delay mechanism to carve out time to show the levelup message
      if(showLevelUpMessage)
         requestSpawnDelayed(false);
   }
         

   // Now we'll check if we need an updated scoreboard... this only needed to handle use case of user
   // holding Tab while one game transitions to the next.  Without it, ratings will be reported as 0.
   if(isLocalClient && getUIManager()->isInScoreboardMode())
      mGameType->c2sRequestScoreboardUpdates(true);

   getUIManager()->onPlayerJoined(clientInfo->getName().getString(), isLocalClient, playAlert, showMessage);

   mGameType->updateLeadingPlayerAndScore();
}


// Another player has just left the game
void ClientGame::onPlayerQuit(const StringTableEntry &name)
{
   removeFromClientList(name);
   mUIManager->onPlayerQuit(name.getString());
}


// Server tells the GameType that the game is now over.  We in turn tell the UI, which in turn notifies its helpers.
// This begins the phase of showing the post-game scoreboard.
void ClientGame::setEnteringGameOverScoreboardPhase()
{
   getUIManager()->onGameOver();
}


// Gets run when game is really and truly over, after post-game scoreboard is displayed.  Over.
void ClientGame::onGameReallyAndTrulyOver()
{
   clearClientList();                   // Erase all info we have about fellow clients

   // Kill any objects lingering in the database, such as forcefields
   getGameObjDatabase()->removeEverythingFromDatabase();    

   // Inform the UI
   getUIManager()->onGameOver();
}


void ClientGame::onGameStarting()
{
   // Shouldn't need to do this, but it will clear out forcefields lingering from level load
   getGameObjDatabase()->removeEverythingFromDatabase();
   getUIManager()->onGameStarting();

   mPlayerLevelRating = UnknownRating;
   mTotalLevelRating  = UnknownRating;
}


S16 ClientGame::getTotalLevelRating() const
{
   return mTotalLevelRating;
}


ClientGame::PersonalRating ClientGame::getPersonalLevelRating() const
{
   return mPlayerLevelRating;
}


ClientGame::PersonalRating ClientGame::toggleLevelRating()
{
   mPlayerLevelRating = getNextRating(mPlayerLevelRating);

   Thread *rateThread = new LevelDatabaseRateThread(this, LevelDatabaseRateThread::LevelRating(mPlayerLevelRating));
   rateThread->start();

   return mPlayerLevelRating;
}


void ClientGame::quitEngineerHelper()
{
   getUIManager()->quitEngineerHelper();
}


// Called by Ship::unpack() -- loadouts are transmitted via the ship object
// Data flow: Ship->ClientGame->UIManager->GameUserInterface->LoadoutIndicator
void ClientGame::newLoadoutHasArrived(const LoadoutTracker &loadout)
{
   getUIManager()->newLoadoutHasArrived(loadout);
}


void ClientGame::setActiveWeapon(U32 weaponIndex)
{
   getUIManager()->setActiveWeapon(weaponIndex);
}


bool ClientGame::isShowingDebugShipCoords()
{
   return getUIManager()->isShowingDebugShipCoords();
}


// Only happens when using connectArranged, part of punching through firewall
void ClientGame::connectionToServerRejected(const char *reason)
{
   UIManager *uiManager = getUIManager();
   uiManager->onConnectionToServerRejected(reason);

   closeConnectionToGameServer();
}


void ClientGame::setMOTD(const char *motd)
{
   getUIManager()->setMOTD(motd); 
}


// Got some new scores from the Master... better inform the UI
void ClientGame::setHighScores(const Vector<StringTableEntry> &groupNames, const Vector<string> &names, const Vector<string> &scores) const
{
   getUIManager()->setHighScores(groupNames, names, scores);
}


void ClientGame::setNeedToUpgrade(bool needToUpgrade)
{
   getUIManager()->setNeedToUpgrade(needToUpgrade);
}



void ClientGame::displayCmdChatMessage(const char *format, ...) const
{
   va_list args;
   char message[MAX_CHAT_MSG_LENGTH]; 

   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args); 
   va_end(args);

   getUIManager()->displayMessage(Colors::cmdChatColor, message);
}


void ClientGame::displayMessage(const Color &msgColor, const char *format, ...) const
{
   va_list args;
   char message[MAX_CHAT_MSG_LENGTH]; 

   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args); 
   va_end(args);
    
   getUIManager()->displayMessage(msgColor, message);
}


void ClientGame::gotPermissionsReply(ClientInfo::ClientRole role)
{
   const char *message;

   if(role == ClientInfo::RoleOwner)
      message = "You've been granted ownership permissions of this server";
   else if(role == ClientInfo::RoleAdmin)
      message = "You've been granted permission to manage players and change levels";
   else if(role == ClientInfo::RoleLevelChanger)
      message = "You've been granted permission to change levels";

   // Notify the UI that the message has arrived
   getUIManager()->gotPasswordOrPermissionsReply(this, message);
}


void ClientGame::gotWrongPassword()
{
   getUIManager()->gotPasswordOrPermissionsReply(this, "Incorrect password");
}


void ClientGame::gotPingResponse(const Address &address, const Nonce &nonce, U32 clientIdentityToken)
{
   getUIManager()->gotPingResponse(address, nonce, clientIdentityToken);
}


void ClientGame::gotQueryResponse(const Address &address, const Nonce &nonce, const char *serverName, const char *serverDescr, 
                                  U32 playerCount, U32 maxPlayers, U32 botCount, bool dedicated, bool test, bool passwordRequired)
{
   getUIManager()->gotQueryResponse(address, nonce, serverName, serverDescr, playerCount, 
                                    maxPlayers, botCount, dedicated, test, passwordRequired);
}


void ClientGame::shutdownInitiated(U16 time, const StringTableEntry &name, const StringPtr &reason, bool originator)
{
   getUIManager()->shutdownInitiated(time, name, reason, originator);
}


void ClientGame::cancelShutdown() 
{ 
   getUIManager()->cancelShutdown(); 
}


// Returns true if we have owner privs, displays error message and returns false if not
bool ClientGame::hasOwner(const char *failureMessage)
{
   if(mClientInfo->isOwner())
      return true;

   displayErrorMessage(failureMessage);
   return false;
}


// Returns true if we have admin privs, displays error message and returns false if not
bool ClientGame::hasAdmin(const char *failureMessage)
{
   if(mClientInfo->isAdmin())
      return true;
   
   displayErrorMessage(failureMessage);
   return false;
}


// Returns true if we have level change privs, displays error message and returns false if not
bool ClientGame::hasLevelChange(const char *failureMessage)
{
   if(mClientInfo->isLevelChanger())
      return true;
   
   displayErrorMessage(failureMessage);
   return false;
}


S32 ClientGame::getBotCount() const
{
   countTeamPlayers();
   return mTeamManager.getBotCount();
}


void ClientGame::gotEngineerResponseEvent(EngineerResponseEvent event)
{
   S32 energyCost = ModuleInfo::getModuleInfo(ModuleEngineer)->getPrimaryPerUseCost();
   Ship *ship = getLocalPlayerShip();

   switch(event)
   {
      case EngineerEventTurretBuilt:            // fallthrough ok
      case EngineerEventForceFieldBuilt:
         if(ship)
         {
            ship->creditEnergy(-energyCost);    // Deduct energy from engineer
            ship->resetFastRecharge();
         }
         break;

      case EngineerEventTeleporterExitBuilt:
         getUIManager()->exitHelper();
         break;

      case EngineerEventTeleporterEntranceBuilt:
         if(ship)
         {
            ship->creditEnergy(-energyCost);    // Deduct energy from engineer
            ship->resetFastRecharge();
         }

         getUIManager()->setSelectedEngineeredObject(EngineeredTeleporterExit);
         break;

      default:
         TNLAssert(false, "Do something in ClientGame::gotEngineerResponseEvent");
         break;
   }
}


// Send a message to the server that we are (or are not) busy chatting
void ClientGame::setBusyChatting(bool isBusy)
{
   GameConnection *conn = getConnectionToServer();
   if(conn)
      conn->c2sSetIsBusy(isBusy);
}


void ClientGame::addToMuteList(const string &name)
{
   mMuteList.push_back(name);
}


void ClientGame::removeFromMuteList(const string &name)
{

   for(S32 i = 0; i < mMuteList.size(); i++)
      if(mMuteList[i] == name)
      {
         mMuteList.erase_fast(i);
         return;
      }
}


bool ClientGame::isOnMuteList(const string &name)
{
   for(S32 i = 0; i < mMuteList.size(); i++)
      if(mMuteList[i] == name)
         return true;
   
   return false;
}


void ClientGame::addToVoiceMuteList(const string &name)
{
   mVoiceMuteList.push_back(name);
}


void ClientGame::removeFromVoiceMuteList(const string &name)
{

   for(S32 i = 0; i < mVoiceMuteList.size(); i++)
      if(mVoiceMuteList[i] == name)
      {
         mVoiceMuteList.erase_fast(i);
         return;
      }
}


bool ClientGame::isOnVoiceMuteList(const string &name)
{
   for(S32 i = 0; i < mVoiceMuteList.size(); i++)
      if(mVoiceMuteList[i] == name)
         return true;

   return false;
}


string ClientGame::getRemoteLevelDownloadFilename() const
{
   return mRemoteLevelDownloadFilename;
}


void ClientGame::setRemoteLevelDownloadFilename(const string &filename)
{
   mRemoteLevelDownloadFilename = filename;
}


void ClientGame::changePassword(GameConnection::ParamType type, const Vector<string> &words, bool required)
{
   GameConnection *gc = getConnectionToServer();

   bool blankPassword = words.size() < 2 || words[1] == "";

   // First, send a message to the server to tell it we want to change a password
   if(required && blankPassword)
   {
      displayErrorMessage("!!! Need to supply a password");
      return;
   }

   gc->changeParam(blankPassword ? "" : words[1].c_str(), type);


   // Now make any changes we need locally -- writing (or erasing) the pw in the INI, etc.

   if(blankPassword)       // Empty password
   {
      // Clear any saved passwords for this server
      if(type == GameConnection::LevelChangePassword)
         mSettings->forgetLevelChangePassword(gc->getServerName());

      else if(type == GameConnection::AdminPassword)
         mSettings->forgetAdminPassword(gc->getServerName());

      else if(type == GameConnection::OwnerPassword)
         mSettings->forgetOwnerPassword(gc->getServerName());
   }
   else                    // Non-empty password
   {
      // Save the password so the user need not enter it again the next time they're on this server
      if(type == GameConnection::LevelChangePassword)
         mSettings->saveLevelChangePassword(gc->getServerName(), words[1]);
         
      else if(type == GameConnection::AdminPassword)
         mSettings->saveAdminPassword(gc->getServerName(), words[1]);

      else if(type == GameConnection::OwnerPassword)
         mSettings->saveAdminPassword(gc->getServerName(), words[1]);
   }
}


// User entered password for joining a game
void ClientGame::submitServerAccessPassword(const Address &connectAddress, const char *password)
{
   mEnteredServerAccessPassword = password;
   joinRemoteGame(connectAddress, false);  // false: Not from master
}


// User entered password to get permissions on the server
bool ClientGame::submitServerPermissionsPassword(const char *password)
{
   mEnteredServerPermsPassword = password;

   GameConnection *gameConnection = getConnectionToServer();

   if(gameConnection)
   {
      gameConnection->submitPassword(password);
      return true;
   }

   return false;
}


void ClientGame::changeServerParam(GameConnection::ParamType type, const Vector<string> &words)
{
   // Concatenate all params into a single string
   string allWords = concatenate(words, 1);

   // Did the user provide a name/description? (not needed for DeleteLevel/UndeleteLevel)
   if(type != GameConnection::DeleteLevel && type != GameConnection::UndeleteLevel && allWords == "")
   {
      if(type == GameConnection::LevelDir)
         displayErrorMessage("!!! Need to supply a folder name");
      else if(type == GameConnection::ServerName)
         displayErrorMessage("!!! Need to supply a name");
      else
         displayErrorMessage("!!! Need to supply a description");
      return;
   }

   getConnectionToServer()->changeParam(allWords.c_str(), type);
}


// Returns true if it finds a case-sensitive match, or only 1 case-insensitive matches, false otherwise
bool ClientGame::checkName(const string &name)
{
   S32 potentials = 0;

   for(S32 i = 0; i < getClientCount(); i++)
   {
      StringTableEntry n = Parent::getClientInfo(i)->getName();

      if(n == name)           // Exact match
         return true;

      else if(!stricmp(n.getString(), name.c_str()))     // Case insensitive match
         potentials++;
   }

   return(potentials == 1);      // Return true if we only found exactly one potential match, false otherwise
}


void ClientGame::displayMessageBox(const StringTableEntry &title, const StringTableEntry &instr, 
                                   const Vector<StringTableEntry> &messages) const
{
   getUIManager()->displayMessageBox(title, instr, messages);
}


void ClientGame::setShowingInGameHelp(bool showing)
{
   getUIManager()->setShowingInGameHelp(showing);
}


void ClientGame::resetInGameHelpMessages()
{
   getUIManager()->resetInGameHelpMessages();
}


// Established connection is terminated.  Compare to onConnectTerminate() below.
void ClientGame::onConnectionTerminated(const Address &serverAddress, NetConnection::TerminationReason reason, const char *reasonStr, bool wasFullyConnected)
{
   // Calling clearClientList can fix cases of extra names appearing on score board when connecting to server 
   // after getting disconnected for reasons other then "SelfDisconnect"
   clearClientList();  
   unsuspendGame();

   getUIManager()->onConnectionTerminated(serverAddress, reason, reasonStr);    // Let the UI know
}


void ClientGame::onConnectionToMasterTerminated(NetConnection::TerminationReason reason, const char *reasonStr, bool wasFullyConnected)
{
   getUIManager()->onConnectionToMasterTerminated(reason, reasonStr, wasFullyConnected);    // Let the UI know

   if(reason == NetConnection::ReasonDuplicateId)
      getClientInfo()->getId()->getRandom();        // Get a different ID and retry to successfully connect to master
}


void ClientGame::runCommand(const char *command)
{
   ChatHelper::runCommand(this, command);
}


string ClientGame::getRequestedServerName()
{
   return getUIManager()->getLastSelectedServerName();
}


string ClientGame::getEnteredServerAccessPassword()
{
   return mEnteredServerAccessPassword;
}


void ClientGame::suspendGame()
{
   mTimeToSuspend.reset();
}


void ClientGame::unsuspendGame()
{
   mTimeToSuspend.clear();
   mGameSuspended = false;
}


void ClientGame::displayErrorMessage(const char *format, ...) const
{
   static char message[256];

   va_list args;

   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args);
   va_end(args);

   getUIManager()->displayErrorMessage(message);
}


void ClientGame::displaySuccessMessage(const char *format, ...) const
{
   static char message[256];

   va_list args;

   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args);
   va_end(args);

   getUIManager()->displaySuccessMessage(message);
}

// Fire keyboard event to suppress screen saver
void ClientGame::supressScreensaver()
{

#if defined(TNL_OS_WIN32) && (_WIN32_WINNT > 0x0400)     // Windows only for now, sadly...
   // _WIN32_WINNT is needed in case of compiling for old windows 98 (this code won't work for windows 98)

   // Code from Tom Revell's Caffeine screen saver suppression product

   // Build keypress
   tagKEYBDINPUT keyup;
   keyup.wVk = VK_MENU;     // Some key that GLUT doesn't recognize
   keyup.wScan = NULL;
   keyup.dwFlags = KEYEVENTF_KEYUP;
   keyup.time = NULL;
   keyup.dwExtraInfo = NULL;

   tagINPUT array[1];
   array[0].type = INPUT_KEYBOARD;
   array[0].ki = keyup;
   SendInput(1, array, sizeof(INPUT));
#endif
}


// We need to let the server know we are in cmdrs map because it needs to send extra data
void ClientGame::setUsingCommandersMap(bool usingCommandersMap)
{
   GameConnection *conn = getConnectionToServer();

   if(conn)
   {
      if(usingCommandersMap)
         conn->c2sRequestCommanderMap();
      else
         conn->c2sReleaseCommanderMap();
   }
}


UIManager *ClientGame::getUIManager() const
{
   return mUIManager;
}


// Only called my gameConnection when connection to game server is established
void ClientGame::resetCommandersMap()
{
   getUIManager()->resetCommandersMap();
}


F32 ClientGame::getCommanderZoomFraction() const
{
   return getUIManager()->getCommanderZoomFraction();
}


// Called from renderObjectiveArrow() & ship's onMouseMoved() when in commander's map
Point ClientGame::worldToScreenPoint(const Point *point, S32 canvasWidth, S32 canvasHeight) const
{
   return getUIManager()->worldToScreenPoint(point, canvasWidth, canvasHeight);
}


bool ClientGame::processPseudoItem(S32 argc, const char **argv, const string &levelFileName, GridDatabase *database, S32 id)
{
   if(!stricmp(argv[0], "BarrierMaker"))
   {
      if(argc >= 2)
      {
         WallItem *wallItem = new WallItem();  
         wallItem->initializeEditor();        // Only runs unselectVerts
        
         wallItem->processArguments(argc, argv, this);
         
         if(wallItem->getVertCount() < 2)     // Too small!  Need at least 2 points for a wall!
            delete wallItem;
         else
            addWallItem(wallItem, database);
      }
   }
   // TODO: Integrate code above with code above!!  EASY!!
   else if(!stricmp(argv[0], "BarrierMakerS") || !stricmp(argv[0], "PolyWall"))
   {
      //if(width)      // BarrierMakerS still width, though we ignore it
      //   barrier.width = F32(atof(argv[1]));
      //else           // PolyWall does not have width specified
      //   barrier.width = 1;

      if(argc >= 2)
      {
         PolyWall *polywall = new PolyWall();  
         
         S32 skipArgs = 0;
         if(!stricmp(argv[0], "BarrierMakerS"))
         {
            logprintf(LogConsumer::LogLevelError, "BarrierMakerS has been deprecated.  Please use PolyWall instead.");

            skipArgs = 1;
         }

         polywall->initializeEditor();     // Only runs unselectVerts

         polywall->processArguments(argc - skipArgs, argv + skipArgs, this);
         
         if(polywall->getVertCount() >= 2)
            addPolyWall(polywall, database);
         else
            delete polywall;
      }
   }
   else if(!stricmp(argv[0], "Robot"))
   {
      // For now, we'll just make a list of robots and associated params, and write that out when saving the level.  We'll leave robots as
      // full-fledged objects on the server (instead of pseudoItems here).
      string robotLine = "";

      for(S32 i = 0; i < argc; i++)
      {
         if(i > 0)
            robotLine.append(" ");

         robotLine.append(argv[i]);
      }

      getUIManager()->readRobotLine(robotLine);
   }
      
   else 
      return false;

   return true;
}


// Add polywall item to game
void ClientGame::addPolyWall(BfObject *polyWall, GridDatabase *database)
{
   Parent::addPolyWall(polyWall, database);
   polyWall->onGeomChanged(); 
}


// Add polywall item to game
void ClientGame::addWallItem(BfObject *wallItem, GridDatabase *database)
{
   Parent::addWallItem(wallItem, database);

   // Do we want to run onGeomChanged here instead?  If so, we can combine with addPolyWall.
   static_cast<WallItem *>(wallItem)->processEndPoints();      
}


AbstractTeam *ClientGame::getNewTeam()
{
   return new EditorTeam;
}


// Note: Can return NULL!
Ship *ClientGame::getLocalPlayerShip() const
{
   if(!mConnectionToServer)      // Needed if we get here while joining a server in cmdrs map mode
      return NULL;

   BfObject *object = mConnectionToServer->getControlObject();

   if(!object)
      return NULL;

   TNLAssert(object->getObjectTypeNumber() == PlayerShipTypeNumber, "What is this dude controlling??");

   return static_cast<Ship *>(object);
}


void ClientGame::changePlayerTeam(const StringTableEntry &playerName, S32 teamIndex) const
{
   if(!getGameType())
      return;

   getGameType()->c2sTriggerTeamChange(playerName, teamIndex);
}


void ClientGame::changeOwnTeam(S32 teamIndex) const
{
   if(!getGameType())
      return;

   getGameType()->c2sChangeTeams(teamIndex);
}


InputMode ClientGame::getInputMode()
{
   return mSettings->getInputMode();
}


void ClientGame::gotAnnouncement(const string &announcement)
{
   mUIManager->gotAnnouncement(announcement);
}


void ClientGame::addInlineHelpItem(HelpItem item) const
{
   mUIManager->addInlineHelpItem(item);
}


void ClientGame::addInlineHelpItem(U8 objectType, S32 objectTeam, S32 playerTeam) const
{
   mUIManager->addInlineHelpItem(objectType, objectTeam, playerTeam);
}


void ClientGame::removeInlineHelpItem(HelpItem item, bool markAsSeen) const
{
   mUIManager->removeInlineHelpItem(item, markAsSeen);
}


F32 ClientGame::getObjectiveArrowHighlightAlpha() const
{
   return mUIManager->getObjectiveArrowHighlightAlpha();
}


void ClientGame::toggleShowAllObjectOutlines()
{
   mShowAllObjectOutlines = !mShowAllObjectOutlines;
}


bool ClientGame::showAllObjectOutlines() const
{
   return mShowAllObjectOutlines;
}



};

