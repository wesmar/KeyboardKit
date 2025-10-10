/*++

Module Name:
    scancode.h

Abstract:
    Keyboard scan code processing interface.

Environment:
    Kernel mode only.

--*/

#pragma once

#include "driver.h"

#define KBD_STATE_SHIFT         0x00000001
#define KBD_STATE_CAPSLOCK      0x00000002
#define KBD_STATE_NUMLOCK       0x00000004

VOID KbdHandler_InitializeBuffer(VOID);
VOID KbdHandler_SendBufferedLine(VOID);
VOID KbdHandler_FlushBuffer(VOID);
BOOLEAN KbdHandler_AddToLineBuffer(_In_ PCUCHAR KeyString);
VOID KbdHandler_ProcessScanCode(_In_ PKEYBOARD_INPUT_DATA InputData);
VOID KbdHandler_ConfigureMapping(_In_ PKEYBOARD_INPUT_DATA InputData);