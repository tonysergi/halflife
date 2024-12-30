/***
*
*	Copyright (c) 1996-2001, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
/*

===== lights.cpp ========================================================

  spawn and think functions for editor-placed lights

*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include <vector>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <string>



class CLight : public CPointEntity
{
public:
	virtual void	KeyValue( KeyValueData* pkvd ); 
	virtual void	Spawn( void );
	void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );

	
	static	TYPEDESCRIPTION m_SaveData[];

protected:
	enum ExPatternState_e
	{
		EXPATTERN_NONE,
		EXPATTERN_START,
		EXPATTERN_ON,
		EXPATTERN_STOP,
		//special state that is only set specifically when ending a stop animation
		EXPATTERN_STOPPED,
	};


	void SetExPatternState( ExPatternState_e state );
	void EXPORT ExPatternThink();

	//returns true if the pattern reached the end.
	bool AnimatePattern( std::vector<char>& pattern );

	//filter a pattern string to ensure validity, and store into the vector.
	static void FilterPattern( const char* szPattern, std::vector<char>& out );
	void GetPattern( ExPatternState_e state, std::vector<char>& pattern );
private:
	int					m_iStyle;
	int					m_iszPattern;

	//Tony; expanded custom lightstyle updating.
	// need string_t versions of these for save/restore.
	int					m_iszPatternStart;
	int					m_iszPatternStop;
	int					m_ExPatternState;
	std::vector<char>	m_ExPatternStart;
	std::vector<char>	m_ExPatternStop;
	std::vector<char>	m_ExPattern;
	//current index in the pattern;
	int					m_iExPatternIdx;

};
LINK_ENTITY_TO_CLASS( light, CLight );

TYPEDESCRIPTION	CLight::m_SaveData[] = 
{
	DEFINE_FIELD( CLight, m_iStyle, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_iszPattern, FIELD_STRING ),

	//Tony; expanded lightstyles
	DEFINE_FIELD( CLight, m_iszPatternStart, FIELD_STRING ),
	DEFINE_FIELD( CLight, m_iszPatternStop, FIELD_STRING ),
	DEFINE_FIELD( CLight, m_ExPatternState, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_iExPatternIdx, FIELD_INTEGER ),
};

//Tony; can't use the macro anymore, because we need to re-populate the pattern vectors on reload.
//IMPLEMENT_SAVERESTORE( CLight, CPointEntity );
int CLight::Save( CSave& save )
{
	if ( !CPointEntity::Save( save ) )
		return 0;
	return save.WriteFields( "CLight", this, m_SaveData, ARRAYSIZE( m_SaveData ) );
}
int CLight::Restore( CRestore& restore )
{
	if ( !CPointEntity::Restore( restore ) )
		return 0;
	
	int result = restore.ReadFields( "CLight", this, m_SaveData, ARRAYSIZE( m_SaveData ) );
	if ( result > 0 )
	{
		//Tony; populate the pattern vectors, if pattern strings exist.
		if ( m_iszPatternStart != iStringNull )
		{
			FilterPattern( STRING( m_iszPatternStart ), m_ExPatternStart );
		}

		if ( m_iszPatternStop != iStringNull )
		{
			FilterPattern( STRING( m_iszPatternStop ), m_ExPatternStop );
		}

		if ( m_iszPattern != iStringNull )
		{
			FilterPattern( STRING( m_iszPattern ), m_ExPattern );
		}
	}

	return result;
}


//
// Cache user-entity-field values until spawn is called.
//
void CLight :: KeyValue( KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "style"))
	{
		m_iStyle = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "pitch"))
	{
		pev->angles.x = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "pattern"))
	{
		m_iszPattern = ALLOC_STRING( pkvd->szValue );

		//Tony; copy the regular pattern to the vector now.
		CLight::FilterPattern( pkvd->szValue, m_ExPattern );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq( pkvd->szKeyName, "pattern_start"))
	{
		m_iszPatternStart = ALLOC_STRING( pkvd->szValue );
		CLight::FilterPattern( pkvd->szValue, m_ExPatternStart );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "pattern_stop" ) )
	{
		m_iszPatternStop = ALLOC_STRING( pkvd->szValue );
		CLight::FilterPattern( pkvd->szValue, m_ExPatternStop );
		pkvd->fHandled = TRUE;
	}
	else
	{
		CPointEntity::KeyValue( pkvd );
	}
}

/*QUAKED light (0 1 0) (-8 -8 -8) (8 8 8) LIGHT_START_OFF
Non-displayed light.
Default light value is 300
Default style is 0
If targeted, it will toggle between on or off.
*/

void CLight :: Spawn( void )
{
	if (FStringNull(pev->targetname))
	{       
		// inert light
		REMOVE_ENTITY(ENT(pev));
		return;
	}

	if (m_iStyle >= 32)
	{
		//Ex Pattern.
		bool bUseEXPattern = FBitSet( pev->spawnflags, SF_LIGHT_EXPATTERN ) && (m_iszPattern != iStringNull || m_iszPatternStart != iStringNull);

		if (FBitSet(pev->spawnflags, SF_LIGHT_START_OFF))
		{
			LIGHT_STYLE( m_iStyle, "a" );
		}
		else if (m_iszPattern || bUseEXPattern )
		{
			//no expattern (flag or start sequence); just do default behavior, set the style and get out.
			if ( !bUseEXPattern )
			{
				LIGHT_STYLE( m_iStyle, (char*)STRING( m_iszPattern ) );
				return;
			}
			
			//if we have a startup pattern; override to start with that, instead.
			if ( m_iszPatternStart != iStringNull )
			{
				SetExPatternState( ExPatternState_e::EXPATTERN_START );
				return;
			}

			// otherwise just enter the normal on state, and tick the pattern.
			SetExPatternState( ExPatternState_e::EXPATTERN_ON );
		}
		else
		{
			LIGHT_STYLE( m_iStyle, "m" );
		}
	}
}


void CLight :: Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (m_iStyle >= 32)
	{
		if ( !ShouldToggle( useType, !FBitSet(pev->spawnflags, SF_LIGHT_START_OFF) ) )
			return;

		if (FBitSet(pev->spawnflags, SF_LIGHT_START_OFF))
		{
			//if we have a startup pattern; activate the startup.
			if ( m_iszPatternStart != iStringNull )
			{
				SetExPatternState( ExPatternState_e::EXPATTERN_START );
				ClearBits( pev->spawnflags, SF_LIGHT_START_OFF );
				return;
			}

			//custom pattern check.
			if (m_iszPattern)
			{
				//explicitly using expattern.
				if ( FBitSet(pev->spawnflags, SF_LIGHT_EXPATTERN ) )
				{
					SetExPatternState( ExPatternState_e::EXPATTERN_ON );
					ClearBits( pev->spawnflags, SF_LIGHT_START_OFF );
					return;
				}

				LIGHT_STYLE( m_iStyle, (char*)STRING( m_iszPattern ) );
			}
			else
			{
				LIGHT_STYLE( m_iStyle, "m" );
			}

			ClearBits(pev->spawnflags, SF_LIGHT_START_OFF);
			SetExPatternState( ExPatternState_e::EXPATTERN_NONE );
		}
		else
		{
			//start a shutdown pattern.
			if ( m_iszPatternStop != iStringNull )
			{
				SetExPatternState( ExPatternState_e::EXPATTERN_STOP );
				SetBits( pev->spawnflags, SF_LIGHT_START_OFF );
				return;
			}

			LIGHT_STYLE(m_iStyle, "a");
			SetBits(pev->spawnflags, SF_LIGHT_START_OFF);
			SetExPatternState( ExPatternState_e::EXPATTERN_NONE );
		}
	}
}

void CLight::SetExPatternState( ExPatternState_e state )
{
	m_ExPatternState = state;
	m_iExPatternIdx = 0; //always start at the beginning of the cycle.

	if ( state == ExPatternState_e::EXPATTERN_NONE || state == ExPatternState_e::EXPATTERN_STOPPED )
	{
		SetThink( nullptr );
		
		if ( state == ExPatternState_e::EXPATTERN_STOPPED )
		{
			// special case; finished a shutdown animation
			// make sure our style is off ;)
			LIGHT_STYLE( m_iStyle, "a" );
		}
		return;
	}

	//start thinking for the new state!
	SetThink( &CLight::ExPatternThink );
	pev->nextthink = gpGlobals->time + 0.1;
}

void CLight::ExPatternThink()
{
	std::vector<char> curPattern;
	GetPattern( (ExPatternState_e)m_ExPatternState, curPattern );

	//animate the style, and check if finished.
	bool bFinished = AnimatePattern( curPattern );
	if ( bFinished )
	{
		//if we're in on (looped state) just reset the pattern index.
		//if we're doing a "start" animation, transition to on.
		// if we're doing a "stop" animation, return to off
		switch ( m_ExPatternState )
		{
		case ExPatternState_e::EXPATTERN_ON: //just fallthrough, it will repeat, etc.
		case ExPatternState_e::EXPATTERN_START: SetExPatternState( ExPatternState_e::EXPATTERN_ON ); break;
		case ExPatternState_e::EXPATTERN_STOP: SetExPatternState( ExPatternState_e::EXPATTERN_STOPPED ); break;
		}
	}

	pev->nextthink = gpGlobals->time + 0.10f; //10fps
}

bool CLight::AnimatePattern( std::vector<char>& pattern )
{
	if ( m_iExPatternIdx < (int)pattern.size() )
	{
		std::string styleBuf = std::string( 1, pattern[m_iExPatternIdx++] );
		g_engfuncs.pfnLightStyle( m_iStyle, (char*)styleBuf.c_str() );
		return false;
	}

	return true;
}

void CLight::GetPattern( ExPatternState_e state, std::vector<char>& pattern )
{
	pattern.clear();

	switch( state )
	{
	case ExPatternState_e::EXPATTERN_START: pattern = m_ExPatternStart; break;
	case ExPatternState_e::EXPATTERN_ON: pattern = m_ExPattern; break;
	case ExPatternState_e::EXPATTERN_STOP: pattern = m_ExPatternStop; break;
	}
}

void CLight::FilterPattern( const char* szPattern, std::vector<char>& out )
{
	out.clear();
	size_t len = std::strlen( szPattern );
	out.reserve( len );

	std::copy_if( szPattern, szPattern + len, std::back_inserter( out ), []( char c ) {
		return c >= 'a' && c <= 'z';
		} );
}

//
// shut up spawn functions for new spotlights
//
LINK_ENTITY_TO_CLASS( light_spot, CLight );


class CEnvLight : public CLight
{
public:
	void	KeyValue( KeyValueData* pkvd ); 
	void	Spawn( void );
};

LINK_ENTITY_TO_CLASS( light_environment, CEnvLight );

void CEnvLight::KeyValue( KeyValueData* pkvd )
{
	if (FStrEq(pkvd->szKeyName, "_light"))
	{
		int r, g, b, v, j;
		char szColor[64];
		j = sscanf( pkvd->szValue, "%d %d %d %d\n", &r, &g, &b, &v );
		if (j == 1)
		{
			g = b = r;
		}
		else if (j == 4)
		{
			r = r * (v / 255.0);
			g = g * (v / 255.0);
			b = b * (v / 255.0);
		}

		// simulate qrad direct, ambient,and gamma adjustments, as well as engine scaling
		r = pow( r / 114.0, 0.6 ) * 264;
		g = pow( g / 114.0, 0.6 ) * 264;
		b = pow( b / 114.0, 0.6 ) * 264;

		pkvd->fHandled = TRUE;
		sprintf( szColor, "%d", r );
		CVAR_SET_STRING( "sv_skycolor_r", szColor );
		sprintf( szColor, "%d", g );
		CVAR_SET_STRING( "sv_skycolor_g", szColor );
		sprintf( szColor, "%d", b );
		CVAR_SET_STRING( "sv_skycolor_b", szColor );
	}
	else
	{
		CLight::KeyValue( pkvd );
	}
}


void CEnvLight :: Spawn( void )
{
	char szVector[64];
	UTIL_MakeAimVectors( pev->angles );

	sprintf( szVector, "%f", gpGlobals->v_forward.x );
	CVAR_SET_STRING( "sv_skyvec_x", szVector );
	sprintf( szVector, "%f", gpGlobals->v_forward.y );
	CVAR_SET_STRING( "sv_skyvec_y", szVector );
	sprintf( szVector, "%f", gpGlobals->v_forward.z );
	CVAR_SET_STRING( "sv_skyvec_z", szVector );

	CLight::Spawn( );
}
