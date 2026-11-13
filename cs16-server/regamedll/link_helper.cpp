// link_helper.cpp - tabla de exportaciones del servidor (ReGameDLL_CS) para el enlace estatico.
//
// En la Wii no hay dlopen: el motor busca las funciones del servidor por nombre en
// lib_hl_exports[] (ver engine/generated_library_tables.h y engine/platform/misc/lib_static.c).
// El servidor de Half-Life (hlsdk-portable/dlls/link_helper.cpp) trae su propia tabla;
// ReGameDLL no, asi que la tabla va aqui.
//
// Cada LINK_ENTITY_TO_CLASS(nombre, Clase) de ReGameDLL genera una funcion extern "C" nombre(entvars_t *).
// Si se anaden/quitan entidades en ReGameDLL hay que reflejarlo aqui: el nombre del mapa
// (classname) solo se resuelve si esta en esta lista.

#include <stddef.h>

typedef struct table_s { const char *name; void *pointer; } table_t;

extern "C" {
void GiveFnptrsToDll( void );
void GetEntityAPI( void );
void GetNewDLLFunctions( void );
void Server_GetBlendingInterface( void );
void DelayedUse( void );
void ambient_generic( void );
void ammo_338magnum( void );
void ammo_357sig( void );
void ammo_45acp( void );
void ammo_50ae( void );
void ammo_556nato( void );
void ammo_556natobox( void );
void ammo_57mm( void );
void ammo_762nato( void );
void ammo_9mm( void );
void ammo_buckshot( void );
void armoury_entity( void );
void beam( void );
void bodyque( void );
void bot( void );
void button_target( void );
void cycler( void );
void cycler_prdroid( void );
void cycler_sprite( void );
void cycler_weapon( void );
void cycler_wreckage( void );
void env_beam( void );
void env_beverage( void );
void env_blood( void );
void env_bombglow( void );
void env_bubbles( void );
void env_debris( void );
void env_explosion( void );
void env_fade( void );
void env_fog( void );
void env_funnel( void );
void env_global( void );
void env_glow( void );
void env_laser( void );
void env_lightning( void );
void env_message( void );
void env_rain( void );
void env_render( void );
void env_shake( void );
void env_shooter( void );
void env_snow( void );
void env_sound( void );
void env_spark( void );
void env_sprite( void );
void fireanddie( void );
void func_bomb_target( void );
void func_breakable( void );
void func_button( void );
void func_buyzone( void );
void func_conveyor( void );
void func_door( void );
void func_door_rotating( void );
void func_escapezone( void );
void func_friction( void );
void func_grencatch( void );
void func_guntarget( void );
void func_healthcharger( void );
void func_hostage_rescue( void );
void func_illusionary( void );
void func_ladder( void );
void func_monsterclip( void );
void func_mortar_field( void );
void func_pendulum( void );
void func_plat( void );
void func_platrot( void );
void func_pushable( void );
void func_rain( void );
void func_recharge( void );
void func_rot_button( void );
void func_rotating( void );
void func_snow( void );
void func_tank( void );
void func_tankcontrols( void );
void func_tanklaser( void );
void func_tankmortar( void );
void func_tankrocket( void );
void func_trackautochange( void );
void func_trackchange( void );
void func_tracktrain( void );
void func_train( void );
void func_traincontrols( void );
void func_vehicle( void );
void func_vehiclecontrols( void );
void func_vip_safetyzone( void );
void func_wall( void );
void func_wall_toggle( void );
void func_water( void );
void func_weaponcheck( void );
void game_counter( void );
void game_counter_set( void );
void game_end( void );
void game_player_equip( void );
void game_player_hurt( void );
void game_player_team( void );
void game_score( void );
void game_team_master( void );
void game_team_set( void );
void game_text( void );
void game_zone_player( void );
void gib( void );
void gibshooter( void );
void grenade( void );
void hostage_entity( void );
void info_bomb_target( void );
void info_hostage_rescue( void );
void info_intermission( void );
void info_landmark( void );
void info_map_parameters( void );
void info_null( void );
void info_player_deathmatch( void );
void info_player_start( void );
void info_spawn_point( void );
void info_target( void );
void info_teleport_destination( void );
void info_vip_start( void );
void infodecal( void );
void item_airbox( void );
void item_airtank( void );
void item_antidote( void );
void item_assaultsuit( void );
void item_battery( void );
void item_healthkit( void );
void item_kevlar( void );
void item_longjump( void );
void item_security( void );
void item_sodacan( void );
void item_suit( void );
void item_thighpack( void );
void light( void );
void light_environment( void );
void light_spot( void );
void momentary_door( void );
void momentary_rot_button( void );
void monster_hevsuit_dead( void );
void monster_mortar( void );
void monster_scientist( void );
void multi_manager( void );
void multisource( void );
void path_corner( void );
void path_track( void );
void player( void );
void player_loadsaved( void );
void player_weaponstrip( void );
void point_clientcommand( void );
void point_servercommand( void );
void soundent( void );
void spark_shower( void );
void speaker( void );
void target_cdaudio( void );
void test_effect( void );
void trigger( void );
void trigger_auto( void );
void trigger_autosave( void );
void trigger_camera( void );
void trigger_cdaudio( void );
void trigger_changelevel( void );
void trigger_changetarget( void );
void trigger_counter( void );
void trigger_endsection( void );
void trigger_gravity( void );
void trigger_hurt( void );
void trigger_monsterjump( void );
void trigger_multiple( void );
void trigger_once( void );
void trigger_push( void );
void trigger_random( void );
void trigger_random_time( void );
void trigger_random_unique( void );
void trigger_relay( void );
void trigger_setorigin( void );
void trigger_teleport( void );
void trigger_transition( void );
void weapon_ak47( void );
void weapon_aug( void );
void weapon_awp( void );
void weapon_c4( void );
void weapon_deagle( void );
void weapon_elite( void );
void weapon_famas( void );
void weapon_fiveseven( void );
void weapon_flashbang( void );
void weapon_g3sg1( void );
void weapon_galil( void );
void weapon_glock18( void );
void weapon_hegrenade( void );
void weapon_knife( void );
void weapon_m249( void );
void weapon_m3( void );
void weapon_m4a1( void );
void weapon_mac10( void );
void weapon_mp5navy( void );
void weapon_p228( void );
void weapon_p90( void );
void weapon_scout( void );
void weapon_sg550( void );
void weapon_sg552( void );
void weapon_shield( void );
void weapon_smokegrenade( void );
void weapon_tmp( void );
void weapon_ump45( void );
void weapon_usp( void );
void weapon_xm1014( void );
void weaponbox( void );
void world_items( void );
void worldspawn( void );
}

#define X(n) { #n, (void *)&n }

table_t lib_hl_exports[] = {
	X( GiveFnptrsToDll ),
	X( GetEntityAPI ),
	X( GetNewDLLFunctions ),
	X( Server_GetBlendingInterface ),

	X( DelayedUse ),
	X( ambient_generic ),
	X( ammo_338magnum ),
	X( ammo_357sig ),
	X( ammo_45acp ),
	X( ammo_50ae ),
	X( ammo_556nato ),
	X( ammo_556natobox ),
	X( ammo_57mm ),
	X( ammo_762nato ),
	X( ammo_9mm ),
	X( ammo_buckshot ),
	X( armoury_entity ),
	X( beam ),
	X( bodyque ),
	X( bot ),
	X( button_target ),
	X( cycler ),
	X( cycler_prdroid ),
	X( cycler_sprite ),
	X( cycler_weapon ),
	X( cycler_wreckage ),
	X( env_beam ),
	X( env_beverage ),
	X( env_blood ),
	X( env_bombglow ),
	X( env_bubbles ),
	X( env_debris ),
	X( env_explosion ),
	X( env_fade ),
	X( env_fog ),
	X( env_funnel ),
	X( env_global ),
	X( env_glow ),
	X( env_laser ),
	X( env_lightning ),
	X( env_message ),
	X( env_rain ),
	X( env_render ),
	X( env_shake ),
	X( env_shooter ),
	X( env_snow ),
	X( env_sound ),
	X( env_spark ),
	X( env_sprite ),
	X( fireanddie ),
	X( func_bomb_target ),
	X( func_breakable ),
	X( func_button ),
	X( func_buyzone ),
	X( func_conveyor ),
	X( func_door ),
	X( func_door_rotating ),
	X( func_escapezone ),
	X( func_friction ),
	X( func_grencatch ),
	X( func_guntarget ),
	X( func_healthcharger ),
	X( func_hostage_rescue ),
	X( func_illusionary ),
	X( func_ladder ),
	X( func_monsterclip ),
	X( func_mortar_field ),
	X( func_pendulum ),
	X( func_plat ),
	X( func_platrot ),
	X( func_pushable ),
	X( func_rain ),
	X( func_recharge ),
	X( func_rot_button ),
	X( func_rotating ),
	X( func_snow ),
	X( func_tank ),
	X( func_tankcontrols ),
	X( func_tanklaser ),
	X( func_tankmortar ),
	X( func_tankrocket ),
	X( func_trackautochange ),
	X( func_trackchange ),
	X( func_tracktrain ),
	X( func_train ),
	X( func_traincontrols ),
	X( func_vehicle ),
	X( func_vehiclecontrols ),
	X( func_vip_safetyzone ),
	X( func_wall ),
	X( func_wall_toggle ),
	X( func_water ),
	X( func_weaponcheck ),
	X( game_counter ),
	X( game_counter_set ),
	X( game_end ),
	X( game_player_equip ),
	X( game_player_hurt ),
	X( game_player_team ),
	X( game_score ),
	X( game_team_master ),
	X( game_team_set ),
	X( game_text ),
	X( game_zone_player ),
	X( gib ),
	X( gibshooter ),
	X( grenade ),
	X( hostage_entity ),
	X( info_bomb_target ),
	X( info_hostage_rescue ),
	X( info_intermission ),
	X( info_landmark ),
	X( info_map_parameters ),
	X( info_null ),
	X( info_player_deathmatch ),
	X( info_player_start ),
	X( info_spawn_point ),
	X( info_target ),
	X( info_teleport_destination ),
	X( info_vip_start ),
	X( infodecal ),
	X( item_airbox ),
	X( item_airtank ),
	X( item_antidote ),
	X( item_assaultsuit ),
	X( item_battery ),
	X( item_healthkit ),
	X( item_kevlar ),
	X( item_longjump ),
	X( item_security ),
	X( item_sodacan ),
	X( item_suit ),
	X( item_thighpack ),
	X( light ),
	X( light_environment ),
	X( light_spot ),
	X( momentary_door ),
	X( momentary_rot_button ),
	X( monster_hevsuit_dead ),
	X( monster_mortar ),
	X( monster_scientist ),
	X( multi_manager ),
	X( multisource ),
	X( path_corner ),
	X( path_track ),
	X( player ),
	X( player_loadsaved ),
	X( player_weaponstrip ),
	X( point_clientcommand ),
	X( point_servercommand ),
	X( soundent ),
	X( spark_shower ),
	X( speaker ),
	X( target_cdaudio ),
	X( test_effect ),
	X( trigger ),
	X( trigger_auto ),
	X( trigger_autosave ),
	X( trigger_camera ),
	X( trigger_cdaudio ),
	X( trigger_changelevel ),
	X( trigger_changetarget ),
	X( trigger_counter ),
	X( trigger_endsection ),
	X( trigger_gravity ),
	X( trigger_hurt ),
	X( trigger_monsterjump ),
	X( trigger_multiple ),
	X( trigger_once ),
	X( trigger_push ),
	X( trigger_random ),
	X( trigger_random_time ),
	X( trigger_random_unique ),
	X( trigger_relay ),
	X( trigger_setorigin ),
	X( trigger_teleport ),
	X( trigger_transition ),
	X( weapon_ak47 ),
	X( weapon_aug ),
	X( weapon_awp ),
	X( weapon_c4 ),
	X( weapon_deagle ),
	X( weapon_elite ),
	X( weapon_famas ),
	X( weapon_fiveseven ),
	X( weapon_flashbang ),
	X( weapon_g3sg1 ),
	X( weapon_galil ),
	X( weapon_glock18 ),
	X( weapon_hegrenade ),
	X( weapon_knife ),
	X( weapon_m249 ),
	X( weapon_m3 ),
	X( weapon_m4a1 ),
	X( weapon_mac10 ),
	X( weapon_mp5navy ),
	X( weapon_p228 ),
	X( weapon_p90 ),
	X( weapon_scout ),
	X( weapon_sg550 ),
	X( weapon_sg552 ),
	X( weapon_shield ),
	X( weapon_smokegrenade ),
	X( weapon_tmp ),
	X( weapon_ump45 ),
	X( weapon_usp ),
	X( weapon_xm1014 ),
	X( weaponbox ),
	X( world_items ),
	X( worldspawn ),
	{ NULL, NULL }
};
