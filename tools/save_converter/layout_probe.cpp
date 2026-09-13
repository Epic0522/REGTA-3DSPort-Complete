#include "common.h"
#include "Script.h"
#include "PlayerPed.h"
#include "PlayerInfo.h"
#include "ParticleObject.h"
#include "Phones.h"
#include "Zones.h"

#include <stddef.h>

#define EXPORT_VALUE(name, value) extern "C" const unsigned name = (unsigned)(value)

EXPORT_VALUE(probe_size_running_script, sizeof(CRunningScript));
EXPORT_VALUE(probe_off_script_name, offsetof(CRunningScript, m_abScriptName));
EXPORT_VALUE(probe_off_script_ip, offsetof(CRunningScript, m_nIp));
EXPORT_VALUE(probe_off_script_stack, offsetof(CRunningScript, m_anStack));
EXPORT_VALUE(probe_off_script_stack_pointer, offsetof(CRunningScript, m_nStackPointer));
EXPORT_VALUE(probe_off_script_locals, offsetof(CRunningScript, m_anLocalVariables));
EXPORT_VALUE(probe_off_script_cond_result, offsetof(CRunningScript, m_bCondResult));
EXPORT_VALUE(probe_off_script_is_mission, offsetof(CRunningScript, m_bIsMissionScript));
EXPORT_VALUE(probe_off_script_skip_wake, offsetof(CRunningScript, m_bSkipWakeTime));
EXPORT_VALUE(probe_off_script_wake_time, offsetof(CRunningScript, m_nWakeTime));
EXPORT_VALUE(probe_off_script_and_or, offsetof(CRunningScript, m_nAndOrState));
EXPORT_VALUE(probe_off_script_not, offsetof(CRunningScript, m_bNotFlag));
EXPORT_VALUE(probe_off_script_deatharrest_enabled, offsetof(CRunningScript, m_bDeatharrestEnabled));
EXPORT_VALUE(probe_off_script_deatharrest_executed, offsetof(CRunningScript, m_bDeatharrestExecuted));
EXPORT_VALUE(probe_off_script_mission_flag, offsetof(CRunningScript, m_bMissionFlag));

#ifdef MIAMI
EXPORT_VALUE(probe_off_script_is_active, offsetof(CRunningScript, m_bIsActive));
#endif

EXPORT_VALUE(probe_size_weapon, sizeof(CWeapon));
EXPORT_VALUE(probe_off_weapon_type, offsetof(CWeapon, m_eWeaponType));
EXPORT_VALUE(probe_off_weapon_state, offsetof(CWeapon, m_eWeaponState));
EXPORT_VALUE(probe_off_weapon_ammo_clip, offsetof(CWeapon, m_nAmmoInClip));
EXPORT_VALUE(probe_off_weapon_ammo_total, offsetof(CWeapon, m_nAmmoTotal));
EXPORT_VALUE(probe_off_weapon_timer, offsetof(CWeapon, m_nTimer));
EXPORT_VALUE(probe_off_weapon_rot_offset, offsetof(CWeapon, m_bAddRotOffset));

EXPORT_VALUE(probe_size_player_ped, sizeof(CPlayerPed));
EXPORT_VALUE(probe_off_player_position, offsetof(CPlayerPed, m_matrix) + offsetof(CMatrix, px));
EXPORT_VALUE(probe_off_player_created_by, offsetof(CPlayerPed, CharCreatedBy));
EXPORT_VALUE(probe_off_player_health, offsetof(CPlayerPed, m_fHealth));
EXPORT_VALUE(probe_off_player_armour, offsetof(CPlayerPed, m_fArmour));
EXPORT_VALUE(probe_off_player_weapons, offsetof(CPlayerPed, m_weapons));
EXPORT_VALUE(probe_off_player_max_stamina, offsetof(CPlayerPed, m_fMaxStamina));
EXPORT_VALUE(probe_off_player_targets, offsetof(CPlayerPed, m_nTargettableObjects));

#ifndef MIAMI
EXPORT_VALUE(probe_off_player_max_weapon_type, offsetof(CPlayerPed, m_maxWeaponTypeAllowed));
#endif

EXPORT_VALUE(probe_size_player_info, sizeof(CPlayerInfo));

EXPORT_VALUE(probe_size_phone, sizeof(CPhone));
EXPORT_VALUE(probe_off_phone_state, offsetof(CPhone, m_nState));
EXPORT_VALUE(probe_off_phone_visible, offsetof(CPhone, m_visibleToCam));

EXPORT_VALUE(probe_size_zone, sizeof(CZone));
EXPORT_VALUE(probe_off_zone_type, offsetof(CZone, type));
EXPORT_VALUE(probe_off_zone_level, offsetof(CZone, level));
EXPORT_VALUE(probe_off_zone_day, offsetof(CZone, zoneinfoDay));
EXPORT_VALUE(probe_off_zone_child, offsetof(CZone, child));

EXPORT_VALUE(probe_size_particle_object, sizeof(CParticleObject));
EXPORT_VALUE(probe_off_particle_position, offsetof(CParticleObject, m_matrix) + offsetof(CMatrix, px));
EXPORT_VALUE(probe_off_particle_remove_timer, offsetof(CParticleObject, m_nRemoveTimer));
EXPORT_VALUE(probe_off_particle_object_type, offsetof(CParticleObject, m_Type));
EXPORT_VALUE(probe_off_particle_type, offsetof(CParticleObject, m_ParticleType));
EXPORT_VALUE(probe_off_particle_effect_cycles, offsetof(CParticleObject, m_nNumEffectCycles));
EXPORT_VALUE(probe_off_particle_skip_frames, offsetof(CParticleObject, m_nSkipFrames));
EXPORT_VALUE(probe_off_particle_frame_counter, offsetof(CParticleObject, m_nFrameCounter));
EXPORT_VALUE(probe_off_particle_state, offsetof(CParticleObject, m_nState));
EXPORT_VALUE(probe_off_particle_target, offsetof(CParticleObject, m_vecTarget));
EXPORT_VALUE(probe_off_particle_rand, offsetof(CParticleObject, m_fRandVal));
EXPORT_VALUE(probe_off_particle_size, offsetof(CParticleObject, m_fSize));
EXPORT_VALUE(probe_off_particle_color, offsetof(CParticleObject, m_Color));
EXPORT_VALUE(probe_off_particle_remove, offsetof(CParticleObject, m_bRemove));
EXPORT_VALUE(probe_off_particle_creation_chance, offsetof(CParticleObject, m_nCreationChance));
