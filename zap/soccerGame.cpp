//-----------------------------------------------------------------------------------
//
// bitFighter - A multiplayer vector graphics space game
// Based on Zap demo released for Torque Network Library by GarageGames.com
//
// Derivative work copyright (C) 2008 Chris Eykamp
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

#include "soccerGame.h"
#include "UIGame.h"
#include "sfx.h"
#include "gameNetInterface.h"
#include "ship.h"
#include "gameObjectRender.h"
#include "goalZone.h"
#include "../glut/glutInclude.h"

namespace Zap
{

class Ship;
class SoccerBallItem;

TNL_IMPLEMENT_NETOBJECT(SoccerGameType);

TNL_IMPLEMENT_NETOBJECT_RPC(SoccerGameType, s2cSoccerScoreMessage,
   (U32 msgIndex, StringTableEntry clientName, RangedU32<0, GameType::gMaxTeamCount> teamIndex), (msgIndex, clientName, teamIndex),
   NetClassGroupGameMask, RPCGuaranteedOrdered, RPCToGhost, 0)
{
   S32 teamIndexAdjusted = (S32) teamIndex + GameType::gFirstTeamNumber;      // Before calling this RPC, we subtracted gFirstTeamNumber, so we need to add it back here...
   string msg;
   SFXObject::play(SFXFlagCapture);

   // Compose the message
   if(msgIndex == SoccerMsgScoreGoal)
   {
      if(teamIndexAdjusted >= 0)
         msg = string(clientName.getString()) + " scored a goal on team " + string(mTeams[teamIndexAdjusted].name.getString());
      else if(teamIndexAdjusted == -1)
         msg = string(clientName.getString()) + " scored a goal on a neutral goal!";
      else if(teamIndexAdjusted == -2)
         msg = string(clientName.getString()) + " scored a goal on a hostile goal (for negative points!)";
      else
         msg = string(clientName.getString()) + " scored a goal on an unknown goal!";
   }
   else if(msgIndex == SoccerMsgScoreOwnGoal)
   {
      if(clientName.isNull())    // Unknown player scored
      {
         if(teamIndexAdjusted >= 0)
            msg = "A goal was scored on team " + string(mTeams[teamIndexAdjusted].name.getString());
         else if(teamIndexAdjusted == -1)
            msg = "A goal was scored on a neutral goal!";
         else if(teamIndexAdjusted == -2)
            msg = "A goal was scored on a hostile goal!";
         else
            msg = "A goal was scored on an unknown goal!";
      }
      else
         msg = string(clientName.getString()) + " scored an own-goal, losing their team a point!";
   }
   // Print the message
   gGameUserInterface.displayMessage(Color(0.6f, 1.0f, 0.8f), msg.c_str());
}

void SoccerGameType::addZone(GoalZone *theZone)
{
   mGoals.push_back(theZone);
}

void SoccerGameType::setBall(SoccerBallItem *theBall)
{
   mBall = theBall;
}


void SoccerGameType::scoreGoal(StringTableEntry playerName, S32 goalTeamIndex)
{
   ClientRef *cl = findClientRef(playerName);		// Need a better mechanism for this... time for shipIDs?
   S32 scoringTeam = -1;
   if(cl)
      scoringTeam = cl->teamId;

   if(scoringTeam == -1 || scoringTeam == goalTeamIndex)    // Own-goal
   {
      updateScore(cl, ScoreGoalOwnTeam);
      s2cSoccerScoreMessage(SoccerMsgScoreOwnGoal, playerName, (U32) (goalTeamIndex - gFirstTeamNumber));   // Subtract gFirstTeamNumber to fit goalTeamIndex into a neat RangedU32 container
   }
   else	   // Goal on someone else's goal
   {
      if(goalTeamIndex == -2)
         updateScore(cl, ScoreGoalHostileTeam);
      else
         updateScore(cl, ScoreGoalEnemyTeam);

      s2cSoccerScoreMessage(SoccerMsgScoreGoal, playerName, (U32) (goalTeamIndex - gFirstTeamNumber));      // See comment above
   }
}

void SoccerGameType::renderInterfaceOverlay(bool scoreboardVisible)
{
   Parent::renderInterfaceOverlay(scoreboardVisible);
   Ship *u = dynamic_cast<Ship *>(gClientGame->getConnectionToServer()->getControlObject());
   if(!u)
      return;

   for(S32 i = 0; i < mGoals.size(); i++)
   {
      if(mGoals[i]->getTeam() != u->getTeam())
         renderObjectiveArrow(mGoals[i], getTeamColor(mGoals[i]->getTeam()));
   }
   if(mBall.isValid())
      renderObjectiveArrow(mBall, getTeamColor(-1));
}


// What does a particular scoring event score?
S32 SoccerGameType::getEventScore(ScoringGroup scoreGroup, ScoringEvent scoreEvent, S32 data)
{
   if(scoreGroup == TeamScore)
   {
      switch(scoreEvent)
      {
         case KillEnemy:
            return 0;
         case KillSelf:
            return 0;
         case KillTeammate:
            return 0;
         case KillEnemyTurret:
            return 0;
         case KillOwnTurret:
            return 0;
         case ScoreGoalEnemyTeam:
            return 1;
         case ScoreGoalOwnTeam:
            return -1;
         case ScoreGoalHostileTeam:
            return -1;
         default:
            return naScore;
      }
   }
   else  // scoreGroup == IndividualScore
   {
      switch(scoreEvent)
      {
         case KillEnemy:
            return 1;
         case KillSelf:
            return -1;
         case KillTeammate:
            return 0;
         case KillEnemyTurret:
            return 1;
         case KillOwnTurret:
            return -1;
		   case ScoreGoalEnemyTeam:
			   return 5;
		   case ScoreGoalOwnTeam:
			   return -5;
         case ScoreGoalHostileTeam:
			   return -5;
         default:
            return naScore;
      }
   }
}


TNL_IMPLEMENT_NETOBJECT(SoccerBallItem);

// Constructor
SoccerBallItem::SoccerBallItem(Point pos) : Item(pos, true, SoccerBallItem::radius, 4)
{
   mObjectTypeMask |= CommandMapVisType | TurretTargetType;
   mNetFlags.set(Ghostable);
   initialPos = pos;
}

void SoccerBallItem::processArguments(S32 argc, const char **argv)
{
   Parent::processArguments(argc, argv);
   initialPos = mMoveState[ActualState].pos;
}


void SoccerBallItem::onAddedToGame(Game *theGame)
{
   // Make soccer ball always visible
   if(!isGhost())
      setScopeAlways();

   // Make soccer ball only visible when in scope
   //if(!isGhost())
   //   theGame->getGameType()->addItemOfInterest(this);

   ((SoccerGameType *) theGame->getGameType())->setBall(this);
}

void SoccerBallItem::renderItem(Point pos)
{
   renderSoccerBall(pos);
}

void SoccerBallItem::idle(GameObject::IdleCallPath path)
{
   if(mSendHomeTimer.update(mCurrentMove.time))
      sendHome();
   else if(mSendHomeTimer.getCurrent())
   {
      F32 accelFraction = 1 - (0.98 * mCurrentMove.time * 0.001f);

      mMoveState[ActualState].vel *= accelFraction;
      mMoveState[RenderState].vel *= accelFraction;
   }
   Parent::idle(path);
}

void SoccerBallItem::damageObject(DamageInfo *theInfo)
{
   // compute impulse direction
   Point dv = theInfo->impulseVector - mMoveState[ActualState].vel;
   Point iv = mMoveState[ActualState].pos - theInfo->collisionPoint;
   iv.normalize();
   mMoveState[ActualState].vel += iv * dv.dot(iv) * 0.3;
   if(theInfo->damagingObject && (theInfo->damagingObject->getObjectTypeMask() & ShipType))
   {
      lastPlayerTouch = (dynamic_cast<Ship *>(theInfo->damagingObject))->mPlayerName;
   }
}

void SoccerBallItem::sendHome()
{
   mMoveState[ActualState].vel = mMoveState[RenderState].vel = Point();
   mMoveState[ActualState].pos = mMoveState[RenderState].pos = initialPos;
   setMaskBits(PositionMask);
   updateExtent();
}

bool SoccerBallItem::collide(GameObject *hitObject)
{
   if(isGhost())
      return true;

   if(hitObject->getObjectTypeMask() & ShipType)
   {
      lastPlayerTouch = (dynamic_cast<Ship *>(hitObject))->mPlayerName;
   }
   else
   {
      GoalZone *goal = dynamic_cast<GoalZone *>(hitObject);

      if(goal && !mSendHomeTimer.getCurrent())
      {
         SoccerGameType *g = (SoccerGameType *) getGame()->getGameType();
         g->scoreGoal(lastPlayerTouch, goal->getTeam());
         mSendHomeTimer.reset(1500);
      }
   }
   return true;
}

};
