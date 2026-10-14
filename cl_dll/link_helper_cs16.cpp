#include <cstddef>

// wii / Xash3D FWGS: tabla de enlace estatico para el cliente CS 1.6.
// El engine resuelve funciones por nombre via lib_cl_dll_exports[].
// NO incluimos cdll_int.h para evitar arrastrar todo el arbol de headers.

typedef struct table_s { const char *name; void *pointer; } table_t;

extern "C" {
extern void Initialize();
extern void HUD_VidInit();
extern void HUD_Init();
extern void HUD_Shutdown();
extern void HUD_Redraw();
extern void HUD_UpdateClientData();
extern void HUD_Reset();
extern void HUD_PlayerMove();
extern void HUD_PlayerMoveInit();
extern void HUD_PlayerMoveTexture();
extern void HUD_ConnectionlessPacket();
extern void HUD_GetHullBounds();
extern void HUD_Frame();
extern void HUD_PostRunCmd();
extern void HUD_Key_Event();
extern void HUD_AddEntity();
extern void HUD_CreateEntities();
extern void HUD_StudioEvent();
extern void HUD_TxferLocalOverrides();
extern void HUD_ProcessPlayerState();
extern void HUD_TxferPredictionData();
extern void HUD_TempEntUpdate();
extern void HUD_DrawNormalTriangles();
extern void HUD_DrawTransparentTriangles();
extern void HUD_GetUserEntity();
extern void Demo_ReadBuffer();
extern void CAM_Think();
extern void CL_IsThirdPerson();
extern void CL_CameraOffset();
extern void CL_CreateMove();
extern void IN_ActivateMouse();
extern void IN_DeactivateMouse();
extern void IN_MouseEvent();
extern void IN_Accumulate();
extern void IN_ClearStates();
extern void V_CalcRefdef();
extern void KB_Find();
extern void HUD_GetStudioModelInterface();
extern void HUD_DirectorMessage();
extern void HUD_VoiceStatus();
extern void HUD_ChatInputPosition();
extern void HUD_GetRenderInterface();
}

__attribute__((visibility("default")))
table_t lib_cl_dll_exports[] = {
    { "Initialize",                   (void*)Initialize },
    { "HUD_VidInit",                  (void*)HUD_VidInit },
    { "HUD_Init",                     (void*)HUD_Init },
    { "HUD_Shutdown",                 (void*)HUD_Shutdown },
    { "HUD_Redraw",                   (void*)HUD_Redraw },
    { "HUD_UpdateClientData",         (void*)HUD_UpdateClientData },
    { "HUD_Reset",                    (void*)HUD_Reset },
    { "HUD_PlayerMove",               (void*)HUD_PlayerMove },
    { "HUD_PlayerMoveInit",           (void*)HUD_PlayerMoveInit },
    { "HUD_PlayerMoveTexture",        (void*)HUD_PlayerMoveTexture },
    { "HUD_ConnectionlessPacket",     (void*)HUD_ConnectionlessPacket },
    { "HUD_GetHullBounds",            (void*)HUD_GetHullBounds },
    { "HUD_Frame",                    (void*)HUD_Frame },
    { "HUD_PostRunCmd",               (void*)HUD_PostRunCmd },
    { "HUD_Key_Event",                (void*)HUD_Key_Event },
    { "HUD_AddEntity",                (void*)HUD_AddEntity },
    { "HUD_CreateEntities",           (void*)HUD_CreateEntities },
    { "HUD_StudioEvent",              (void*)HUD_StudioEvent },
    { "HUD_TxferLocalOverrides",      (void*)HUD_TxferLocalOverrides },
    { "HUD_ProcessPlayerState",       (void*)HUD_ProcessPlayerState },
    { "HUD_TxferPredictionData",      (void*)HUD_TxferPredictionData },
    { "HUD_TempEntUpdate",            (void*)HUD_TempEntUpdate },
    { "HUD_DrawNormalTriangles",      (void*)HUD_DrawNormalTriangles },
    { "HUD_DrawTransparentTriangles", (void*)HUD_DrawTransparentTriangles },
    { "HUD_GetUserEntity",            (void*)HUD_GetUserEntity },
    { "Demo_ReadBuffer",              (void*)Demo_ReadBuffer },
    { "CAM_Think",                    (void*)CAM_Think },
    { "CL_IsThirdPerson",             (void*)CL_IsThirdPerson },
    { "CL_CameraOffset",              (void*)CL_CameraOffset },
    { "CL_CreateMove",                (void*)CL_CreateMove },
    // XASH_OGC pide estos con doble guion bajo
    { "IN__ActivateMouse",            (void*)IN_ActivateMouse },
    { "IN__DeactivateMouse",          (void*)IN_DeactivateMouse },
    { "IN__MouseEvent",               (void*)IN_MouseEvent },
    { "IN_Accumulate",                (void*)IN_Accumulate },
    { "IN_ClearStates",               (void*)IN_ClearStates },
    { "V_CalcRefdef",                 (void*)V_CalcRefdef },
    { "KB_Find",                      (void*)KB_Find },

    // opcionales
    { "HUD_GetStudioModelInterface",  (void*)HUD_GetStudioModelInterface },
    { "HUD_DirectorMessage",          (void*)HUD_DirectorMessage },
    { "HUD_VoiceStatus",              (void*)HUD_VoiceStatus },
    { "HUD_ChatInputPosition",        (void*)HUD_ChatInputPosition },
    { "HUD_GetRenderInterface",       (void*)HUD_GetRenderInterface },
    { NULL, NULL }
};
