// wii / Xash3D FWGS: puente de enlace estatico para el cliente CS 1.6.
// El engine resuelve funciones por nombre via lib_cl_dll_exports[]
// (ver engine/generated_library_tables.h y engine/client/dll_int/cl_game.c).
// Cuando todo se enlaza estatico en el .dol, esta tabla debe existir
// sin weak y con visibilidad default.

#include "cdll_int.h"

typedef struct table_s { const char *name; void *pointer; } table_t;

extern "C" __attribute__((visibility("default")))
table_t lib_cl_dll_exports[] = {
    { "Initialize",                  (void*)Initialize },
    { "HUD_VidInit",                 (void*)HUD_VidInit },
    { "HUD_Init",                    (void*)HUD_Init },
    { "HUD_Shutdown",                (void*)HUD_Shutdown },
    { "HUD_Redraw",                  (void*)HUD_Redraw },
    { "HUD_UpdateClientData",        (void*)HUD_UpdateClientData },
    { "HUD_Reset",                   (void*)HUD_Reset },
    { "HUD_PlayerMove",              (void*)HUD_PlayerMove },
    { "HUD_PlayerMoveInit",          (void*)HUD_PlayerMoveInit },
    { "HUD_ConnectionlessPacket",    (void*)HUD_ConnectionlessPacket },
    { "HUD_GetHullBounds",           (void*)HUD_GetHullBounds },
    { "HUD_Frame",                   (void*)HUD_Frame },
    { "HUD_PostRunCmd",              (void*)HUD_PostRunCmd },
    { "HUD_Key_Event",               (void*)HUD_Key_Event },
    { "HUD_AddEntity",               (void*)HUD_AddEntity },
    { "HUD_CreateEntities",          (void*)HUD_CreateEntities },
    { "HUD_StudioEvent",             (void*)HUD_StudioEvent },
    { "HUD_TxferLocalOverrides",     (void*)HUD_TxferLocalOverrides },
    { "HUD_ProcessPlayerState",      (void*)HUD_ProcessPlayerState },
    { "HUD_TxferPredictionData",     (void*)HUD_TxferPredictionData },
    { "HUD_TempEntUpdate",           (void*)HUD_TempEntUpdate },
    { "HUD_DrawNormalTriangles",     (void*)HUD_DrawNormalTriangles },
    { "HUD_DrawTransparentTriangles",(void*)HUD_DrawTransparentTriangles },
    { "Demo_ReadBuffer",             (void*)Demo_ReadBuffer },
    { "CAM_Think",                   (void*)CAM_Think },
    { "CL_IsThirdPerson",            (void*)CL_IsThirdPerson },
    { "CL_CameraOffset",             (void*)CL_CameraOffset },
    { "CL_CreateMove",               (void*)CL_CreateMove },
    { "IN__DeactivateMouse",         (void*)IN_DeactivateMouse },
    { "IN__MouseEvent",              (void*)IN_MouseEvent },
    { "IN_Accumulate",               (void*)IN_Accumulate },
    { "IN_ClearStates",              (void*)IN_ClearStates },
    { "V_CalcRefdef",                (void*)V_CalcRefdef },

    // opcionales
    { "HUD_GetStudioModelInterface", (void*)HUD_GetStudioModelInterface },
    { "HUD_DirectorMessage",         (void*)HUD_DirectorMessage },
    { "HUD_VoiceStatus",             (void*)HUD_VoiceStatus },
    { "HUD_ChatInputPosition",       (void*)HUD_ChatInputPosition },
    { "HUD_GetRenderInterface",      (void*)HUD_GetRenderInterface },

    { NULL, NULL }
};
