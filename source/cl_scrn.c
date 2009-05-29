/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "cl_local.h"

qboolean    scr_initialized;        // ready to draw

vrect_t     scr_vrect;      // position of render window on screen

glconfig_t  scr_glconfig;


cvar_t      *scr_viewsize;
cvar_t      *scr_centertime;
cvar_t      *scr_showpause;
cvar_t      *scr_printspeed;

cvar_t      *scr_netgraph;
cvar_t      *scr_timegraph;
cvar_t      *scr_debuggraph;
cvar_t      *scr_graphheight;
cvar_t      *scr_graphscale;
cvar_t      *scr_graphshift;
cvar_t      *scr_demobar;
cvar_t      *scr_fontvar;
cvar_t      *scr_scale;

cvar_t      *crosshair;

qhandle_t   crosshair_pic;
int         crosshair_width, crosshair_height;

qhandle_t   scr_backtile;

qhandle_t   scr_pause;
int         scr_pause_width, scr_pause_height;

qhandle_t   scr_net;
qhandle_t   scr_font;

int         scr_hudWidth;
int         scr_hudHeight;

#define STAT_MINUS      10  // num frame for '-' stats digit

static const char   sb_nums[2][11][11] = {
    { "num_0", "num_1", "num_2", "num_3", "num_4", "num_5",
    "num_6", "num_7", "num_8", "num_9", "num_minus" },
    { "anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5",
    "anum_6", "anum_7", "anum_8", "anum_9", "anum_minus" }
};

static qhandle_t    sb_pics[2][11];
static qhandle_t    sb_inventory;
static qhandle_t    sb_field;

#define ICON_WIDTH  24
#define ICON_HEIGHT 24
#define DIGIT_WIDTH 16
#define ICON_SPACE  8

void SCR_Loading_f (void);


/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

/*
==============
CL_AddNetgraph

A new packet was just parsed
==============
*/
void CL_AddNetgraph (void)
{
    int     i;
    int     in;
    int     ping;

    if (!scr_initialized)
        return;

    // if using the debuggraph for something else, don't
    // add the net lines
    if (scr_debuggraph->integer || scr_timegraph->integer)
        return;

    for (i=0 ; i<cls.netchan->dropped ; i++)
        SCR_DebugGraph (30, 0x40);

    //for (i=0 ; i<cl.surpressCount ; i++)
    //  SCR_DebugGraph (30, 0xdf);

    // see what the latency was on this packet
    in = cls.netchan->incoming_acknowledged & CMD_MASK;
    ping = cls.realtime - cl.history[in].sent;
    ping /= 30;
    if (ping > 30)
        ping = 30;
    SCR_DebugGraph (ping, 0xd0);
}


typedef struct
{
    float   value;
    int     color;
} graphsamp_t;

static  int         current;
static  graphsamp_t values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
    values[current&1023].value = value;
    values[current&1023].color = color;
    current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
    int     a, x, y, w, i, h;
    float   v;
    int     color;

    //
    // draw the graph
    //
    w = scr_glconfig.vidWidth;

    x = w-1;
    y = scr_glconfig.vidHeight;
    R_DrawFill (x, y-scr_graphheight->value,
        w, scr_graphheight->value, 8);

    for (a=0 ; a<w ; a++)
    {
        i = (current-1-a+1024) & 1023;
        v = values[i].value;
        color = values[i].color;
        v = v*scr_graphscale->value + scr_graphshift->value;
        
        if (v < 0)
            v += scr_graphheight->value * (1+(int)(-v/scr_graphheight->value));
        h = (int)v % (int)scr_graphheight->value;
        R_DrawFill (x, y - h, 1,    h, color);
        x--;
    }
}

static void SCR_DrawPercentBar( int percent ) {
    char buffer[16];
    int x, w;
    size_t len;

    scr_hudHeight -= CHAR_HEIGHT;

    w = scr_hudWidth * percent / 100;

    R_DrawFill( 0, scr_hudHeight, w, CHAR_HEIGHT, 4 );
    R_DrawFill( w, scr_hudHeight, scr_hudWidth - w, CHAR_HEIGHT, 0 );

    len = Q_scnprintf( buffer, sizeof( buffer ), "%d%%", percent );
    x = ( scr_hudWidth - len * CHAR_WIDTH ) / 2;
    R_DrawString( x, scr_hudHeight, 0, MAX_STRING_CHARS, buffer, scr_font );
}

/*
================
SCR_DrawDemoBar
================
*/
static void SCR_DrawDemoBar( void ) {
#if USE_MVD_CLIENT  
    int percent;
#endif

    if( !scr_demobar->integer ) {
        return;
    }

    if( cls.demo.playback ) {
        if( cls.demo.file_size ) {
            SCR_DrawPercentBar( cls.demo.file_percent );
        }
        return;
    }

#if USE_MVD_CLIENT  
    if( sv_running->integer != ss_broadcast ) {
        return;
    }

    if( ( percent = MVD_GetDemoPercent() ) == -1 ) {
        return;
    }

    SCR_DrawPercentBar( percent );
#endif
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

static char     scr_centerstring[MAX_STRING_CHARS];
static unsigned scr_centertime_start;   // for slow victory printing
static int      scr_center_lines;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint( const char *str ) {
    const char  *s;

    scr_centertime_start = cls.realtime;
    if( !strcmp( scr_centerstring, str ) ) {
        return;
    }

    Q_strlcpy( scr_centerstring, str, sizeof( scr_centerstring ) );

    // count the number of lines for centering
    scr_center_lines = 1;
    s = str;
    while( *s ) {
        if( *s == '\n' )
            scr_center_lines++;
        s++;
    }

    // echo it to the console
    Com_Printf( "%s\n", scr_centerstring );
    Con_ClearNotify_f();
}

void SCR_DrawCenterString( void ) {
    int y;
    float alpha;

    Cvar_ClampValue( scr_centertime, 0.3f, 10.0f );

    alpha = SCR_FadeAlpha( scr_centertime_start, scr_centertime->value * 1000, 300 );
    if( !alpha ) {
        return;
    }

    R_SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );

    y = scr_hudHeight / 4 - scr_center_lines * 8 / 2;

    SCR_DrawStringMulti( scr_hudWidth / 2, y, UI_CENTER,
        MAX_STRING_CHARS, scr_centerstring, scr_font );

    R_SetColor( DRAW_COLOR_CLEAR, NULL );
}

//============================================================================

/*
=================
SCR_CalcVrect

Sets scr_vrect, the coordinates of the rendered window
=================
*/
static void SCR_CalcVrect( void ) {
    int     size;

    // bound viewsize
    Cvar_ClampInteger( scr_viewsize, 40, 100 );
    scr_viewsize->modified = qfalse;

    size = scr_viewsize->integer;

    scr_vrect.width = scr_hudWidth * size / 100;
    scr_vrect.width &= ~7;

    scr_vrect.height = scr_hudHeight * size / 100;
    scr_vrect.height &= ~1;

    scr_vrect.x = ( scr_hudWidth - scr_vrect.width ) / 2;
    scr_vrect.y = ( scr_hudHeight - scr_vrect.height ) / 2;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f( void ) {
    Cvar_SetInteger( scr_viewsize, scr_viewsize->integer + 10, CVAR_SET_CONSOLE );
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f( void ) {
    Cvar_SetInteger( scr_viewsize, scr_viewsize->integer - 10, CVAR_SET_CONSOLE );
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed
=================
*/
static void SCR_Sky_f( void ) {
    float   rotate = 0;
    vec3_t  axis = { 0, 0, 1 };
    int     argc = Cmd_Argc();

    if( argc < 2 ) {
        Com_Printf ("Usage: sky <basename> [rotate] [axis x y z]\n");
        return;
    }

    if( argc > 2 )
        rotate = atof(Cmd_Argv(2));
    if( argc == 6 ) {
        axis[0] = atof(Cmd_Argv(3));
        axis[1] = atof(Cmd_Argv(4));
        axis[2] = atof(Cmd_Argv(5));
    }

    R_SetSky (Cmd_Argv(1), rotate, axis);
}

/*
================
SCR_TimeRefresh_f
================
*/
static void SCR_TimeRefresh_f (void) {
    int     i;
    unsigned    start, stop;
    float       time;

    if( cls.state != ca_active ) {
        Com_Printf( "No map loaded.\n" );
        return;
    }

    start = Sys_Milliseconds ();

    if (Cmd_Argc() == 2) {
        // run without page flipping
        R_BeginFrame();
        for (i=0 ; i<128 ; i++) {
            cl.refdef.viewangles[1] = i/128.0f*360.0f;
            R_RenderFrame (&cl.refdef);
        }
        R_EndFrame();
    } else {
        for (i=0 ; i<128 ; i++) {
            cl.refdef.viewangles[1] = i/128.0f*360.0f;

            R_BeginFrame();
            R_RenderFrame (&cl.refdef);
            R_EndFrame();
        }
    }

    stop = Sys_Milliseconds();
    time = (stop-start)*0.001f;
    Com_Printf ("%f seconds (%f fps)\n", time, 128.0f/time);
}


//============================================================================

/*
===============
SCR_TouchPics

Allows rendering code to cache all needed sbar graphics
===============
*/
void SCR_TouchPics( void ) {
    int     i, j;
    char    buffer[16];

    for( i = 0; i < 2; i++ )
        for( j = 0; j < 11; j++ )
            sb_pics[i][j] = R_RegisterPic( sb_nums[i][j] );

    sb_inventory = R_RegisterPic( "inventory" );
    sb_field = R_RegisterPic( "field_3" );

    if( crosshair->integer ) {
        if( crosshair->integer < 0 ) {
            Cvar_Set( "crosshair", "0" );
        }

        Q_snprintf( buffer, sizeof( buffer ), "ch%i", crosshair->integer );
        crosshair_pic = R_RegisterPic( buffer );
        R_GetPicSize( &crosshair_width, &crosshair_height, crosshair_pic );
    }
}

void SCR_ModeChanged( void ) {
    R_GetConfig( &scr_glconfig );
    IN_Activate();
#if USE_UI
    UI_ModeChanged();
#endif
}

/*
==================
SCR_RegisterMedia
==================
*/
void SCR_RegisterMedia( void ) {
    R_GetConfig( &scr_glconfig );

    scr_backtile = R_RegisterPic( "backtile" );
    scr_pause = R_RegisterPic( "pause" );
    scr_net = R_RegisterPic( "net" );
    scr_font = R_RegisterFont( scr_fontvar->string );

    R_GetPicSize( &scr_pause_width, &scr_pause_height, scr_pause );
}

static void scr_fontvar_changed( cvar_t *self ) {
    scr_font = R_RegisterFont( self->string );
}

static const cmdreg_t scr_cmds[] = {
    { "timerefresh", SCR_TimeRefresh_f },
    { "sizeup", SCR_SizeUp_f }, 
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { NULL }
};

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
    scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);
    scr_showpause = Cvar_Get ("scr_showpause", "1", 0);
    scr_centertime = Cvar_Get ("scr_centertime", "2.5", 0);
    scr_printspeed = Cvar_Get ("scr_printspeed", "8", 0);
    scr_netgraph = Cvar_Get ("netgraph", "0", 0);
    scr_timegraph = Cvar_Get ("timegraph", "0", 0);
    scr_debuggraph = Cvar_Get ("debuggraph", "0", 0);
    scr_graphheight = Cvar_Get ("graphheight", "32", 0);
    scr_graphscale = Cvar_Get ("graphscale", "1", 0);
    scr_graphshift = Cvar_Get ("graphshift", "0", 0);
    scr_demobar = Cvar_Get( "scr_demobar", "1", CVAR_ARCHIVE );
    scr_fontvar = Cvar_Get( "scr_font", "conchars", CVAR_ARCHIVE );
    scr_fontvar->changed = scr_fontvar_changed;
    scr_scale = Cvar_Get( "scr_scale", "1", CVAR_ARCHIVE );
    crosshair = Cvar_Get ("crosshair", "0", CVAR_ARCHIVE);

    Cmd_Register( scr_cmds );

    SCR_InitDraw();

    scr_glconfig.vidWidth = 640;
    scr_glconfig.vidHeight = 480;

    scr_initialized = qtrue;
}

void SCR_Shutdown( void ) {
    Cmd_Deregister( scr_cmds );

    SCR_ShutdownDraw();

    scr_initialized = qfalse;
}

/*
==============
SCR_DrawPause
==============
*/
void SCR_DrawPause( void ) {
    int     x, y;

    if( !sv_paused->integer ) {
        return;
    }
    if( cl_paused->integer != 2 ) {
        return;
    }

    if( !scr_showpause->integer ) {     // turn off for screenshots
        return;
    }

    x = ( scr_hudWidth - scr_pause_width ) / 2;
    y = ( scr_hudHeight - scr_pause_height ) / 2;
    R_DrawPic( x, y, scr_pause );
}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque( void ) {
    Con_Close();
#if USE_UI
    UI_OpenMenu( UIMENU_NONE );
#endif
    S_StopAllSounds();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque( void ) {
    Con_ClearNotify_f();
}

/*
==============
SCR_TileClear

Clear any parts of the tiled background that were drawn on last frame
==============
*/
static void SCR_TileClear( void ) {
    int top, bottom, left, right;

    //if( con.currentHeight == 1 )
    //  return;     // full screen console

    if( scr_viewsize->integer == 100 )
        return;     // full screen rendering

    top = scr_vrect.y;
    bottom = top + scr_vrect.height - 1;
    left = scr_vrect.x;
    right = left + scr_vrect.width - 1;

    
    // clear above view screen
    R_TileClear( 0, 0, scr_glconfig.vidWidth, top, scr_backtile );

    // clear below view screen
    R_TileClear( 0, bottom, scr_glconfig.vidWidth,
            scr_glconfig.vidHeight - bottom, scr_backtile );

    // clear left of view screen
    R_TileClear( 0, top, left, scr_vrect.height, scr_backtile );
    
    // clear right of view screen
    R_TileClear( right, top, scr_glconfig.vidWidth - right,
            scr_vrect.height, scr_backtile );

}

/*
===============================================================================

STAT PROGRAMS

===============================================================================
*/

#define HUD_DrawString( x, y, string ) \
    R_DrawString( x, y, 0, MAX_STRING_CHARS, string, scr_font )

#define HUD_DrawAltString( x, y, string ) \
    R_DrawString( x, y, UI_ALTCOLOR, MAX_STRING_CHARS, string, scr_font )

#define HUD_DrawCenterString( x, y, string ) \
    SCR_DrawStringEx( x, y, UI_CENTER, MAX_STRING_CHARS, string, scr_font )

#define HUD_DrawAltCenterString( x, y, string ) \
    SCR_DrawStringEx( x, y, UI_CENTER|UI_ALTCOLOR, MAX_STRING_CHARS, string, scr_font )



/*
==============
HUD_DrawNumber
==============
*/
void HUD_DrawNumber( int x, int y, int color, int width, int value ) {
    char    num[16], *ptr;
    int     l;
    int     frame;

    if( width < 1 )
        return;

    // draw number string
    if( width > 5 )
        width = 5;

    color &= 1;

    l = Q_scnprintf( num, sizeof( num ), "%i", value );
    if( l > width )
        l = width;
    x += 2 + DIGIT_WIDTH * ( width - l );

    ptr = num;
    while( *ptr && l ) {
        if( *ptr == '-' )
            frame = STAT_MINUS;
        else
            frame = *ptr - '0';

        R_DrawPic( x, y, sb_pics[color][frame] );
        x += DIGIT_WIDTH;
        ptr++;
        l--;
    }
}

/*
================
CL_DrawInventory
================
*/
#define DISPLAY_ITEMS   17

void SCR_DrawInventory( void ) {
    int     i;
    int     num, selected_num, item;
    int     index[MAX_ITEMS];
    char    string[MAX_STRING_CHARS];
    int     x, y;
    char    *bind;
    int     selected;
    int     top;

    selected = cl.frame.ps.stats[STAT_SELECTED_ITEM];

    num = 0;
    selected_num = 0;
    for( i = 0; i < MAX_ITEMS; i++ ) {
        if( i == selected ) {
            selected_num = num;
        }
        if( cl.inventory[i] ) {
            index[num++] = i;
        }
    }

    // determine scroll point
    top = selected_num - DISPLAY_ITEMS / 2;
    clamp( top, 0, num - DISPLAY_ITEMS );

    x = ( scr_hudWidth - 256 ) / 2;
    y = ( scr_hudHeight - 240 ) / 2;

    R_DrawPic( x, y + 8, sb_inventory );
    y += 24;
    x += 24;

    HUD_DrawString( x, y, "hotkey ### item" );
    y += CHAR_HEIGHT;

    HUD_DrawString( x, y, "------ --- ----" );
    y += CHAR_HEIGHT;

    for( i = top; i < num && i < top + DISPLAY_ITEMS; i++ ) {
        item = index[i];
        // search for a binding
        Q_concat( string, sizeof( string ),
            "use ", cl.configstrings[CS_ITEMS + item], NULL );
        bind = Key_GetBinding( string );

        Q_snprintf( string, sizeof( string ), "%6s %3i %s",
            bind, cl.inventory[item], cl.configstrings[CS_ITEMS + item] );
        
        if( item != selected ) {
            HUD_DrawAltString( x, y, string );
        } else {    // draw a blinky cursor by the selected item
            HUD_DrawString( x, y, string );
            if( ( cls.realtime >> 8 ) & 1 ) {
                R_DrawChar( x - CHAR_WIDTH, y, 0, 15, scr_font );
            }
        }
        
        y += CHAR_HEIGHT;
    }
}

/*
================
SCR_ExecuteLayoutString 

================
*/
void SCR_ExecuteLayoutString( const char *s ) {
    char    buffer[MAX_QPATH];
    int     x, y;
    int     value;
    char    *token;
    int     width;
    int     index;
    clientinfo_t    *ci;

    if( !s[0] )
        return;

    x = 0;
    y = 0;
    width = 3;

    while( s ) {
        token = COM_Parse( &s );
        if( token[2] == 0 ) {
            if( token[0] == 'x' ) {
                if( token[1] == 'l' ) {
                    token = COM_Parse( &s );
                    x = atoi( token );
                    continue;
                }

                if( token[1] == 'r' ) {
                    token = COM_Parse( &s );
                    x = scr_hudWidth + atoi( token );
                    continue;
                }

                if( token[1] == 'v' ) {
                    token = COM_Parse( &s );
                    x = scr_hudWidth / 2 - 160 + atoi( token );
                    continue;
                }
            }

            if( token[0] == 'y' ) {
                if( token[1] == 't' ) {
                    token = COM_Parse( &s );
                    y = atoi( token );
                    continue;
                }

                if( token[1] == 'b' ) {
                    token = COM_Parse( &s );
                    y = scr_hudHeight + atoi( token );
                    continue;
                }

                if( token[1] == 'v' ) {
                    token = COM_Parse( &s );
                    y = scr_hudHeight / 2 - 120 + atoi( token );
                    continue;
                }
            }
        }

        if( !strcmp( token, "pic" ) ) { 
            // draw a pic from a stat number
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            if( value < 0 || value >= MAX_IMAGES ) {
                Com_Error( ERR_DROP, "%s: invalid pic index", __func__ );
            }
            token = cl.configstrings[CS_IMAGES + value];
            if( token[0] ) {
                R_DrawPic( x, y, R_RegisterPic( token ) );
            }
            continue;
        }

        if( !strcmp( token, "client" ) ) {  
            // draw a deathmatch client block
            int     score, ping, time;

            token = COM_Parse( &s );
            x = scr_hudWidth / 2 - 160 + atoi( token );
            token = COM_Parse( &s );
            y = scr_hudHeight / 2 - 120 + atoi( token );

            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_CLIENTS ) {
                Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse( &s );
            score = atoi( token );

            token = COM_Parse( &s );
            ping = atoi( token );

            token = COM_Parse( &s );
            time = atoi( token );

            HUD_DrawString( x + 32, y, ci->name );
            Q_snprintf( buffer, sizeof( buffer ), "Score: %i", score ); 
            HUD_DrawString( x + 32, y + CHAR_HEIGHT, buffer );
            Q_snprintf( buffer, sizeof( buffer ), "Ping:  %i", ping ); 
            HUD_DrawString( x + 32, y + 2 * CHAR_HEIGHT, buffer );
            Q_snprintf( buffer, sizeof( buffer ), "Time:  %i", time ); 
            HUD_DrawString( x + 32, y + 3 * CHAR_HEIGHT, buffer );

            if( !ci->icon ) {
                ci = &cl.baseclientinfo;
            }
            R_DrawPic( x, y, ci->icon );
            continue;
        }

        if( !strcmp( token, "ctf" ) ) { 
            // draw a ctf client block
            int     score, ping;

            token = COM_Parse( &s );
            x = scr_hudWidth / 2 - 160 + atoi( token );
            token = COM_Parse( &s );
            y = scr_hudHeight / 2 - 120 + atoi( token );

            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_CLIENTS ) {
                Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse( &s );
            score = atoi( token );

            token = COM_Parse( &s );
            ping = atoi( token );
            if( ping > 999 )
                ping = 999;

            Q_snprintf( buffer, sizeof( buffer ), "%3d %3d %-12.12s",
                score, ping, ci->name );
            if( value == cl.frame.clientNum ) {
                HUD_DrawAltString( x, y, buffer );
            } else {
                HUD_DrawString( x, y, buffer );
            }
            continue;
        }

        if( !strcmp( token, "picn" ) ) {    
            // draw a pic from a name
            token = COM_Parse( &s );
            R_DrawPic( x, y, R_RegisterPic( token ) );
            continue;
        }

        if( !strcmp( token, "num" ) ) { 
            // draw a number
            token = COM_Parse( &s );
            width = atoi( token );
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            HUD_DrawNumber( x, y, 0, width, value );
            continue;
        }

        if( !strcmp( token, "hnum" ) ) {    
            // health number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_HEALTH];
            if( value > 25 )
                color = 0;  // green
            else if( value > 0 )
                color = ( cl.frame.number >> 2 ) & 1;       // flash
            else
                color = 1;

            if( cl.frame.ps.stats[STAT_FLASHES] & 1 )
                R_DrawPic( x, y, sb_field );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "anum" ) ) {    
            // ammo number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_AMMO];
            if( value > 5 )
                color = 0;  // green
            else if( value >= 0 )
                color = ( cl.frame.number >> 2 ) & 1;       // flash
            else
                continue;   // negative number = don't show

            if( cl.frame.ps.stats[STAT_FLASHES] & 4 )
                R_DrawPic( x, y, sb_field );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "rnum" ) ) {    
            // armor number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_ARMOR];
            if( value < 1 )
                continue;

            color = 0;  // green

            if( cl.frame.ps.stats[STAT_FLASHES] & 2 )
                R_DrawPic( x, y, sb_field );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "stat_string" ) ) {
            token = COM_Parse( &s );
            index = atoi( token );
            if( index < 0 || index >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            index = cl.frame.ps.stats[index];
            if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
                Com_Error( ERR_DROP, "%s: invalid string index", __func__ );
            }
            HUD_DrawString( x, y, cl.configstrings[index] );
            continue;
        }

        if( !strcmp( token, "cstring" ) ) {
            token = COM_Parse( &s );
            HUD_DrawCenterString( x + 320 / 2, y, token );
            continue;
        }

        if( !strcmp( token, "cstring2" ) ) {
            token = COM_Parse( &s );
            HUD_DrawAltCenterString( x + 320 / 2, y, token );
            continue;
        }

        if( !strcmp( token, "string" ) ) {
            token = COM_Parse( &s );
            HUD_DrawString( x, y, token );
            continue;
        }

        if( !strcmp( token, "string2" ) ) {
            token = COM_Parse( &s );
            HUD_DrawAltString( x, y, token );
            continue;
        }

        if( !strcmp( token, "if" ) ) {
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            if( !value ) {  // skip to endif
                while( strcmp( token, "endif" ) ) {
                    token = COM_Parse( &s );
                    if( !s ) {
                        break;
                    }
                }
            }
            continue;
        }
    }
}

static void SCR_DrawActiveFrame( void ) {
    float scale;

    scr_hudHeight = scr_glconfig.vidHeight;
    scr_hudWidth = scr_glconfig.vidWidth;

    SCR_DrawDemoBar();

    SCR_CalcVrect();

    // clear any dirty part of the background
    SCR_TileClear();

    // draw 3D game view
    V_RenderView();

    Cvar_ClampValue( scr_scale, 1, 9 );

    scale = 1.0f / scr_scale->value;
    R_SetScale( &scale );

    scr_hudHeight *= scale;
    scr_hudWidth *= scale;

    // draw all 2D elements
    SCR_Draw2D();

    R_SetScale( NULL );
}

//=======================================================

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
    static int recursive;

    if( !scr_initialized )
        return;             // not initialized yet

    if( recursive > 1 ) {
        Com_Error( ERR_FATAL, "%s: recursively called", __func__ );
    }

    recursive++;

    R_BeginFrame();
    
    switch( cls.state ) {
    case ca_disconnected:
        // make sure at least fullscreen console or main menu is up
        if( !( cls.key_dest & (KEY_MENU|KEY_CONSOLE) ) ) {
            Key_SetDest( cls.key_dest | KEY_CONSOLE );
            Con_RunConsole();
        }

#if USE_UI
        if( UI_IsTransparent() )
#endif
        {
            R_DrawFill( 0, 0, scr_glconfig.vidWidth,
                scr_glconfig.vidHeight, 0 );
        }

#if USE_UI
        // draw main menu
        UI_Draw( cls.realtime );
#endif
        break;

    case ca_challenging:
    case ca_connecting:
    case ca_connected:
    case ca_loading:
    case ca_precached:
#if USE_UI
        // make sure main menu is down
        if( cls.key_dest & KEY_MENU ) {
            UI_OpenMenu( UIMENU_NONE );
        }
#endif

        // draw loading screen
        SCR_DrawLoading();
        break;

    case ca_active:
#if USE_UI
        if( UI_IsTransparent() )
#endif
        {
            // do 3D refresh drawing
            SCR_DrawActiveFrame();
        }

#if USE_UI
        // draw ingame menu
        UI_Draw( cls.realtime );
#endif
        break;

    default:
        Com_Error( ERR_FATAL, "%s: bad cls.state", __func__ );
        break;
    }

    Con_DrawConsole();

    if( scr_timegraph->integer )
        SCR_DebugGraph( cls.frametime*300, 0 );

    if( scr_debuggraph->integer || scr_timegraph->integer || scr_netgraph->integer ) {
        SCR_DrawDebugGraph();
    }

    R_EndFrame();

    recursive--;
}


